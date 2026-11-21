/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A call of the board with itself: the camera and the microphone go through
 * the encoders, a first endpoint, the loopback of the IP stack, a second
 * endpoint and the decoders to the display and the headphones. Everything a
 * call with a remote peer runs is there, signaling and the network driver
 * excepted, which makes it the way to see and to measure the media of the
 * call on a board that has no network.
 */

#include "libpeer_self_call.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <peer_connection.h>

#include "libpeer_endpoint.h"
#include "media.h"
#include "platform.h"

#define SELF_CALL_SDP_SIZE  8096U
#define SELF_CALL_REPORT_MS 1000U

static struct libpeer_endpoint caller;
static struct libpeer_endpoint callee;
static char sdp[SELF_CALL_SDP_SIZE];

K_THREAD_STACK_DEFINE(caller_stack, ZEPHYRTC_ENDPOINT_STACK_SIZE);
K_THREAD_STACK_DEFINE(callee_stack, ZEPHYRTC_ENDPOINT_STACK_SIZE);

static void audio_received(const uint8_t *data, size_t size, uint32_t rtp_timestamp,
			   void *user_data)
{
	MediaFrame frame = {
		.data = data,
		.size = size,
		.timestamp_ns = (uint64_t)rtp_timestamp * NSEC_PER_SEC / 8000U,
	};

	(void)media_receive(user_data, MEDIA_AUDIO, &frame);
}

static void video_received(const uint8_t *data, size_t size, uint32_t rtp_timestamp,
			   void *user_data)
{
	MediaFrame frame = {
		.data = data,
		.size = size,
		.timestamp_ns = (uint64_t)rtp_timestamp * NSEC_PER_SEC / 90000U,
	};

	(void)media_receive(user_data, MEDIA_VIDEO, &frame);
}

/* The callee asks for a key frame: the caller's encoder is in the same media */
static void key_frame_requested(void *user_data)
{
	media_request_keyframe(user_data);
}

static int keep_sdp(const char *description)
{
	size_t length;

	if (description == NULL) {
		return -EINVAL;
	}

	length = strlen(description);
	if (length >= sizeof(sdp)) {
		return -E2BIG;
	}

	memcpy(sdp, description, length + 1U);

	return 0;
}

/* Offer from the caller to the callee, answer back */
static int negotiate(void)
{
	int ret = keep_sdp(peer_connection_create_offer(caller.connection));

	if (ret != 0) {
		return ret;
	}
	peer_connection_set_remote_description(callee.connection, sdp, SDP_TYPE_OFFER);

	ret = keep_sdp(peer_connection_create_answer(callee.connection));
	if (ret != 0) {
		return ret;
	}
	peer_connection_set_remote_description(caller.connection, sdp, SDP_TYPE_ANSWER);

	return (libpeer_endpoint_failed(&caller) || libpeer_endpoint_failed(&callee)) ? -EPROTO : 0;
}

/* Share of the last period the processor spent out of the idle thread, in percent */
static uint32_t cpu_load(void)
{
#if defined(CONFIG_SCHED_THREAD_USAGE_ALL)
	static uint64_t last_total;
	static uint64_t last_idle;
	struct k_thread_runtime_stats stats;
	uint64_t total;
	uint64_t idle;

	if (k_thread_runtime_stats_all_get(&stats) != 0) {
		return 0U;
	}

	total = stats.total_cycles - last_total;
	idle = stats.idle_cycles - last_idle;
	last_total = stats.total_cycles;
	last_idle = stats.idle_cycles;

	return (total != 0U) ? (uint32_t)(((total - idle) * 100U) / total) : 0U;
#else
	return 0U;
#endif
}

/* Move what the media captured to the caller until the call is over */
static int stream(Media *media)
{
	uint64_t deadline = platform_now_ms() + CONFIG_ZEPHYRTC_SELF_CALL_RUN_MS;
	uint64_t next_report = platform_now_ms() + SELF_CALL_REPORT_MS;
	uint64_t sent[2] = {0};
	uint64_t shown = 0;

	while (platform_now_ms() < deadline) {
		bool idle = true;

		if (media_poll(media) != 0 || libpeer_endpoint_failed(&caller) ||
		    libpeer_endpoint_failed(&callee)) {
			return -EIO;
		}

		/* The decoder lost a frame: the encoder is asked directly */
		if (media_take_keyframe_request(media)) {
			media_request_keyframe(media);
		}

		for (int kind = MEDIA_AUDIO; kind <= MEDIA_VIDEO; kind++) {
			MediaFrame frame;
			int ret;

			if (!media_enabled(media, (MediaKind)kind) ||
			    !media_pull(media, (MediaKind)kind, &frame)) {
				continue;
			}

			ret = (kind == MEDIA_AUDIO) ? libpeer_endpoint_send_audio(
							      &caller, frame.data, frame.size,
							      frame.timestamp_ns / NSEC_PER_USEC)
						    : libpeer_endpoint_send_video(
							      &caller, frame.data, frame.size,
							      frame.timestamp_ns / NSEC_PER_USEC);
			media_release(media, (MediaKind)kind);
			if (ret != 0) {
				platform_log("error", "self_call_send", "kind=%d result=%d", kind,
					     ret);
				return ret;
			}

			sent[kind]++;
			idle = false;
		}

		if (platform_now_ms() >= next_report) {
			uint64_t now_shown = media_decoded(media, MEDIA_VIDEO);

			platform_log("info", "self_call",
				     "video_sent=%llu video_shown=%llu fps=%llu audio_sent=%llu "
				     "audio_played=%llu cpu=%u",
				     sent[MEDIA_VIDEO], now_shown, now_shown - shown,
				     sent[MEDIA_AUDIO], media_decoded(media, MEDIA_AUDIO),
				     cpu_load());
			shown = now_shown;
			next_report += SELF_CALL_REPORT_MS;
		}

		if (idle) {
			platform_sleep_ms(1U);
		}
	}

	return (sent[MEDIA_AUDIO] > 0U && media_decoded(media, MEDIA_AUDIO) > 0U &&
		(!media_enabled(media, MEDIA_VIDEO) || media_decoded(media, MEDIA_VIDEO) > 0U))
		       ? 0
		       : -ENODATA;
}

int libpeer_self_call_run(void)
{
	const MediaConfig media_config = {
		.audio_only = !IS_ENABLED(CONFIG_ZEPHYRTC_VIDEO_H264),
	};
	const bool video = IS_ENABLED(CONFIG_ZEPHYRTC_VIDEO_H264);
	bool stack_initialized = false;
	Media *media = NULL;
	int ret;

	memset(&caller, 0, sizeof(caller));
	memset(&callee, 0, sizeof(callee));

	ret = libpeer_stack_init();
	if (ret != 0) {
		goto cleanup;
	}
	stack_initialized = true;

	ret = libpeer_endpoint_init(&caller, "caller", 0U, 0U, false, video);
	if (ret == 0) {
		ret = libpeer_endpoint_init(&callee, "callee", 0U, 0U, false, video);
	}
	if (ret != 0) {
		goto cleanup;
	}

	media = media_create(&media_config);
	if (media == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = libpeer_endpoint_set_audio_sink(&callee, audio_received, media);
	if (ret == 0 && video) {
		ret = libpeer_endpoint_set_video_sink(&callee, video_received, key_frame_requested,
						      media);
	}
	if (ret == 0) {
		ret = negotiate();
	}
	if (ret != 0) {
		goto cleanup;
	}

	(void)libpeer_endpoint_start(&caller, caller_stack, K_THREAD_STACK_SIZEOF(caller_stack));
	(void)libpeer_endpoint_start(&callee, callee_stack, K_THREAD_STACK_SIZEOF(callee_stack));

	ret = libpeer_pair_wait_connected(&caller, &callee);
	if (ret == 0) {
		ret = media_start(media);
	}
	if (ret == 0) {
		ret = stream(media);
	}

cleanup:
	libpeer_endpoint_stop(&caller);
	libpeer_endpoint_stop(&callee);
	platform_log((ret == 0) ? "info" : "error", "self_call_result",
		     "status=%s result=%d audio_played=%llu video_shown=%llu",
		     (ret == 0) ? "pass" : "fail", ret,
		     (media != NULL) ? media_decoded(media, MEDIA_AUDIO) : 0U,
		     (media != NULL) ? media_decoded(media, MEDIA_VIDEO) : 0U);
	libpeer_endpoint_destroy(&caller);
	libpeer_endpoint_destroy(&callee);
	if (media != NULL) {
		(void)media_destroy(media);
	}
	if (stack_initialized) {
		libpeer_stack_deinit();
	}

	return ret;
}
