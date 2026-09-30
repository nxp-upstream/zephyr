/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Android link sample built on top of the mpipe media pipeline framework.
 *
 * An Android phone (running an scrcpy-like client) connects over Wi-Fi or
 * Ethernet and streams MJPEG frames to this device over TCP. The frames are
 * fed into an mpipe pipeline that parses, decodes and displays them:
 *
 *   tcp_src -> jpeg_parser -> caps_filter -> queue_parse -> jpeg_decoder -> queue_dec -> disp_sink
 *
 * tcp_src is the TCP server: it binds the port when the pipeline is prepared
 * and waits for the phone to connect when the pipeline starts playing, so the
 * sample only tells it which port to use.
 *
 * The touch controller is read locally and injected touch events are sent back
 * to the phone over a second (control) connection to that same port, accepted
 * on the listening socket tcp_src owns.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/wifi_mgmt.h>

#include <zephyr/zbus/zbus.h>

#include <zephyr/drivers/video.h>
#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/net/mpipe_tcp_server_src.h>

#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_decoder.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/utils/mpipe_player.h>

#include "screen.h"
#include "zephyr/mpipe/mpipe_element.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#define MY_PORT 5000

#define WIFI_AP_SSID "zephyr-link"
#define WIFI_AP_PSK  "zephyrdemo"

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	TCP_SRC_ID,
	JPEG_PARSER_ID,
	QUEUE_PARSE_ID,
	JPEG_DEC_ID,
	QUEUE_DEC_ID,
	CAPS_FILTER_ID,
	DISP_SINK_ID,
};

/* The panel's geometry, pinned behind the JPEG parser by the caps filter.
 * The sender must stream frames of this resolution.
 */
#define DISP_WIDTH   720
#define DISP_HEIGHT  1280

K_EVENT_DEFINE(application_event);

/* The run ended: the phone closed the video connection, or the stream failed */
#define EVENT_STREAM_END BIT(3)

#if !defined(CONFIG_WIFI)
/*
 * Static server address / netmask used on the wired (Ethernet or native_sim
 * TAP) path. The soft-AP Wi-Fi build assigns these through the NXP driver
 * instead (see CONFIG_NXP_WIFI_SOFTAP_IP_*).
 */
static struct in_addr server_addr = {{{192, 0, 2, 1}}};
static struct in_addr netmask = {{{255, 255, 255, 0}}};
#endif

static struct mpipe pipe;
static struct mpipe_tcp_server_src tcp_src;

static struct mpipe_img_jpeg_parser jpeg_parser;
static struct mpipe_img_jpeg_decoder jpeg_dec;
static struct mpipe_disp_sink disp_sink;
static struct mpipe_queue queue_parse;
static struct mpipe_queue queue_dec;
static struct mpipe_caps_filter caps_filter;
static struct mpipe_player player;

static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

/*
 * Runs in the thread posting the message: only note that the run is over. A
 * stop from the shell posts nothing here, so it is not undone by main().
 */
static void stream_msg_cb(const struct zbus_channel *chan)
{
	const struct mpipe_message *msg = zbus_chan_const_msg(chan);

	if (msg->type == MPIPE_MESSAGE_EOS || msg->type == MPIPE_MESSAGE_ERROR) {
		k_event_post(&application_event, EVENT_STREAM_END);
	}
}

ZBUS_LISTENER_DEFINE(stream_listener, stream_msg_cb);

/*
 * Build the mpipe pipeline that serves the phone and renders the frames it
 * sends to the display. Only the port is configured here: tcp_src binds it
 * when the pipeline is prepared and accepts the client when it starts playing.
 */
static int build_video_stream_pipeline(void)
{
	uint16_t port = MY_PORT;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_tcp_server_src_init(&tcp_src, TCP_SRC_ID);

	if (ret < 0) {
		return ret;
	}
	ret = mpipe_img_jpeg_parser_init(&jpeg_parser, JPEG_PARSER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_img_jpeg_decoder_init(&jpeg_dec, JPEG_DEC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_queue_init(&queue_parse, QUEUE_PARSE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_queue_init(&queue_dec, QUEUE_DEC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}

	/*
	 * The parser only tells the format: pin the frame size behind it so
	 * that the decoder and the display negotiate the panel's resolution.
	 */
	struct mpipe_structure stream_caps;

	ret = mpipe_structure_init_fields(&stream_caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					  MPIPE_TYPE_UINT, VIDEO_PIX_FMT_JPEG,
					  MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, DISP_WIDTH,
					  MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, DISP_HEIGHT,
					  MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &stream_caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&tcp_src,
					  MPIPE_PROP_TCP_SERVER_SRC_PORT, &port,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&disp_sink,
					  MPIPE_PROP_DISP_SINK_DEVICE, display_dev,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	/*
	 * A mirror shows the newest frame, so a queue that fills because the
	 * stage behind it is slower drops its oldest frame rather than holding
	 * the producer: frames the decoder cannot keep up with are dropped
	 * before being decoded, and the sender is never throttled into
	 * buffering seconds of frames on its side.
	 */
	ret = mpipe_object_set_properties((struct mpipe_object *)&queue_parse,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &(uint8_t){2},
					  MPIPE_PROP_BASE_QUEUE_LEAK,
					  &(enum mpipe_base_queue_leak){MPIPE_BASE_QUEUE_LEAK_OLDEST},
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&queue_dec,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &(uint8_t){1},
					  MPIPE_PROP_BASE_QUEUE_LEAK,
					  &(enum mpipe_base_queue_leak){MPIPE_BASE_QUEUE_LEAK_OLDEST},
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	/*
	 * The queues are what split the graph across threads. Without them the
	 * whole path - receive, parse, decode, display - runs on the source
	 * thread, so the socket is not read again until a frame has been decoded
	 * and blitted. With them the reader keeps filling the next buffer while
	 * a frame is being decoded, and the decoder while a frame is displayed.
	 */
	ret = mpipe_bin_add(
		(struct mpipe_bin *)&pipe, (struct mpipe_element *)&tcp_src,
		(struct mpipe_element *)&jpeg_parser, (struct mpipe_element *)&caps_filter,
		(struct mpipe_element *)&queue_parse, (struct mpipe_element *)&jpeg_dec,
		(struct mpipe_element *)&queue_dec, (struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		return ret;
	}

	ret = mpipe_element_link(
		(struct mpipe_element *)&tcp_src, (struct mpipe_element *)&jpeg_parser,
		(struct mpipe_element *)&caps_filter, (struct mpipe_element *)&queue_parse,
		(struct mpipe_element *)&jpeg_dec, (struct mpipe_element *)&queue_dec,
		(struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		return ret;
	}

	LOG_INF("Pipeline linked.");

	ret = zbus_chan_add_obs(mpipe_element_get_bus_chan((struct mpipe_element *)&pipe),
				&stream_listener, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	return mpipe_player_init(&player, &pipe);
}

int main(void)
{
	struct sockaddr_in client_addr;
	struct net_if *iface;
	int ret;

	iface = net_if_get_default();

#if defined(CONFIG_WIFI)
	STRUCT_SECTION_FOREACH(net_if, i_iface) {
		if (strncmp(net_if_get_device(i_iface)->name, "ua", 2) == 0) {
			iface = i_iface;
			break;
		}
	}
	static struct wifi_connect_req_params ap_config;

	if (!iface) {
		LOG_INF("AP: is not initialized");
		return -EIO;
	}

	LOG_INF("Turning on AP Mode");
	ap_config.ssid = (const uint8_t *)WIFI_AP_SSID;
	ap_config.ssid_length = strlen(WIFI_AP_SSID);
	ap_config.psk = (const uint8_t *)WIFI_AP_PSK;
	ap_config.psk_length = strlen(WIFI_AP_PSK);
	ap_config.channel = WIFI_CHANNEL_ANY;
	ap_config.band = WIFI_FREQ_BAND_5_GHZ;
	ap_config.bandwidth = WIFI_FREQ_BANDWIDTH_40MHZ;

	if (strlen(WIFI_AP_PSK) == 0) {
		ap_config.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		ap_config.security = WIFI_SECURITY_TYPE_PSK;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_config,
		       sizeof(struct wifi_connect_req_params));
#endif

	LOG_INF("Protocol %s is selected", iface->if_dev->dev->name);

#if !defined(CONFIG_WIFI)
	/*
	 * Non-Wi-Fi (e.g. Ethernet) build: assign the static server address
	 * on the interface. In the Wi-Fi soft-AP build the NXP driver already
	 * assigns the address, gateway, netmask and starts the DHCPv4 server
	 * (see CONFIG_NXP_WIFI_SOFTAP_IP_*), so there is nothing to do here.
	 */
	(void)net_config_init_app(net_if_get_device(iface), "Initializing network");
	(void)net_if_ipv4_addr_add(iface, &server_addr, NET_ADDR_MANUAL, 0);
	(void)net_if_ipv4_set_netmask_by_addr(iface, &server_addr, &netmask);
#endif

	control_init();

	/*
	 * The video path is the player's from here on. The player takes the
	 * pipeline back to READY when the phone closes the video connection,
	 * which also closes the listening port; main() then plays again so the
	 * next client is accepted, and otherwise never touches the pipeline.
	 * The shell still drives it with p/s/r/q.
	 */
	ret = build_video_stream_pipeline();
	if (ret < 0) {
		LOG_ERR("Failed to build pipeline (%d)", ret);
		return ret;
	}

	(void)mpipe_player_play(&player);

	while (1) {
		socklen_t addr_len = sizeof(client_addr);

		/*
		 * The player opens the listening socket on its own worker thread,
		 * so tcp_src.server_fd is not valid the instant play() returns and
		 * is closed again on a stop/replay. Wait for it before each accept.
		 */
		while (pipe.bin.element.current_state != MPIPE_STATE_PLAYING) {
			k_msleep(100);
		}

		/*
		 * The control messages go to the phone that sends the video, so
		 * take its address from the video connection: a send to an
		 * unspecified address is refused by the socket layer.
		 */
		(void)memset(&client_addr, 0, sizeof(client_addr));
		if (zsock_getpeername(tcp_src.ssrc.fd, (struct sockaddr *)&client_addr,
				      &addr_len) < 0) {
			LOG_ERR("Failed to get the video client address (%d)", errno);
		}
		client_addr.sin_family = AF_INET;
		client_addr.sin_port = htons(MY_PORT);

		connect_control_socket(tcp_src.server_fd, &client_addr);

		/* Serve this client until the run ends or the control connection fails */
		uint32_t events = k_event_wait(&application_event,
					       EVENT_TOUCH_ERROR | EVENT_STREAM_END, false,
					       K_FOREVER);

		(void)disconnect_control_socket();
		k_event_clear(&application_event, EVENT_TOUCH_ERROR | EVENT_STREAM_END);

		if ((events & EVENT_STREAM_END) != 0U) {
			/*
			 * Queued behind the player's own end-of-run handling, so the
			 * pipeline is back in READY before it is played again.
			 */
			(void)mpipe_player_play(&player);
		}
	}

	return 0;
}
