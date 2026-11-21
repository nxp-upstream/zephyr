/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The media of the call on the Multimedia Pipeline subsystem. The devicetree
 * describes the four pipelines, one per leg (pipelines.dtsi and the overlay of
 * the board):
 *
 *   camera     vid_src -> caps_filter -> vid_convert -> vid_h264_encoder -> app_sink
 *   display    app_src -> caps_filter -> vid_h264_decoder -> ... -> disp_sink
 *   microphone aud_dmic_src -> caps_filter -> aud_gain -> app_sink
 *   headphones app_src -> aud_raw_parser -> aud_gain -> queue -> aud_i2s_codec_sink
 *
 * The application is the peer of the four application elements: it takes the
 * encoded frames and the microphone blocks from the sinks and gives the
 * received ones to the sources. G.711 is too small a codec for an element of
 * its own: the microphone blocks are encoded, and the received packets
 * decoded, on the way through.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#if !defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
#include <zephyr/mpipe/vid/mpipe_vid_h264_decoder.h>
#endif
#include <zephyr/mpipe/vid/mpipe_vid_h264_encoder.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/video/controls.h>

#include "media.h"
#include "media_stats_zephyr.h"

LOG_MODULE_REGISTER(media_mpipe, CONFIG_LOG_DEFAULT_LEVEL);

/* The pipelines and the elements of the devicetree the application talks to */
#define CAMERA_PIPE     MPIPE_DT_PIPELINE_GET(DT_NODELABEL(camera_pipeline))
#define DISPLAY_PIPE    MPIPE_DT_PIPELINE_GET(DT_NODELABEL(display_pipeline))
#define MICROPHONE_PIPE MPIPE_DT_PIPELINE_GET(DT_NODELABEL(microphone_pipeline))
#define HEADPHONES_PIPE MPIPE_DT_PIPELINE_GET(DT_NODELABEL(headphones_pipeline))
#define CAMERA          MPIPE_DT_ELEMENT_GET(DT_NODELABEL(camera))
#define ENCODER         MPIPE_DT_ELEMENT_GET(DT_NODELABEL(encoder))
#define DECODER         MPIPE_DT_ELEMENT_GET(DT_NODELABEL(decoder))
#define ENCODED_SINK    ((struct mpipe_app_sink *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(encoded_sink)))
#define ENCODED_SRC     ((struct mpipe_app_src *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(encoded_src)))
#define CAPTURED_SINK   ((struct mpipe_app_sink *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(captured_sink)))
#define RECEIVED_SRC    ((struct mpipe_app_src *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(received_src)))

#if defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
/*
 * The decoder of the second core cannot ask for a key frame: this many pieces
 * of the stream that gave no picture mean that it lost its place.
 */
#define DECODER_STALLED_SLICES 6
#endif

/*
 * Pieces of the stream between media_receive() and the display at most: what
 * the pool of the application sources holds, and the pictures after the decoder.
 */
#define RECEIVED_FRAMES (CONFIG_MPIPE_BASE_APP_SRC_POOL_NUM + 8)

/* What G.711 carries: 8 kHz mono, one byte per sample, 20 ms per packet */
#define PCMU_SAMPLE_RATE 8000U
#define PCMU_PACKET_US   20000U
#define PCMU_PACKET_SIZE (PCMU_SAMPLE_RATE * PCMU_PACKET_US / USEC_PER_SEC)
#define PCMU_PACKET_NS   ((uint64_t)PCMU_PACKET_US * NSEC_PER_USEC)

/*
 * The formats the devicetree gives the two audio pipelines. The G.711 steps
 * below take the microphone at twice the G.711 rate in 32-bit samples on two
 * channels, and give the codec 16-bit samples on two channels at a multiple of
 * the G.711 rate.
 */
#define MICROPHONE_FORMAT      DT_NODELABEL(microphone_filter)
#define HEADPHONES_FORMAT      DT_NODELABEL(headphones_parser)
#define AUDIO_CHANNELS         2U
#define MICROPHONE_BLOCK_SIZE  (PCMU_PACKET_SIZE * 2U * AUDIO_CHANNELS * sizeof(int32_t))
#define HEADPHONES_SAMPLE_RATE DT_PROP(HEADPHONES_FORMAT, sample_rate)
#define HEADPHONES_UPSAMPLING  (HEADPHONES_SAMPLE_RATE / PCMU_SAMPLE_RATE)
#define HEADPHONES_BLOCK_SIZE                                                                      \
	(PCMU_PACKET_SIZE * HEADPHONES_UPSAMPLING * AUDIO_CHANNELS * sizeof(int16_t))

BUILD_ASSERT(DT_PROP(MICROPHONE_FORMAT, sample_rate) == 2U * PCMU_SAMPLE_RATE &&
		     DT_PROP(MICROPHONE_FORMAT, bit_width) == 32 &&
		     DT_PROP(MICROPHONE_FORMAT, num_channels) == AUDIO_CHANNELS &&
		     DT_PROP(MICROPHONE_FORMAT, frame_interval) == PCMU_PACKET_US,
	     "the microphone blocks are not what the G.711 encoding takes");
BUILD_ASSERT(HEADPHONES_SAMPLE_RATE % PCMU_SAMPLE_RATE == 0 &&
		     DT_PROP(HEADPHONES_FORMAT, bit_width) == 16 &&
		     DT_PROP(HEADPHONES_FORMAT, num_channels) == AUDIO_CHANNELS &&
		     DT_PROP(HEADPHONES_FORMAT, frame_interval) == PCMU_PACKET_US,
	     "the headphones blocks are not what the G.711 decoding gives");
BUILD_ASSERT(HEADPHONES_BLOCK_SIZE <= CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ,
	     "the application source must hold one block of decoded audio");

struct Media {
#if defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
	/* Slices given to the second core, and how many of them gave no picture last time */
	atomic_t received_slices;
	atomic_val_t missing_slices;
#endif

	/* The encoded frame the application borrowed with media_pull() */
	struct net_buf *borrowed_video;
	uint8_t pcmu[PCMU_PACKET_SIZE];

	/* Uptime of the running time 0 of the camera pipeline, in microseconds */
	uint64_t camera_base_us;
	/* Uptime at which the first block of the microphone was captured, in microseconds */
	uint64_t microphone_start_us;

	/*
	 * Running time of the display pipeline at which each piece of the stream
	 * went in, with the timestamp of the peer, and the timestamp of the piece
	 * that made the picture on the display.
	 */
	struct k_spinlock received_lock;
	struct {
		uint64_t pushed_us;
		uint64_t timestamp_ns;
	} received[RECEIVED_FRAMES];
	uint32_t received_count;
	uint64_t displayed_timestamp_ns;

	atomic_t failure;
	atomic_t keyframe_request;
	atomic_t displayed_frames;
	atomic_t dropped_video;
	atomic_t captured_packets;
	atomic_t played_packets;
	atomic_t dropped_audio;
	bool video;
	bool started;
};

static struct Media media_instance;

/*
 * G.711 mu-law: a sign, a 3-bit exponent and a 4-bit mantissa of the 14-bit
 * magnitude plus a bias, all bits inverted on the wire.
 */
#define MULAW_BIAS 0x84
#define MULAW_CLIP 32635

static uint8_t linear_to_mulaw(int16_t sample)
{
	int32_t magnitude = (sample < 0) ? -(int32_t)sample : (int32_t)sample;
	uint8_t sign = (sample < 0) ? 0x80U : 0x00U;
	uint32_t exponent = 7U;

	magnitude = MIN(magnitude, MULAW_CLIP) + MULAW_BIAS;
	while (exponent > 0U && (magnitude & (0x80 << exponent)) == 0) {
		exponent--;
	}

	return (uint8_t)~(sign | (exponent << 4) | ((magnitude >> (exponent + 3U)) & 0x0F));
}

static int16_t mulaw_to_linear(uint8_t code)
{
	uint8_t value = (uint8_t)~code;
	uint32_t exponent = (value >> 4) & 0x07U;
	int32_t magnitude = ((((int32_t)value & 0x0F) << 3) + MULAW_BIAS) << exponent;

	magnitude -= MULAW_BIAS;

	return (int16_t)(((value & 0x80U) != 0U) ? -magnitude : magnitude);
}

/* An error on a pipeline ends the call: note it for media_poll() */
static void media_message_cb(const struct zbus_channel *chan)
{
	const struct mpipe_message *msg = zbus_chan_const_msg(chan);

	if (msg->type == MPIPE_MESSAGE_ERROR) {
		atomic_set(&media_instance.failure, 1);
	}
}

ZBUS_LISTENER_DEFINE(media_listener, media_message_cb);

#if !defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
/* The decoder lost its place in the stream: the peer must send a key frame */
static void media_key_frame_cb(struct mpipe_vid_h264_decoder *dec, void *user_data)
{
	struct Media *media = user_data;

	ARG_UNUSED(dec);

	atomic_set(&media->keyframe_request, 1);
}
#endif

void media_displayed_cb(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			const struct net_buf *in, void *user_data);

/* The draw callback of the probe: runs for every frame on its way to the display */
void media_displayed_cb(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			const struct net_buf *in, void *user_data)
{
	struct Media *media = &media_instance;

	ARG_UNUSED(frame);
	ARG_UNUSED(width);
	ARG_UNUSED(height);
	ARG_UNUSED(pixfmt);
	ARG_UNUSED(user_data);

	/*
	 * The picture has the timestamp of the piece that completed it: the last
	 * one that went in at that time or before. The pieces of one frame may go
	 * in at the same time; the peer gave them the same timestamp.
	 */
	K_SPINLOCK(&media->received_lock) {
		uint64_t pts = mpipe_buffer_get_meta(in)->pts;
		uint64_t pushed_us = 0U;

		media->displayed_timestamp_ns = 0U;
		for (size_t i = 0; i < ARRAY_SIZE(media->received); i++) {
			if (media->received[i].pushed_us <= pts &&
			    media->received[i].pushed_us >= pushed_us) {
				pushed_us = media->received[i].pushed_us;
				media->displayed_timestamp_ns = media->received[i].timestamp_ns;
			}
		}
	}

	atomic_inc(&media->displayed_frames);
}

/* The pipelines of the call, the receiving legs first: the peer is sending already */
static struct mpipe *const media_pipes[] = {HEADPHONES_PIPE, MICROPHONE_PIPE, DISPLAY_PIPE,
					    CAMERA_PIPE};

static bool media_plays(const struct Media *media, const struct mpipe *pipe)
{
	return media->video || (pipe != DISPLAY_PIPE && pipe != CAMERA_PIPE);
}

Media *media_create(const MediaConfig *config)
{
	struct Media *media = &media_instance;
	int ret = 0;

	if (config == NULL) {
		return NULL;
	}

	memset(media, 0, sizeof(*media));
	media->video = !config->audio_only;

	ARRAY_FOR_EACH(media_pipes, i) {
		if (ret == 0) {
			ret = zbus_chan_add_obs(
				mpipe_element_get_bus_chan((struct mpipe_element *)media_pipes[i]),
				&media_listener, K_FOREVER);
		}
	}

#if !defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
	if (ret == 0) {
		const struct mpipe_vid_h264_decoder_cb key_frame_cb = {
			.fn = media_key_frame_cb,
			.user_data = media,
		};

		ret = mpipe_object_set_properties((struct mpipe_object *)DECODER,
						  MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB,
						  &key_frame_cb, MPIPE_PROP_LIST_END);
	}
#endif

	/* What the camera is asked on top of the settings of its driver */
	if (ret == 0 && CONFIG_ZEPHYRTC_CAMERA_SHARPNESS >= 0) {
		ret = mpipe_object_set_properties(
			(struct mpipe_object *)CAMERA, VIDEO_CID_SHARPNESS,
			(const void *)(uintptr_t)CONFIG_ZEPHYRTC_CAMERA_SHARPNESS,
			MPIPE_PROP_LIST_END);
	}
	if (ret == 0 && CONFIG_ZEPHYRTC_CAMERA_EXPOSURE_BIAS >= 0) {
		ret = mpipe_object_set_properties(
			(struct mpipe_object *)CAMERA, VIDEO_CID_AUTO_EXPOSURE_BIAS,
			(const void *)(uintptr_t)CONFIG_ZEPHYRTC_CAMERA_EXPOSURE_BIAS,
			MPIPE_PROP_LIST_END);
	}
	if (ret != 0) {
		LOG_ERR("Failed to set the pipelines up (%d)", ret);
		return NULL;
	}

	return media;
}

bool media_enabled(const Media *media, MediaKind kind)
{
	return media != NULL && (kind == MEDIA_AUDIO || (kind == MEDIA_VIDEO && media->video));
}

/* Uptime at which the running time of a pipeline is 0, in microseconds */
static uint64_t media_base_us(struct mpipe *pipe)
{
	uint64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());

	return now_us - mpipe_pipeline_running_time_at(pipe, now_us);
}

int media_start(Media *media)
{
	int ret;

	if (media == NULL || media->started) {
		return -EINVAL;
	}

	ARRAY_FOR_EACH(media_pipes, i) {
		if (!media_plays(media, media_pipes[i])) {
			continue;
		}

		ret = mpipe_element_set_state((struct mpipe_element *)media_pipes[i],
					      MPIPE_STATE_PLAYING);
		if (ret != 0) {
			LOG_ERR("Pipeline %zu failed to start (%d)", i, ret);
			return ret;
		}
	}

	if (media->video) {
		media->camera_base_us = media_base_us(CAMERA_PIPE);
	}
	media->started = true;

	return 0;
}

int media_poll(Media *media)
{
	return (media != NULL && atomic_get(&media->failure) == 0) ? 0 : -EIO;
}

/* The next encoded frame, lent to the application until media_release() */
static bool media_pull_video(struct Media *media, MediaFrame *frame)
{
	struct net_buf *buf;

	if (media->borrowed_video != NULL ||
	    mpipe_app_sink_pull(ENCODED_SINK, &buf, K_NO_WAIT) != 0) {
		return false;
	}

	media->borrowed_video = buf;
	*frame = (MediaFrame){
		.data = buf->data,
		.size = mpipe_buffer_get_meta(buf)->bytes_used,
		.timestamp_ns =
			(media->camera_base_us + mpipe_buffer_get_meta(buf)->pts) * NSEC_PER_USEC,
	};

	return true;
}

/* The next block of the microphone as a G.711 packet */
static bool media_pull_audio(struct Media *media, MediaFrame *frame)
{
	const int32_t *samples;
	struct net_buf *buf;

	if (mpipe_app_sink_pull(CAPTURED_SINK, &buf, K_NO_WAIT) != 0) {
		return false;
	}

	if (mpipe_buffer_get_meta(buf)->bytes_used != MICROPHONE_BLOCK_SIZE) {
		net_buf_unref(buf);
		return false;
	}

	/* Two frames of two channels make one sample: the mean of the top 16 bits of each */
	samples = (const int32_t *)buf->data;
	for (size_t i = 0; i < PCMU_PACKET_SIZE; i++) {
		int32_t sum = (samples[0] >> 16) + (samples[1] >> 16) + (samples[2] >> 16) +
			      (samples[3] >> 16);

		media->pcmu[i] = linear_to_mulaw((int16_t)(sum / 4));
		samples += 4;
	}

	/*
	 * The microphone delivers its blocks back to back: the capture time
	 * of a block is the one of the first block plus the blocks in between,
	 * whenever the block reached this thread. The peer places the audio by
	 * its timestamps and hears every step that is not one packet long.
	 */
	if (media->microphone_start_us == 0U) {
		media->microphone_start_us =
			media_base_us(MICROPHONE_PIPE) + mpipe_buffer_get_meta(buf)->pts;
	}

	*frame = (MediaFrame){
		.data = media->pcmu,
		.size = sizeof(media->pcmu),
		.timestamp_ns = (media->microphone_start_us * NSEC_PER_USEC) +
				((uint64_t)atomic_get(&media->captured_packets) * PCMU_PACKET_NS),
		.duration_ns = PCMU_PACKET_NS,
	};
	net_buf_unref(buf);
	atomic_inc(&media->captured_packets);

	return true;
}

bool media_pull(Media *media, MediaKind kind, MediaFrame *frame)
{
	if (media == NULL || frame == NULL || !media->started) {
		return false;
	}

	if (kind == MEDIA_VIDEO) {
		return media->video && media_pull_video(media, frame);
	}

	return kind == MEDIA_AUDIO && media_pull_audio(media, frame);
}

void media_release(Media *media, MediaKind kind)
{
	if (media != NULL && kind == MEDIA_VIDEO && media->borrowed_video != NULL) {
		net_buf_unref(media->borrowed_video);
		media->borrowed_video = NULL;
	}
}

/* A G.711 packet to the headphones: at the rate of the codec, on both channels */
static int media_receive_audio(struct Media *media, const MediaFrame *frame)
{
	struct net_buf *buf;
	int16_t *samples;

	if (frame->size != PCMU_PACKET_SIZE) {
		return -EINVAL;
	}

	if (mpipe_app_src_alloc(RECEIVED_SRC, HEADPHONES_BLOCK_SIZE, K_NO_WAIT, &buf) != 0) {
		atomic_inc(&media->dropped_audio);
		return 0;
	}

	/* The samples in between lie on the line from one G.711 sample to the next */
	samples = (int16_t *)buf->data;
	for (size_t i = 0; i < PCMU_PACKET_SIZE; i++) {
		int32_t current = mulaw_to_linear(frame->data[i]);
		int32_t next = (i + 1U < PCMU_PACKET_SIZE) ? mulaw_to_linear(frame->data[i + 1U])
							   : current;

		for (int32_t step = 0; step < (int32_t)HEADPHONES_UPSAMPLING; step++) {
			int16_t sample = (int16_t)(current + (((next - current) * step) /
							      (int32_t)HEADPHONES_UPSAMPLING));

			for (uint32_t channel = 0; channel < AUDIO_CHANNELS; channel++) {
				*samples++ = sample;
			}
		}
	}

	if (mpipe_app_src_push_buf(RECEIVED_SRC, buf, HEADPHONES_BLOCK_SIZE, K_NO_WAIT) != 0) {
		net_buf_unref(buf);
		atomic_inc(&media->dropped_audio);
		return 0;
	}

	atomic_inc(&media->played_packets);

	return 0;
}

/* A piece of the received H.264 stream to the decoder */
static int media_receive_video(struct Media *media, const MediaFrame *frame)
{
	if (frame->size > CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ) {
		return -E2BIG;
	}

	/* Before the push: the picture may be on the display when the push returns */
	K_SPINLOCK(&media->received_lock) {
		media->received[media->received_count % ARRAY_SIZE(media->received)].pushed_us =
			mpipe_element_running_time((struct mpipe_element *)ENCODED_SRC);
		media->received[media->received_count % ARRAY_SIZE(media->received)].timestamp_ns =
			frame->timestamp_ns;
		media->received_count++;
	}

	if (mpipe_app_src_push(ENCODED_SRC, frame->data, (uint32_t)frame->size,
			       K_NO_WAIT) != 0) {
		/* The frames after this one refer to it: the peer must start again */
		atomic_inc(&media->dropped_video);
		atomic_set(&media->keyframe_request, 1);
		return 0;
	}

#if defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
	/* A slice makes a picture: count them against the pictures that come back */
	if (frame->size > 4U &&
	    ((frame->data[4] & 0x1FU) == 1U || (frame->data[4] & 0x1FU) == 5U)) {
		atomic_inc(&media->received_slices);
	}
#endif

	return 0;
}

int media_receive(Media *media, MediaKind kind, const MediaFrame *frame)
{
	if (media == NULL || frame == NULL || frame->data == NULL || frame->size == 0U) {
		return -EINVAL;
	}

	/* What the peer sends before the start is lost: the decoder asks for a key frame */
	if (!media->started) {
		return 0;
	}

	if (kind == MEDIA_VIDEO) {
		return media->video ? media_receive_video(media, frame) : -EINVAL;
	}

	return (kind == MEDIA_AUDIO) ? media_receive_audio(media, frame) : -EINVAL;
}

void media_request_keyframe(Media *media)
{
	static const bool request = true;

	if (media != NULL && media->video) {
		(void)mpipe_object_set_properties((struct mpipe_object *)ENCODER,
						  MPIPE_PROP_VID_H264_ENCODER_KEY_FRAME, &request,
						  MPIPE_PROP_LIST_END);
	}
}

bool media_take_keyframe_request(Media *media)
{
	if (media == NULL) {
		return false;
	}

#if defined(CONFIG_ZEPHYRTC_DECODER_REMOTE)
	/* More slices without a picture than the last time: the decoder waits for a key frame */
	atomic_val_t missing =
		atomic_get(&media->received_slices) - atomic_get(&media->displayed_frames);

	if (missing - media->missing_slices >= DECODER_STALLED_SLICES) {
		media->missing_slices = missing;
		atomic_set(&media->keyframe_request, 1);
	}
#endif

	return atomic_cas(&media->keyframe_request, 1, 0);
}

uint64_t media_decoded(Media *media, MediaKind kind)
{
	if (media == NULL) {
		return 0U;
	}

	return (uint64_t)atomic_get((kind == MEDIA_VIDEO) ? &media->displayed_frames
							  : &media->played_packets);
}

uint64_t media_decoded_timestamp_ns(Media *media, MediaKind kind)
{
	uint64_t timestamp_ns = 0U;

	if (media == NULL || kind != MEDIA_VIDEO) {
		return 0U;
	}

	K_SPINLOCK(&media->received_lock) {
		timestamp_ns = media->displayed_timestamp_ns;
	}

	return timestamp_ns;
}

int media_destroy(Media *media)
{
	int result = 0;

	if (media == NULL) {
		return -EINVAL;
	}

	media_release(media, MEDIA_VIDEO);

	/* The sending legs first */
	for (size_t i = ARRAY_SIZE(media_pipes); i-- > 0U;) {
		struct mpipe_element *pipe = (struct mpipe_element *)media_pipes[i];
		int ret = 0;

		if (media->started && media_plays(media, media_pipes[i])) {
			ret = mpipe_element_set_state(pipe, MPIPE_STATE_READY);
		}
		if (ret != 0 && result == 0) {
			result = ret;
		}
		(void)zbus_chan_rm_obs(mpipe_element_get_bus_chan(pipe), &media_listener,
				       K_FOREVER);
	}

	LOG_INF("captured %ld played %ld dropped %ld audio packets, displayed %ld dropped %ld "
		"video frames",
		atomic_get(&media->captured_packets), atomic_get(&media->played_packets),
		atomic_get(&media->dropped_audio), atomic_get(&media->displayed_frames),
		atomic_get(&media->dropped_video));
	media->started = false;

	return result;
}

uint64_t media_zephyr_generated_audio(void)
{
	return (uint64_t)atomic_get(&media_instance.captured_packets);
}

uint64_t media_zephyr_late_audio(void)
{
	return 0U;
}

int media_zephyr_quiesce(void)
{
	return 0;
}
