/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The media of the call on the Multimedia Pipeline subsystem, one pipeline
 * per leg:
 *
 *   camera     vid_src -> caps_filter -> vid_convert -> vid_h264_encoder -> app_sink
 *   display    app_src -> vid_h264_decoder -> vid_convert -> vid_overlay -> disp_sink
 *
 * On a board with a video transform device, the PXP of an i.MX RT for example,
 * the device takes the place of the software conversion in the display leg
 * and also scales and rotates the frames to the size of the panel:
 *
 *   display    app_src -> vid_h264_decoder -> vid_transform -> caps_filter
 *              -> vid_overlay -> disp_sink
 *   microphone aud_dmic_src -> caps_filter -> app_sink
 *   headphones app_src -> aud_raw_parser -> queue -> aud_i2s_codec_sink
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
#include <zephyr/mpipe/aud/mpipe_aud_dmic_src.h>
#include <zephyr/mpipe/aud/mpipe_aud_i2s_codec_sink.h>
#include <zephyr/mpipe/aud/mpipe_aud_raw_parser.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/vid/mpipe_vid_convert.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_decoder.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_encoder.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>
#include <zephyr/mpipe/vid/mpipe_vid_transform.h>
#include <zephyr/video/controls.h>

#include "media.h"
#include "media_stats_zephyr.h"

LOG_MODULE_REGISTER(media_mpipe, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within a pipeline matters) */
enum {
	CAMERA_PIPE_ID,
	DISPLAY_PIPE_ID,
	MICROPHONE_PIPE_ID,
	HEADPHONES_PIPE_ID,
	SRC_ID,
	CAPS_FILTER_ID,
	CONVERT_ID,
	TRANSFORM_ID,
	DISPLAY_CAPS_ID,
	CODEC_ID,
	PARSER_ID,
	QUEUE_ID,
	PROBE_ID,
	SINK_ID,
};

#define VIDEO_WIDTH  CONFIG_ZEPHYRTC_VIDEO_WIDTH
#define VIDEO_HEIGHT CONFIG_ZEPHYRTC_VIDEO_HEIGHT

/*
 * With a video transform device the frames of the peer are rotated and scaled
 * on their way to the display.
 */
#define DISPLAY_TRANSFORM DT_HAS_CHOSEN(zephyr_videotrans)
#define DISPLAY_ROTATION  CONFIG_ZEPHYRTC_DISPLAY_ROTATION
#define DISPLAY_SCALE     CONFIG_ZEPHYRTC_DISPLAY_SCALE
#define DISPLAY_TURNED    (DISPLAY_ROTATION == 90 || DISPLAY_ROTATION == 270)
#define DISPLAY_WIDTH     ((DISPLAY_TURNED ? VIDEO_HEIGHT : VIDEO_WIDTH) * DISPLAY_SCALE)
#define DISPLAY_HEIGHT    ((DISPLAY_TURNED ? VIDEO_WIDTH : VIDEO_HEIGHT) * DISPLAY_SCALE)

BUILD_ASSERT(DISPLAY_ROTATION % 90 == 0, "the frames turn by quarter turns");

/* What G.711 carries: 8 kHz mono, one byte per sample, 20 ms per packet */
#define PCMU_SAMPLE_RATE 8000U
#define PCMU_PACKET_US   20000U
#define PCMU_PACKET_SIZE (PCMU_SAMPLE_RATE * PCMU_PACKET_US / USEC_PER_SEC)
#define PCMU_PACKET_NS   ((uint64_t)PCMU_PACKET_US * NSEC_PER_USEC)

/*
 * The microphone runs at twice the G.711 rate and delivers 32-bit samples on
 * two channels. The codec takes 16-bit samples on two channels, at a multiple
 * of the G.711 rate that depends on the codec.
 */
#define MICROPHONE_SAMPLE_RATE (2U * PCMU_SAMPLE_RATE)
#define MICROPHONE_BIT_WIDTH   32U
#define MICROPHONE_BLOCK_SIZE  (PCMU_PACKET_SIZE * 2U * AUDIO_CHANNELS * sizeof(int32_t))
#define AUDIO_CHANNELS         2U
#define HEADPHONES_SAMPLE_RATE CONFIG_ZEPHYRTC_HEADPHONES_SAMPLE_RATE
#define HEADPHONES_UPSAMPLING  (HEADPHONES_SAMPLE_RATE / PCMU_SAMPLE_RATE)
#define HEADPHONES_BIT_WIDTH   16U
#define HEADPHONES_BLOCK_SIZE                                                                      \
	(PCMU_PACKET_SIZE * HEADPHONES_UPSAMPLING * AUDIO_CHANNELS * sizeof(int16_t))
/* 100 ms of received audio wait for the codec at most: older blocks are dropped */
#define HEADPHONES_QUEUE_BLOCKS 5U

/*
 * Audio first: a late block is heard. The decoder next, since a decoded frame
 * is short work; the encoder last, since it runs for most of a frame period
 * and everything else must be able to interrupt it.
 */
#define AUDIO_THREAD_PRIORITY   4
#define DISPLAY_THREAD_PRIORITY 6
#define CAMERA_THREAD_PRIORITY  8

BUILD_ASSERT(HEADPHONES_SAMPLE_RATE % PCMU_SAMPLE_RATE == 0,
	     "the codec must run at a multiple of the G.711 rate");
BUILD_ASSERT(HEADPHONES_BLOCK_SIZE <= CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ,
	     "the application source must hold one block of decoded audio");

struct Media {
	struct mpipe camera_pipe;
	struct mpipe_vid_src camera;
	struct mpipe_caps_filter camera_caps;
	struct mpipe_vid_convert to_yuv;
	struct mpipe_vid_h264_encoder encoder;
	struct mpipe_app_sink encoded_sink;

	struct mpipe display_pipe;
	struct mpipe_app_src encoded_src;
	struct mpipe_vid_h264_decoder decoder;
#if DISPLAY_TRANSFORM
	struct mpipe_vid_transform transform;
	struct mpipe_caps_filter display_caps;
#else
	struct mpipe_vid_convert to_rgb;
#endif
	struct mpipe_vid_overlay probe;
	struct mpipe_disp_sink display;

	struct mpipe microphone_pipe;
	struct mpipe_aud_dmic_src microphone;
	struct mpipe_caps_filter microphone_caps;
	struct mpipe_app_sink captured_sink;

	struct mpipe headphones_pipe;
	struct mpipe_app_src received_src;
	struct mpipe_aud_raw_parser parser;
	struct mpipe_queue queue;
	struct mpipe_aud_i2s_codec_sink headphones;

	/* The encoded frame the application borrowed with media_pull() */
	struct net_buf *borrowed_video;
	uint8_t pcmu[PCMU_PACKET_SIZE];

	/* Uptime of the running time 0 of the camera pipeline, in microseconds */
	uint64_t camera_base_us;
	/* Uptime at which the first block of the microphone was captured, in microseconds */
	uint64_t microphone_start_us;

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

/* The audio drivers move the blocks of these slabs by DMA: they must not be cached */
static __nocache struct k_mem_slab microphone_slab;
static __nocache struct k_mem_slab headphones_slab;

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

/* The decoder lost its place in the stream: the peer must send a key frame */
static void media_key_frame_cb(struct mpipe_vid_h264_decoder *dec, void *user_data)
{
	struct Media *media = user_data;

	ARG_UNUSED(dec);

	atomic_set(&media->keyframe_request, 1);
}

/* Runs for every frame on its way to the display */
static void media_displayed_cb(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			       const struct net_buf *in, void *user_data)
{
	struct Media *media = user_data;

	ARG_UNUSED(frame);
	ARG_UNUSED(width);
	ARG_UNUSED(height);
	ARG_UNUSED(pixfmt);
	ARG_UNUSED(in);

	atomic_inc(&media->displayed_frames);
}

static int media_video_caps(struct mpipe_structure *caps, uint32_t pixfmt)
{
	return mpipe_structure_init_fields(caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					   MPIPE_TYPE_UINT, pixfmt, MPIPE_CAPS_IMAGE_WIDTH,
					   MPIPE_TYPE_UINT, VIDEO_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT,
					   MPIPE_TYPE_UINT, VIDEO_HEIGHT, MPIPE_CAPS_END);
}

static int media_microphone_caps(struct mpipe_structure *caps)
{
	return mpipe_structure_init_fields(
		caps, MPIPE_MEDIA_AUDIO_PCM, MPIPE_CAPS_SAMPLE_RATE, MPIPE_TYPE_UINT,
		MICROPHONE_SAMPLE_RATE, MPIPE_CAPS_BIT_WIDTH, MPIPE_TYPE_UINT, MICROPHONE_BIT_WIDTH,
		MPIPE_CAPS_NUM_CHANNELS, MPIPE_TYPE_UINT, AUDIO_CHANNELS, MPIPE_CAPS_FRAME_INTERVAL,
		MPIPE_TYPE_UINT, PCMU_PACKET_US, MPIPE_CAPS_END);
}

static int media_pipeline_init(struct mpipe *pipe, uint8_t id, int priority)
{
	int ret = mpipe_pipeline_init(pipe, id);

	if (ret != 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)pipe,
					  MPIPE_PROP_PIPELINE_THREAD_PRIORITY, &priority,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		return ret;
	}

	return zbus_chan_add_obs(mpipe_element_get_bus_chan((struct mpipe_element *)pipe),
				 &media_listener, K_FOREVER);
}

/* vid_src -> caps_filter -> vid_convert -> vid_h264_encoder -> app_sink */
static int media_build_camera(struct Media *media)
{
	static const uint8_t qp = CONFIG_ZEPHYRTC_VIDEO_QP;
	static const uint8_t speed = CONFIG_ZEPHYRTC_VIDEO_SPEED;
	static const uint16_t gop = CONFIG_ZEPHYRTC_VIDEO_GOP;
	struct mpipe_structure caps;
	int ret;

	ret = media_pipeline_init(&media->camera_pipe, CAMERA_PIPE_ID, CAMERA_THREAD_PRIORITY);
	if (ret == 0) {
		ret = mpipe_vid_src_init(&media->camera, SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_caps_filter_init(&media->camera_caps, CAPS_FILTER_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_convert_init(&media->to_yuv, CONVERT_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_h264_encoder_init(&media->encoder, CODEC_ID);
	}
	if (ret == 0) {
		ret = mpipe_app_sink_init(&media->encoded_sink, SINK_ID);
	}

	/* The camera delivers the frame size the encoder memory is reserved for */
	if (ret == 0) {
		ret = mpipe_structure_init_fields(
			&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT,
			VIDEO_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, VIDEO_HEIGHT,
			MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT,
			MPIPE_FRAME_INTERVAL_FROM_FPS(CONFIG_ZEPHYRTC_VIDEO_FPS), MPIPE_CAPS_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->camera_caps,
						  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties(
			(struct mpipe_object *)&media->encoder, MPIPE_PROP_VID_H264_ENCODER_QP, &qp,
			MPIPE_PROP_VID_H264_ENCODER_GOP, &gop, MPIPE_PROP_VID_H264_ENCODER_SPEED,
			&speed, MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = media_video_caps(&caps, VIDEO_PIX_FMT_H264);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->encoded_sink,
						  MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_bin_add((struct mpipe_bin *)&media->camera_pipe,
				    (struct mpipe_element *)&media->camera,
				    (struct mpipe_element *)&media->camera_caps,
				    (struct mpipe_element *)&media->to_yuv,
				    (struct mpipe_element *)&media->encoder,
				    (struct mpipe_element *)&media->encoded_sink, NULL);
	}
	if (ret == 0) {
		ret = mpipe_element_link((struct mpipe_element *)&media->camera,
					 (struct mpipe_element *)&media->camera_caps,
					 (struct mpipe_element *)&media->to_yuv,
					 (struct mpipe_element *)&media->encoder,
					 (struct mpipe_element *)&media->encoded_sink, NULL);
	}

	return ret;
}

/*
 * app_src -> vid_h264_decoder -> vid_convert -> vid_overlay -> disp_sink, or
 * app_src -> vid_h264_decoder -> vid_transform -> caps_filter -> vid_overlay -> disp_sink
 */
static int media_build_display(struct Media *media)
{
	static const enum mpipe_vid_overlay_mode probe_mode = MPIPE_VID_OVERLAY_MODE_IN_PLACE;
	static const int __maybe_unused plane = CONFIG_ZEPHYRTC_DISPLAY_PLANE;
	static const uint16_t x = CONFIG_ZEPHYRTC_DISPLAY_X;
	static const uint16_t y = CONFIG_ZEPHYRTC_DISPLAY_Y;
	const struct mpipe_vid_h264_decoder_cb key_frame_cb = {
		.fn = media_key_frame_cb,
		.user_data = media,
	};
	const struct mpipe_vid_overlay_cb probe_cb = {
		.fn = media_displayed_cb,
		.user_data = media,
	};
	struct mpipe_structure caps;
	int ret;

	ret = media_pipeline_init(&media->display_pipe, DISPLAY_PIPE_ID, DISPLAY_THREAD_PRIORITY);
	if (ret == 0) {
		ret = mpipe_app_src_init(&media->encoded_src, SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_h264_decoder_init(&media->decoder, CODEC_ID);
	}
#if DISPLAY_TRANSFORM
	if (ret == 0) {
		ret = mpipe_vid_transform_init(&media->transform, TRANSFORM_ID);
	}
	if (ret == 0) {
		ret = mpipe_caps_filter_init(&media->display_caps, DISPLAY_CAPS_ID);
	}
	/* clang-format off */
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->transform,
			COND_CODE_0(CONFIG_ZEPHYRTC_DISPLAY_ROTATION, (),
				    (VIDEO_CID_ROTATE, CONFIG_ZEPHYRTC_DISPLAY_ROTATION,))
			MPIPE_PROP_LIST_END);
	}
	/* clang-format on */

	/* The size on the panel is what tells the device to scale */
	if (ret == 0) {
		ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_IMAGE_WIDTH,
						  MPIPE_TYPE_UINT, DISPLAY_WIDTH,
						  MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT,
						  DISPLAY_HEIGHT, MPIPE_CAPS_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->display_caps,
						  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
#else
	if (ret == 0) {
		ret = mpipe_vid_convert_init(&media->to_rgb, CONVERT_ID);
	}
#endif
	if (ret == 0) {
		ret = mpipe_vid_overlay_init(&media->probe, PROBE_ID);
	}
	if (ret == 0) {
		ret = mpipe_disp_sink_init(&media->display, SINK_ID);
	}

	/* The frame size the two peers agreed on, which the stream itself does not announce */
	if (ret == 0) {
		ret = media_video_caps(&caps, VIDEO_PIX_FMT_H264);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->encoded_src,
						  MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->decoder,
						  MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB,
						  &key_frame_cb, MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties(
			(struct mpipe_object *)&media->probe, MPIPE_PROP_VID_OVERLAY_DRAW_CB,
			&probe_cb, MPIPE_PROP_VID_OVERLAY_MODE, &probe_mode, MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->display,
						  MPIPE_PROP_DISP_SINK_X, &x,
						  MPIPE_PROP_DISP_SINK_Y, &y, MPIPE_PROP_LIST_END);
	}
	if (ret == 0 && CONFIG_ZEPHYRTC_DISPLAY_PLANE >= 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->display,
						  MPIPE_PROP_DISP_SINK_PLANE, &plane,
						  MPIPE_PROP_LIST_END);
	}
	/* clang-format off */
	if (ret == 0) {
		ret = mpipe_bin_add((struct mpipe_bin *)&media->display_pipe,
				    (struct mpipe_element *)&media->encoded_src,
				    (struct mpipe_element *)&media->decoder,
				    COND_CODE_1(DISPLAY_TRANSFORM,
						((struct mpipe_element *)&media->transform,
						 (struct mpipe_element *)&media->display_caps,),
						((struct mpipe_element *)&media->to_rgb,))
				    (struct mpipe_element *)&media->probe,
				    (struct mpipe_element *)&media->display, NULL);
	}
	if (ret == 0) {
		ret = mpipe_element_link((struct mpipe_element *)&media->encoded_src,
					 (struct mpipe_element *)&media->decoder,
					 COND_CODE_1(DISPLAY_TRANSFORM,
						((struct mpipe_element *)&media->transform,
						 (struct mpipe_element *)&media->display_caps,),
						((struct mpipe_element *)&media->to_rgb,))
					 (struct mpipe_element *)&media->probe,
					 (struct mpipe_element *)&media->display, NULL);
	}
	/* clang-format on */

	return ret;
}

/* aud_dmic_src -> caps_filter -> app_sink */
static int media_build_microphone(struct Media *media)
{
	struct mpipe_structure caps;
	int ret;

	ret = media_pipeline_init(&media->microphone_pipe, MICROPHONE_PIPE_ID,
				  AUDIO_THREAD_PRIORITY);
	if (ret == 0) {
		ret = mpipe_aud_dmic_src_init(&media->microphone, SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_caps_filter_init(&media->microphone_caps, CAPS_FILTER_ID);
	}
	if (ret == 0) {
		ret = mpipe_app_sink_init(&media->captured_sink, SINK_ID);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->microphone,
						  MPIPE_PROP_AUD_SRC_SLAB_PTR, &microphone_slab,
						  MPIPE_PROP_LIST_END);
	}

	/* One block of the microphone makes one G.711 packet */
	if (ret == 0) {
		ret = media_microphone_caps(&caps);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->microphone_caps,
						  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_bin_add((struct mpipe_bin *)&media->microphone_pipe,
				    (struct mpipe_element *)&media->microphone,
				    (struct mpipe_element *)&media->microphone_caps,
				    (struct mpipe_element *)&media->captured_sink, NULL);
	}
	if (ret == 0) {
		ret = mpipe_element_link((struct mpipe_element *)&media->microphone,
					 (struct mpipe_element *)&media->microphone_caps,
					 (struct mpipe_element *)&media->captured_sink, NULL);
	}

	return ret;
}

/* app_src -> aud_raw_parser -> queue -> aud_i2s_codec_sink */
static int media_build_headphones(struct Media *media)
{
	static const uint32_t sample_rate = HEADPHONES_SAMPLE_RATE;
	static const uint32_t bit_width = HEADPHONES_BIT_WIDTH;
	static const uint32_t channels = AUDIO_CHANNELS;
	static const uint32_t block_us = PCMU_PACKET_US;
	static const uint8_t queue_size = HEADPHONES_QUEUE_BLOCKS;
	static const enum mpipe_base_queue_leak leak = MPIPE_BASE_QUEUE_LEAK_OLDEST;
	static const int priority = AUDIO_THREAD_PRIORITY;
	int ret;

	ret = media_pipeline_init(&media->headphones_pipe, HEADPHONES_PIPE_ID,
				  AUDIO_THREAD_PRIORITY);
	if (ret == 0) {
		ret = mpipe_app_src_init(&media->received_src, SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_aud_raw_parser_init(&media->parser, PARSER_ID);
	}
	if (ret == 0) {
		ret = mpipe_queue_init(&media->queue, QUEUE_ID);
	}
	if (ret == 0) {
		ret = mpipe_aud_i2s_codec_sink_init(&media->headphones, SINK_ID);
	}

	/* The parser announces the format and cuts the bytes into the blocks of the I2S driver */
	if (ret == 0) {
		ret = mpipe_object_set_properties(
			(struct mpipe_object *)&media->parser, MPIPE_PROP_AUD_RAW_PARSER_SLAB_PTR,
			&headphones_slab, MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE, &sample_rate,
			MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH, &bit_width,
			MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS, &channels,
			MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL, &block_us, MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties(
			(struct mpipe_object *)&media->queue, MPIPE_PROP_BASE_QUEUE_SIZE,
			&queue_size, MPIPE_PROP_BASE_QUEUE_LEAK, &leak,
			MPIPE_PROP_BASE_QUEUE_THREAD_PRIORITY, &priority, MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&media->headphones,
						  MPIPE_PROP_AUD_SINK_SLAB_PTR, &headphones_slab,
						  MPIPE_PROP_LIST_END);
	}
	if (ret == 0) {
		ret = mpipe_bin_add((struct mpipe_bin *)&media->headphones_pipe,
				    (struct mpipe_element *)&media->received_src,
				    (struct mpipe_element *)&media->parser,
				    (struct mpipe_element *)&media->queue,
				    (struct mpipe_element *)&media->headphones, NULL);
	}
	if (ret == 0) {
		ret = mpipe_element_link((struct mpipe_element *)&media->received_src,
					 (struct mpipe_element *)&media->parser,
					 (struct mpipe_element *)&media->queue,
					 (struct mpipe_element *)&media->headphones, NULL);
	}

	return ret;
}

Media *media_create(const MediaConfig *config)
{
	struct Media *media = &media_instance;
	int ret;

	if (config == NULL) {
		return NULL;
	}

	memset(media, 0, sizeof(*media));
	media->video = !config->audio_only;

	ret = media_build_microphone(media);
	if (ret == 0) {
		ret = media_build_headphones(media);
	}
	if (ret == 0 && media->video) {
		ret = media_build_camera(media);
	}
	if (ret == 0 && media->video) {
		ret = media_build_display(media);
	}
	if (ret != 0) {
		LOG_ERR("Failed to build the pipelines (%d)", ret);
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
	struct mpipe *pipes[] = {&media->headphones_pipe, &media->microphone_pipe,
				 &media->display_pipe, &media->camera_pipe};
	int ret;

	if (media == NULL || media->started) {
		return -EINVAL;
	}

	/* The receiving legs first: the peer is sending already */
	ARRAY_FOR_EACH(pipes, i) {
		if (!media->video &&
		    (pipes[i] == &media->display_pipe || pipes[i] == &media->camera_pipe)) {
			continue;
		}

		ret = mpipe_element_set_state((struct mpipe_element *)pipes[i],
					      MPIPE_STATE_PLAYING);
		if (ret != 0) {
			LOG_ERR("Pipeline %zu failed to start (%d)", i, ret);
			return ret;
		}
	}

	if (media->video) {
		media->camera_base_us = media_base_us(&media->camera_pipe);
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
	    mpipe_app_sink_pull(&media->encoded_sink, &buf, K_NO_WAIT) != 0) {
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

	if (mpipe_app_sink_pull(&media->captured_sink, &buf, K_NO_WAIT) != 0) {
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
			media_base_us(&media->microphone_pipe) + mpipe_buffer_get_meta(buf)->pts;
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

	if (mpipe_app_src_alloc(&media->received_src, HEADPHONES_BLOCK_SIZE, K_NO_WAIT, &buf) !=
	    0) {
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

	if (mpipe_app_src_push_buf(&media->received_src, buf, HEADPHONES_BLOCK_SIZE, K_NO_WAIT) !=
	    0) {
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

	if (mpipe_app_src_push(&media->encoded_src, frame->data, (uint32_t)frame->size,
			       K_NO_WAIT) != 0) {
		/* The frames after this one refer to it: the peer must start again */
		atomic_inc(&media->dropped_video);
		atomic_set(&media->keyframe_request, 1);
	}

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
		(void)mpipe_object_set_properties((struct mpipe_object *)&media->encoder,
						  MPIPE_PROP_VID_H264_ENCODER_KEY_FRAME, &request,
						  MPIPE_PROP_LIST_END);
	}
}

bool media_take_keyframe_request(Media *media)
{
	return media != NULL && atomic_cas(&media->keyframe_request, 1, 0);
}

uint64_t media_decoded(Media *media, MediaKind kind)
{
	if (media == NULL) {
		return 0U;
	}

	return (uint64_t)atomic_get((kind == MEDIA_VIDEO) ? &media->displayed_frames
							  : &media->played_packets);
}

int media_destroy(Media *media)
{
	struct mpipe *pipes[] = {&media->camera_pipe, &media->microphone_pipe, &media->display_pipe,
				 &media->headphones_pipe};
	int result = 0;

	if (media == NULL) {
		return -EINVAL;
	}

	media_release(media, MEDIA_VIDEO);

	ARRAY_FOR_EACH(pipes, i) {
		struct mpipe_element *pipe = (struct mpipe_element *)pipes[i];
		int ret;

		if (!media->video &&
		    (pipes[i] == &media->display_pipe || pipes[i] == &media->camera_pipe)) {
			continue;
		}

		ret = media->started ? mpipe_element_set_state(pipe, MPIPE_STATE_READY) : 0;
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
