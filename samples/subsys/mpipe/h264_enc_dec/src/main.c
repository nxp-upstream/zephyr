/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_convert.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_decoder.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_encoder.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>
#include <zephyr/sys/util_macro.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	VID_SRC_ID,
	CAPS_FILTER_ID,
	PATTERN_ID,
	TO_YUV_ID,
	ENCODER_ID,
	DECODER_ID,
	TO_RGB_ID,
	DISP_SINK_ID,
};

#define PATTERN_BLOCK_SIZE 96U

/* A gradient, and a textured block that moves and changes from frame to frame */
static void __maybe_unused draw_pattern(uint8_t *frame, uint16_t width, uint16_t height,
					uint32_t pixfmt, const struct net_buf *in, void *user_data)
{
	static uint32_t n;
	uint32_t *pixel = (uint32_t *)frame;
	uint32_t block_x = (4U * n) % (width - PATTERN_BLOCK_SIZE);
	uint32_t block_y = (2U * n) % (height - PATTERN_BLOCK_SIZE);

	ARG_UNUSED(in);
	ARG_UNUSED(user_data);

	if (pixfmt != VIDEO_PIX_FMT_BGRX32) {
		return;
	}

	for (uint32_t y = 0U; y < height; y++) {
		for (uint32_t x = 0U; x < width; x++) {
			uint32_t grey = (x + (2U * y)) / 2U;

			if ((x - block_x) < PATTERN_BLOCK_SIZE &&
			    (y - block_y) < PATTERN_BLOCK_SIZE) {
				grey = ((4U * (x - block_x)) ^ (4U * (y - block_y))) + (4U * n);
			}

			*pixel++ = (grey & 0xFFU) * 0x010101U;
		}
	}

	n++;
}

/*
 * vid_src -> caps_filter -> [vid_overlay] -> vid_convert -> vid_h264_encoder
 *   -> vid_h264_decoder -> vid_convert -> disp_sink
 *
 * The camera frame is encoded and decoded again before it is shown, so what
 * the display shows is what a receiver of the stream would see.
 */
int main(void)
{
	static struct mpipe pipe;
	static struct mpipe_vid_src vid_src;
	static struct mpipe_caps_filter caps_filter;
#if defined(CONFIG_H264_TEST_PATTERN)
	static struct mpipe_vid_overlay pattern;
	static const struct mpipe_vid_overlay_cb pattern_cb = {.fn = draw_pattern};
	static const enum mpipe_vid_overlay_mode pattern_mode = MPIPE_VID_OVERLAY_MODE_IN_PLACE;
#endif
	static struct mpipe_vid_convert to_yuv;
	static struct mpipe_vid_h264_encoder encoder;
	static struct mpipe_vid_h264_decoder decoder;
	static struct mpipe_vid_convert to_rgb;
	static struct mpipe_disp_sink disp_sink;
	static struct mpipe_player player;
	static const uint8_t qp = CONFIG_H264_QP;
	static const uint8_t speed = CONFIG_H264_SPEED;
	static const uint16_t gop = CONFIG_H264_GOP;
	static const int __maybe_unused plane = CONFIG_VIDEO_PLANE;
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret == 0) {
		ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	}
#if defined(CONFIG_H264_TEST_PATTERN)
	if (ret == 0) {
		ret = mpipe_vid_overlay_init(&pattern, PATTERN_ID);
	}
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&pattern,
						  MPIPE_PROP_VID_OVERLAY_DRAW_CB, &pattern_cb,
						  MPIPE_PROP_VID_OVERLAY_MODE, &pattern_mode,
						  MPIPE_PROP_LIST_END);
	}
#endif
	if (ret == 0) {
		ret = mpipe_vid_convert_init(&to_yuv, TO_YUV_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_h264_encoder_init(&encoder, ENCODER_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_h264_decoder_init(&decoder, DECODER_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_convert_init(&to_rgb, TO_RGB_ID);
	}
	if (ret == 0) {
		ret = mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
	}
	if (ret != 0) {
		LOG_ERR("Failed to initialize the elements (%d)", ret);
		return 0;
	}

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_src,
		COND_CODE_0(CONFIG_PROP_NUM_BUFS, (),
			    (MPIPE_PROP_SRC_NUM_BUFS, CONFIG_PROP_NUM_BUFS,))
		MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret != 0) {
		LOG_ERR("Failed to configure the source (%d)", ret);
		return 0;
	}

	/* The frame size is what the encoder memory is reserved for */
	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_IMAGE_WIDTH,
					  MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_WIDTH,
					  MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT,
					  CONFIG_VIDEO_FRAME_HEIGHT, MPIPE_CAPS_END);
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
						  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
	if (ret != 0) {
		LOG_ERR("Failed to configure the caps filter (%d)", ret);
		return 0;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&encoder, MPIPE_PROP_VID_H264_ENCODER_QP, &qp,
		MPIPE_PROP_VID_H264_ENCODER_GOP, &gop, MPIPE_PROP_VID_H264_ENCODER_SPEED, &speed,
		MPIPE_PROP_LIST_END);
	if (ret != 0) {
		LOG_ERR("Failed to configure the encoder (%d)", ret);
		return 0;
	}

	if (CONFIG_VIDEO_PLANE >= 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&disp_sink,
						  MPIPE_PROP_DISP_SINK_PLANE, &plane,
						  MPIPE_PROP_LIST_END);
		if (ret != 0) {
			LOG_ERR("Failed to configure the display sink (%d)", ret);
			return 0;
		}
	}

	/* clang-format off */
	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&vid_src,
			    (struct mpipe_element *)&caps_filter,
			    IF_ENABLED(CONFIG_H264_TEST_PATTERN,
				       ((struct mpipe_element *)&pattern,))
			    (struct mpipe_element *)&to_yuv, (struct mpipe_element *)&encoder,
			    (struct mpipe_element *)&decoder, (struct mpipe_element *)&to_rgb,
			    (struct mpipe_element *)&disp_sink, NULL);
	/* clang-format on */
	if (ret != 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		return 0;
	}

	/* clang-format off */
	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
				 (struct mpipe_element *)&caps_filter,
				 IF_ENABLED(CONFIG_H264_TEST_PATTERN,
					    ((struct mpipe_element *)&pattern,))
				 (struct mpipe_element *)&to_yuv, (struct mpipe_element *)&encoder,
				 (struct mpipe_element *)&decoder, (struct mpipe_element *)&to_rgb,
				 (struct mpipe_element *)&disp_sink, NULL);
	/* clang-format on */
	if (ret != 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		return 0;
	}

	LOG_INF("Pipeline linked.");

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		return 0;
	}

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)mpipe_player_deinit(&player);

	LOG_INF("Done.");

	return 0;
}
