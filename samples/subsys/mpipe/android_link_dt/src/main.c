/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Android link sample, its pipelines described in the devicetree.
 *
 * An Android phone (running an scrcpy-like client) connects over Wi-Fi or
 * Ethernet and streams MJPEG frames, raw PCM audio and gets the microphone of
 * the board back, each on a TCP connection of its own. The three pipelines
 * that carry them are described in pipeline.dtsi and built at boot:
 *
 *   video_src -> jpeg_parser -> caps_filter -> queue -> jpeg_decoder
 *             -> queue -> overlay -> disp_sink
 *   audio_src -> audio_parser -> queue -> i2s_sink
 *   mic_src -> caps_filter -> gain -> queue -> mic_sink
 *
 * The application keeps what is its own: the network, the listening socket
 * and the order the phone opens its connections in, the stream sockets it
 * hands to the socket elements, the control connection, the touch controller
 * and the messages drawn by the overlay.
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
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/net/mpipe_socket_sink.h>
#include <zephyr/mpipe/net/mpipe_socket_src.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>

#include "osd.h"
#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

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

/* The pipelines of the devicetree, their players and the elements the application reaches */
#define VIDEO_PIPELINE DT_NODELABEL(video_pipeline)
#define VIDEO_PLAYER   MPIPE_DT_PLAYER_GET(VIDEO_PIPELINE)
#define VIDEO_SRC      MPIPE_DT_ELEMENT_GET(DT_NODELABEL(video_src))
#define OVERLAY        MPIPE_DT_ELEMENT_GET(DT_NODELABEL(overlay))

#if defined(CONFIG_ANDROID_LINK_AUDIO)
#define AUDIO_PIPELINE DT_NODELABEL(audio_pipeline)
#define AUDIO_PLAYER   MPIPE_DT_PLAYER_GET(AUDIO_PIPELINE)
#define AUDIO_SRC      MPIPE_DT_ELEMENT_GET(DT_NODELABEL(audio_src))
#endif

#if defined(CONFIG_ANDROID_LINK_MIC)
#define MIC_PIPELINE DT_NODELABEL(mic_pipeline)
#define MIC_PLAYER   MPIPE_DT_PLAYER_GET(MIC_PIPELINE)
#define MIC_SINK     MPIPE_DT_ELEMENT_GET(DT_NODELABEL(mic_sink))
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

/* The end of the stream of a pipeline, or its failure, ends the session */
static int observe_pipeline(struct mpipe *pipeline)
{
	return zbus_chan_add_obs(mpipe_element_get_bus_chan((struct mpipe_element *)pipeline),
				 &stream_listener, K_FOREVER);
}

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

/*
 * Notice a phone that vanished without closing: a Wi-Fi drop leaves the
 * sockets open on both sides, the reads blocked and the writes retransmitting
 * for minutes. Keep-alive probes fail them within about twenty seconds.
 */
static void set_keepalive(int fd)
{
	int on = 1;
	int idle = 5;
	int interval = 5;
	int count = 3;

	(void)zsock_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
	(void)zsock_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
	(void)zsock_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
	(void)zsock_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

#if defined(CONFIG_WIFI)
/* The access point knows at once when the phone leaves: end the session then */
static void wifi_event_cb(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_AP_STA_DISCONNECTED) {
		LOG_INF("The phone left the network");
		k_event_post(&application_event, EVENT_STREAM_END);
	}
}

static struct net_mgmt_event_callback wifi_event_cb_data;
#endif

/*
 * Wait for a player to have settled its pipeline in @p state, with every
 * command it was given applied, and say so when it takes long: a teardown
 * that never completes would otherwise look like a board that stopped
 * accepting the phone. Reading the pipeline's state alone is not enough: a
 * stop posted behind a play still in progress finds the pipeline READY
 * before the play has even started, and the end of stream that play then
 * produces would end the next client's session at once.
 */
static void wait_for_state(struct mpipe_player *pl, enum mpipe_state state)
{
	uint32_t waited_ms = 0;

	while (mpipe_player_wait_state(pl, state, K_MSEC(5000)) != 0) {
		waited_ms += 5000;
		LOG_WRN("Player #%u not settled in state %d after %u ms",
			pl->pipeline->bin.element.object.id, state, waited_ms);
	}
}

/*
 * Ask a player to stop until the command is queued: a pipeline whose stream
 * just failed can fill the player's queue with error reports for a moment.
 */
static void stop_player(struct mpipe_player *pl)
{
	while (mpipe_player_stop(pl) != 0) {
		k_msleep(10);
	}
}

/*
 * Hand a connected socket to a pipeline's socket element, once the previous
 * run is torn down. @p key is the element's socket property.
 */
static int hand_socket(struct mpipe_player *pl, struct mpipe_object *obj, uint32_t key, int fd)
{
	wait_for_state(pl, MPIPE_STATE_READY);

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
	ap_config.ssid = (const uint8_t *)CONFIG_ANDROID_LINK_WIFI_SSID;
	ap_config.ssid_length = strlen(CONFIG_ANDROID_LINK_WIFI_SSID);
	ap_config.psk = (const uint8_t *)CONFIG_ANDROID_LINK_WIFI_PSK;
	ap_config.psk_length = strlen(CONFIG_ANDROID_LINK_WIFI_PSK);
	ap_config.channel = WIFI_CHANNEL_ANY;
	ap_config.band = WIFI_FREQ_BAND_5_GHZ;
	ap_config.bandwidth = WIFI_FREQ_BANDWIDTH_40MHZ;

	if (strlen(CONFIG_ANDROID_LINK_WIFI_PSK) == 0) {
		ap_config.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		ap_config.security = WIFI_SECURITY_TYPE_PSK;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_config,
		       sizeof(struct wifi_connect_req_params));

	net_mgmt_init_event_callback(&wifi_event_cb_data, wifi_event_cb,
				     NET_EVENT_WIFI_AP_STA_DISCONNECTED);
	net_mgmt_add_event_callback(&wifi_event_cb_data);
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
	(void)display_setup(display_dev);
#if defined(CONFIG_ANDROID_LINK_MIC)
	demo_mode_init();
#endif

	/* The overlay draws the messages of the demo */
	ret = osd_init((struct mpipe_vid_overlay *)OVERLAY, display_dev);
	if (ret == 0) {
		ret = observe_pipeline(MPIPE_DT_PIPELINE_GET(VIDEO_PIPELINE));
	}
#if defined(CONFIG_ANDROID_LINK_AUDIO)
	if (ret == 0) {
		ret = observe_pipeline(MPIPE_DT_PIPELINE_GET(AUDIO_PIPELINE));
	}
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
	if (ret == 0) {
		ret = observe_pipeline(MPIPE_DT_PIPELINE_GET(MIC_PIPELINE));
	}
#endif
	if (ret < 0) {
		LOG_ERR("Failed to set the pipelines up (%d)", ret);
		return ret;
	}

	server_fd = scrcpy_listen(CONFIG_ANDROID_LINK_PORT);
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

		LOG_INF("Waiting for a client on port %u", CONFIG_ANDROID_LINK_PORT);
		osd_idle("Waiting for phone");

		video_fd = zsock_accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
		if (video_fd < 0) {
			LOG_ERR("accept() failed (%d)", errno);
			k_msleep(1000);
			continue;
		}

		/* The control messages go back to the phone that sends the video */
		client_addr.sin_port = htons(CONFIG_ANDROID_LINK_PORT);
		set_keepalive(video_fd);

		if (IS_ENABLED(CONFIG_ANDROID_LINK_AUDIO)) {
			audio_fd = zsock_accept(server_fd, NULL, NULL);
			if (audio_fd < 0) {
				LOG_ERR("accept() of the audio connection failed (%d)", errno);
				close_client(video_fd, -1, -1, -1);
				continue;
			}
			set_keepalive(audio_fd);
		}

		control_fd = zsock_accept(server_fd, NULL, NULL);
		if (control_fd < 0) {
			LOG_ERR("accept() of the control connection failed (%d)", errno);
			close_client(video_fd, audio_fd, -1, -1);
			continue;
		}
		set_keepalive(control_fd);

		if (IS_ENABLED(CONFIG_ANDROID_LINK_MIC)) {
			mic_fd = zsock_accept(server_fd, NULL, NULL);
			if (mic_fd < 0) {
				LOG_ERR("accept() of the microphone connection failed (%d)", errno);
				close_client(video_fd, audio_fd, control_fd, -1);
				continue;
			}
			set_keepalive(mic_fd);
		}

		/*
		 * Whatever was posted while no client was served is about the
		 * past: a station-left event for a phone that is back, or a
		 * late message of the previous run. From here on every event is
		 * this client's.
		 */
		k_event_clear(&application_event, EVENT_TOUCH_ERROR | EVENT_STREAM_END);

		ret = hand_socket(VIDEO_PLAYER, &VIDEO_SRC->object, MPIPE_PROP_SOCKET_SRC_FD,
				  video_fd);
#if defined(CONFIG_ANDROID_LINK_AUDIO)
		if (ret == 0) {
			ret = hand_socket(AUDIO_PLAYER, &AUDIO_SRC->object,
					  MPIPE_PROP_SOCKET_SRC_FD, audio_fd);
		}
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
		if (ret == 0) {
			ret = hand_socket(MIC_PLAYER, &MIC_SINK->object, MPIPE_PROP_SOCKET_SINK_FD,
					  mic_fd);
		}
#endif
		if (ret < 0) {
			LOG_ERR("Failed to hand the sockets to the pipelines (%d)", ret);
			close_client(video_fd, audio_fd, control_fd, mic_fd);
			continue;
		}

		(void)mpipe_player_play(VIDEO_PLAYER);
#if defined(CONFIG_ANDROID_LINK_AUDIO)
		(void)mpipe_player_play(AUDIO_PLAYER);
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
		(void)mpipe_player_play(MIC_PLAYER);
#endif
		connect_control_socket(control_fd, &client_addr);
		osd_show("Phone connected", 2000);
#if defined(CONFIG_ANDROID_LINK_MIC)
		demo_mode_announce();
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
		stop_player(VIDEO_PLAYER);
		wait_for_state(VIDEO_PLAYER, MPIPE_STATE_READY);
#if defined(CONFIG_ANDROID_LINK_AUDIO)
		stop_player(AUDIO_PLAYER);
		wait_for_state(AUDIO_PLAYER, MPIPE_STATE_READY);
#endif
#if defined(CONFIG_ANDROID_LINK_MIC)
		stop_player(MIC_PLAYER);
		wait_for_state(MIC_PLAYER, MPIPE_STATE_READY);
#endif
		k_event_clear(&application_event, EVENT_TOUCH_ERROR | EVENT_STREAM_END);
	}

	return 0;
}
