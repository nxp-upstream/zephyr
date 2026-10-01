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
 *   video_src -> jpeg_parser -> caps_filter -> queue_parse -> jpeg_decoder -> queue_dec
 *             -> disp_sink
 *
 * and, with CONFIG_ANDROID_LINK_AUDIO, a second pipeline plays the raw PCM
 * the phone sends on its audio connection:
 *
 *   audio_src -> audio_parser -> queue_audio -> i2s_sink
 *
 * With CONFIG_ANDROID_LINK_MIC a third pipeline sends the board's microphone
 * to the phone as raw PCM, on a connection of its own:
 *
 *   mic_src -> mic_caps_filter -> queue_mic -> mic_sink
 *
 * The phone opens its connections to one port, video first, then audio, then
 * control, then the microphone, so the application owns the listening socket,
 * accepts them in that order and hands the stream sockets to the pipelines'
 * socket source and sink. The streams are not synchronized with each other,
 * as in the phone's own client: each runs at the lowest latency it can. The
 * touch controller is read locally and injected touch events are sent back to
 * the phone over the control connection, which the application keeps; the
 * same connection tells the phone what to do with the microphone stream.
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
#include <zephyr/mpipe/net/mpipe_socket_sink.h>
#include <zephyr/mpipe/net/mpipe_socket_src.h>

#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_decoder.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#if defined(CONFIG_ANDROID_LINK_AUDIO)
#include <zephyr/mpipe/aud/mpipe_aud_i2s_codec_sink.h>
#include <zephyr/mpipe/aud/mpipe_aud_raw_parser.h>
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
#include <zephyr/mpipe/aud/mpipe_aud_dmic_src.h>
#endif

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
	VIDEO_SRC_ID,
	JPEG_PARSER_ID,
	QUEUE_PARSE_ID,
	JPEG_DEC_ID,
	QUEUE_DEC_ID,
	CAPS_FILTER_ID,
	DISP_SINK_ID,
	AUDIO_PIPE_ID,
	AUDIO_SRC_ID,
	AUDIO_PARSER_ID,
	QUEUE_AUDIO_ID,
	I2S_SINK_ID,
	MIC_PIPE_ID,
	MIC_SRC_ID,
	MIC_CAPS_FILTER_ID,
	QUEUE_MIC_ID,
	MIC_SINK_ID,
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
static struct mpipe_socket_src video_src;

static struct mpipe_img_jpeg_parser jpeg_parser;
static struct mpipe_img_jpeg_decoder jpeg_dec;
static struct mpipe_disp_sink disp_sink;
static struct mpipe_queue queue_parse;
static struct mpipe_queue queue_dec;
static struct mpipe_caps_filter caps_filter;
static struct mpipe_player player;

#if defined(CONFIG_ANDROID_LINK_AUDIO)
/* What the phone sends with audio_codec=raw: 48 kHz, 16-bit, stereo */
#define AUDIO_SAMPLE_RATE     48000
#define AUDIO_BIT_WIDTH       16
#define AUDIO_CHANNELS        2
/* One I2S block covers 10 ms; the queue holds 150 ms of them against Wi-Fi jitter */
#define AUDIO_BLOCK_US        10000
#define AUDIO_QUEUE_BLOCKS    15

static struct mpipe audio_pipe;
static struct mpipe_socket_src audio_src;
static struct mpipe_aud_raw_parser audio_parser;
static struct mpipe_queue queue_audio;
static struct mpipe_aud_i2s_codec_sink i2s_sink;
static struct mpipe_player audio_player;

/* The I2S driver returns the blocks it has sent to this slab; it must not be cached */
static __nocache struct k_mem_slab audio_slab;
#endif

#if defined(CONFIG_ANDROID_LINK_MIC) || defined(CONFIG_ANDROID_LINK_AUDIO)
/* Above the video decoder: a late audio block is lost, a late frame is only late */
#define AUDIO_THREAD_PRIORITY 4
#endif

#if defined(CONFIG_ANDROID_LINK_MIC)
/*
 * What the microphone path produces and the phone expects: the MICFIL on the
 * RT1170 delivers 16 kHz 32-bit samples and one channel is enough for speech.
 */
#define MIC_SAMPLE_RATE  16000
#define MIC_BIT_WIDTH    32
#define MIC_CHANNELS     1
/* One block covers 10 ms; the queue holds 100 ms of them against Wi-Fi back-pressure */
#define MIC_BLOCK_US     10000
#define MIC_QUEUE_BLOCKS 10

static struct mpipe mic_pipe;
static struct mpipe_aud_dmic_src mic_src;
static struct mpipe_caps_filter mic_caps_filter;
static struct mpipe_queue queue_mic;
static struct mpipe_socket_sink mic_sink;
static struct mpipe_player mic_player;

/* The DMIC driver fills blocks of this slab by DMA; it must not be cached */
static __nocache struct k_mem_slab mic_slab;
#endif

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
 * Build the mpipe pipeline that renders the frames the phone sends to the
 * display. The video socket is handed to the source once a phone connects.
 */
static int build_video_stream_pipeline(void)
{
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_socket_src_init(&video_src, VIDEO_SRC_ID);
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
		(struct mpipe_bin *)&pipe, (struct mpipe_element *)&video_src,
		(struct mpipe_element *)&jpeg_parser, (struct mpipe_element *)&caps_filter,
		(struct mpipe_element *)&queue_parse, (struct mpipe_element *)&jpeg_dec,
		(struct mpipe_element *)&queue_dec, (struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		return ret;
	}

	ret = mpipe_element_link(
		(struct mpipe_element *)&video_src, (struct mpipe_element *)&jpeg_parser,
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

#if defined(CONFIG_ANDROID_LINK_AUDIO)
/*
 * Build the pipeline that plays the raw PCM the phone sends. The parser
 * announces the format, since the stream cannot, and cuts it into the blocks
 * the I2S sink hands to its driver; the queue in between holds enough of them
 * to ride through the jitter of the link.
 */
static int build_audio_stream_pipeline(void)
{
	uint32_t sample_rate = AUDIO_SAMPLE_RATE;
	uint32_t bit_width = AUDIO_BIT_WIDTH;
	uint32_t channels = AUDIO_CHANNELS;
	uint32_t block_us = AUDIO_BLOCK_US;
	int priority = AUDIO_THREAD_PRIORITY;
	int ret;

	ret = mpipe_pipeline_init(&audio_pipe, AUDIO_PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_socket_src_init(&audio_src, AUDIO_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_aud_raw_parser_init(&audio_parser, AUDIO_PARSER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_queue_init(&queue_audio, QUEUE_AUDIO_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_aud_i2s_codec_sink_init(&i2s_sink, I2S_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&audio_pipe,
					  MPIPE_PROP_PIPELINE_THREAD_PRIORITY, &priority,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&audio_parser, MPIPE_PROP_AUD_RAW_PARSER_SLAB_PTR,
		&audio_slab, MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE, &sample_rate,
		MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH, &bit_width,
		MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS, &channels,
		MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL, &block_us, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&queue_audio, MPIPE_PROP_BASE_QUEUE_SIZE,
		&(uint8_t){AUDIO_QUEUE_BLOCKS}, MPIPE_PROP_BASE_QUEUE_LEAK,
		&(enum mpipe_base_queue_leak){MPIPE_BASE_QUEUE_LEAK_OLDEST},
		MPIPE_PROP_BASE_QUEUE_THREAD_PRIORITY, &priority, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&i2s_sink,
					  MPIPE_PROP_AUD_SINK_SLAB_PTR, &audio_slab,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&audio_pipe, (struct mpipe_element *)&audio_src,
			    (struct mpipe_element *)&audio_parser,
			    (struct mpipe_element *)&queue_audio, (struct mpipe_element *)&i2s_sink,
			    NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add the audio elements (%d)", ret);
		return ret;
	}

	ret = mpipe_element_link(
		(struct mpipe_element *)&audio_src, (struct mpipe_element *)&audio_parser,
		(struct mpipe_element *)&queue_audio, (struct mpipe_element *)&i2s_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link the audio elements (%d)", ret);
		return ret;
	}

	LOG_INF("Audio pipeline linked.");

	ret = zbus_chan_add_obs(mpipe_element_get_bus_chan((struct mpipe_element *)&audio_pipe),
				&stream_listener, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	return mpipe_player_init(&audio_player, &audio_pipe);
}
#endif

#if defined(CONFIG_ANDROID_LINK_MIC)
/*
 * Build the pipeline that sends the microphone to the phone. The caps filter
 * pins the format the phone expects out of what the DMIC offers; the queue
 * keeps a slow link from holding the capture thread, dropping the oldest
 * block instead; the socket sink writes the blocks to the connection.
 */
static int build_mic_stream_pipeline(void)
{
	struct mpipe_structure mic_caps;
	int priority = AUDIO_THREAD_PRIORITY;
	int ret;

	ret = mpipe_pipeline_init(&mic_pipe, MIC_PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_aud_dmic_src_init(&mic_src, MIC_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_caps_filter_init(&mic_caps_filter, MIC_CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_queue_init(&queue_mic, QUEUE_MIC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_socket_sink_init(&mic_sink, MIC_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&mic_pipe,
					  MPIPE_PROP_PIPELINE_THREAD_PRIORITY, &priority,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&mic_src,
					  MPIPE_PROP_AUD_SRC_SLAB_PTR, &mic_slab,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_structure_init_fields(&mic_caps, MPIPE_MEDIA_AUDIO_PCM, MPIPE_CAPS_SAMPLE_RATE,
					  MPIPE_TYPE_UINT, MIC_SAMPLE_RATE, MPIPE_CAPS_BIT_WIDTH,
					  MPIPE_TYPE_UINT, MIC_BIT_WIDTH, MPIPE_CAPS_NUM_CHANNELS,
					  MPIPE_TYPE_UINT, MIC_CHANNELS, MPIPE_CAPS_FRAME_INTERVAL,
					  MPIPE_TYPE_UINT, MIC_BLOCK_US, MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&mic_caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &mic_caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&queue_mic, MPIPE_PROP_BASE_QUEUE_SIZE,
		&(uint8_t){MIC_QUEUE_BLOCKS}, MPIPE_PROP_BASE_QUEUE_LEAK,
		&(enum mpipe_base_queue_leak){MPIPE_BASE_QUEUE_LEAK_OLDEST},
		MPIPE_PROP_BASE_QUEUE_THREAD_PRIORITY, &priority, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&mic_pipe, (struct mpipe_element *)&mic_src,
			    (struct mpipe_element *)&mic_caps_filter,
			    (struct mpipe_element *)&queue_mic, (struct mpipe_element *)&mic_sink,
			    NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add the microphone elements (%d)", ret);
		return ret;
	}

	ret = mpipe_element_link(
		(struct mpipe_element *)&mic_src, (struct mpipe_element *)&mic_caps_filter,
		(struct mpipe_element *)&queue_mic, (struct mpipe_element *)&mic_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link the microphone elements (%d)", ret);
		return ret;
	}

	LOG_INF("Microphone pipeline linked.");

	ret = zbus_chan_add_obs(mpipe_element_get_bus_chan((struct mpipe_element *)&mic_pipe),
				&stream_listener, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	return mpipe_player_init(&mic_player, &mic_pipe);
}
#endif

/*
 * Listen on the port the phone connects to. The phone opens video, (audio,)
 * control (and the microphone) in that order, each as a new connection, so
 * the backlog holds them all while the application accepts them one by one.
 */
static int scrcpy_listen(uint16_t port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};
	int opt = 1;
	int fd;

	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("socket() failed (%d)", errno);
		return -errno;
	}

	(void)zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || zsock_listen(fd, 4) < 0) {
		LOG_ERR("Failed to listen on port %u (%d)", port, errno);
		(void)zsock_close(fd);
		return -errno;
	}

	return fd;
}

/* Wait for a player to have brought its pipeline to @p state */
static void wait_for_state(const struct mpipe *p, enum mpipe_state state)
{
	while (p->bin.element.current_state != state) {
		k_msleep(10);
	}
}

/*
 * Hand a connected socket to a pipeline's socket element, once the previous
 * run is torn down. @p key is the element's socket property.
 */
static int hand_socket(struct mpipe *p, struct mpipe_object *obj, uint32_t key, int fd)
{
	wait_for_state(p, MPIPE_STATE_READY);

	return mpipe_object_set_properties(obj, key, &fd, MPIPE_PROP_LIST_END);
}

/* Close the connections of a client that could not be served */
static void close_client(int video_fd, int audio_fd, int control_fd, int mic_fd)
{
	(void)zsock_close(video_fd);
	if (audio_fd >= 0) {
		(void)zsock_close(audio_fd);
	}
	if (control_fd >= 0) {
		(void)zsock_close(control_fd);
	}
	if (mic_fd >= 0) {
		(void)zsock_close(mic_fd);
	}
}

int main(void)
{
	struct sockaddr_in client_addr;
	struct net_if *iface;
	int server_fd;
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
#if defined(CONFIG_ANDROID_LINK_MIC)
	mic_mode_init();
#endif

	ret = build_video_stream_pipeline();
	if (ret < 0) {
		LOG_ERR("Failed to build pipeline (%d)", ret);
		return ret;
	}

#if defined(CONFIG_ANDROID_LINK_AUDIO)
	ret = build_audio_stream_pipeline();
	if (ret < 0) {
		LOG_ERR("Failed to build the audio pipeline (%d)", ret);
		return ret;
	}
#endif

#if defined(CONFIG_ANDROID_LINK_MIC)
	ret = build_mic_stream_pipeline();
	if (ret < 0) {
		LOG_ERR("Failed to build the microphone pipeline (%d)", ret);
		return ret;
	}
#endif

	server_fd = scrcpy_listen(MY_PORT);
	if (server_fd < 0) {
		return server_fd;
	}

	/*
	 * One client at a time: accept its connections in the order the phone
	 * opens them, hand the stream sockets to the pipelines and play. A
	 * player takes its pipeline back to READY when the phone closes the
	 * connection, which closes the socket with it; when any stream ends
	 * or the control connection fails, the other pipelines are stopped too
	 * and the application accepts the next client. The shell still drives
	 * the pipelines with p/s/r/q.
	 */
	while (1) {
		socklen_t addr_len = sizeof(client_addr);
		int video_fd;
		int audio_fd = -1;
		int control_fd;
		int mic_fd = -1;

		LOG_INF("Waiting for a client on port %u", MY_PORT);

		video_fd = zsock_accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
		if (video_fd < 0) {
			LOG_ERR("accept() failed (%d)", errno);
			k_msleep(1000);
			continue;
		}

		/* The control messages go back to the phone that sends the video */
		client_addr.sin_port = htons(MY_PORT);

		if (IS_ENABLED(CONFIG_ANDROID_LINK_AUDIO)) {
			audio_fd = zsock_accept(server_fd, NULL, NULL);
			if (audio_fd < 0) {
				LOG_ERR("accept() of the audio connection failed (%d)", errno);
				close_client(video_fd, -1, -1, -1);
				continue;
			}
		}

		control_fd = zsock_accept(server_fd, NULL, NULL);
		if (control_fd < 0) {
			LOG_ERR("accept() of the control connection failed (%d)", errno);
			close_client(video_fd, audio_fd, -1, -1);
			continue;
		}

		if (IS_ENABLED(CONFIG_ANDROID_LINK_MIC)) {
			mic_fd = zsock_accept(server_fd, NULL, NULL);
			if (mic_fd < 0) {
				LOG_ERR("accept() of the microphone connection failed (%d)", errno);
				close_client(video_fd, audio_fd, control_fd, -1);
				continue;
			}
		}

		ret = hand_socket(&pipe, (struct mpipe_object *)&video_src,
				  MPIPE_PROP_SOCKET_SRC_FD, video_fd);
#if defined(CONFIG_ANDROID_LINK_AUDIO)
		if (ret == 0) {
			ret = hand_socket(&audio_pipe, (struct mpipe_object *)&audio_src,
					  MPIPE_PROP_SOCKET_SRC_FD, audio_fd);
		}
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
		if (ret == 0) {
			ret = hand_socket(&mic_pipe, (struct mpipe_object *)&mic_sink,
					  MPIPE_PROP_SOCKET_SINK_FD, mic_fd);
		}
#endif
		if (ret < 0) {
			LOG_ERR("Failed to hand the sockets to the pipelines (%d)", ret);
			close_client(video_fd, audio_fd, control_fd, mic_fd);
			continue;
		}

		(void)mpipe_player_play(&player);
#if defined(CONFIG_ANDROID_LINK_AUDIO)
		(void)mpipe_player_play(&audio_player);
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
		(void)mpipe_player_play(&mic_player);
#endif
		connect_control_socket(control_fd, &client_addr);
#if defined(CONFIG_ANDROID_LINK_MIC)
		control_send_mic_mode(mic_mode_get());
#endif

		/* Serve this client until a stream ends or the control connection fails */
		(void)k_event_wait(&application_event, EVENT_TOUCH_ERROR | EVENT_STREAM_END, false,
				   K_FOREVER);

		(void)disconnect_control_socket();

		/*
		 * Stop whatever is still running, wait for every pipeline to be
		 * torn down, and only then forget the events: an end of stream
		 * posted by another pipeline during the teardown would
		 * otherwise end the next client's session at once.
		 */
		(void)mpipe_player_stop(&player);
		wait_for_state(&pipe, MPIPE_STATE_READY);
#if defined(CONFIG_ANDROID_LINK_AUDIO)
		(void)mpipe_player_stop(&audio_player);
		wait_for_state(&audio_pipe, MPIPE_STATE_READY);
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
		(void)mpipe_player_stop(&mic_player);
		wait_for_state(&mic_pipe, MPIPE_STATE_READY);
#endif
		k_event_clear(&application_event, EVENT_TOUCH_ERROR | EVENT_STREAM_END);
	}

	return 0;
}
