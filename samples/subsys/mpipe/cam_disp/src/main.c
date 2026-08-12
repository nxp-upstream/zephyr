/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>
#include <zephyr/video/controls.h>
#include <zephyr/logging/log.h>
#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#if DT_HAS_CHOSEN(zephyr_jpegdec) || DT_HAS_CHOSEN(zephyr_videotrans)
#include <zephyr/mpipe/vid/mpipe_vid_transform.h>
#endif
#if DT_HAS_CHOSEN(zephyr_jpegdec)
#include <zephyr/mpipe/vid/mpipe_vid_convert.h>
#endif
#include <zephyr/sys/util_macro.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	VID_SRC_ID,
	CAPS_FILTER_ID,
	JPEG_DEC_ID,
	VID_CONV_ID,
	VID_TRANS_ID,
	DISP_SINK_ID,
};

static struct mpipe pipe;
static struct mpipe_vid_src vid_src;
static struct mpipe_disp_sink disp_sink;
#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
static struct mpipe_caps_filter caps_filter;
#endif
#if (DT_HAS_CHOSEN(zephyr_jpegdec))
static struct mpipe_vid_transform jpeg_dec;
static struct mpipe_vid_convert vid_conv;
#endif
#if (DT_HAS_CHOSEN(zephyr_videotrans))
static struct mpipe_vid_transform vid_trans;
#endif
static struct mpipe_player player;

int main(void)
{
	int ret;

	MPIPE_ELEMENT_INIT(&pipe, mpipe_pipeline_init, PIPE_ID);
	MPIPE_ELEMENT_INIT(&vid_src, mpipe_vid_src_init, VID_SRC_ID);
	MPIPE_ELEMENT_INIT(&disp_sink, mpipe_disp_sink_init, DISP_SINK_ID);

	struct video_rect __maybe_unused crop = {
		CONFIG_VIDEO_SOURCE_CROP_LEFT, CONFIG_VIDEO_SOURCE_CROP_TOP,
		CONFIG_VIDEO_SOURCE_CROP_WIDTH, CONFIG_VIDEO_SOURCE_CROP_HEIGHT};

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_src,
		COND_CODE_0(CONFIG_PROP_NUM_BUFS, (),
			    (MPIPE_PROP_SRC_NUM_BUFS, CONFIG_PROP_NUM_BUFS,))
		COND_CODE_0(CONFIG_VIDEO_SOURCE_CROP_WIDTH, (), (MPIPE_PROP_VID_CROP, &crop,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_HFLIP, (VIDEO_CID_HFLIP, CONFIG_VIDEO_CTRL_HFLIP,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_VFLIP, (VIDEO_CID_VFLIP, CONFIG_VIDEO_CTRL_VFLIP,))
		MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret < 0) {
		goto err;
	}

	/* Caps filter element */
#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
	MPIPE_ELEMENT_INIT(&caps_filter, mpipe_caps_filter_init, CAPS_FILTER_ID);

	/* clang-format off */
	struct mpipe_structure caps;
	struct mpipe_value pixfmt;

	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO,
		COND_CODE_0(CONFIG_VIDEO_FRAME_WIDTH,
			(), (MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_WIDTH,))
		COND_CODE_0(CONFIG_VIDEO_FRAME_HEIGHT,
			(), (MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_HEIGHT,))
		COND_CODE_0(CONFIG_VIDEO_FRAME_RATE, (),
			(MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT,
			 MPIPE_FRAME_INTERVAL_FROM_FPS(CONFIG_VIDEO_FRAME_RATE),))
		MPIPE_CAPS_END);
	/* clang-format on */

	if (ret != 0) {
		goto err;
	}

	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		mpipe_value_set(&pixfmt, MPIPE_TYPE_UINT,
				VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT));
		mpipe_structure_append_value(&caps, MPIPE_CAPS_PIXEL_FORMAT, &pixfmt);
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}
#endif

	/* JPEG decoder element */
#if (DT_HAS_CHOSEN(zephyr_jpegdec))
	MPIPE_ELEMENT_INIT(&jpeg_dec, mpipe_vid_transform_init, JPEG_DEC_ID);
	MPIPE_ELEMENT_INIT(&vid_conv, mpipe_vid_convert_init, VID_CONV_ID);

	ret = mpipe_object_set_properties((struct mpipe_object *)&jpeg_dec, MPIPE_PROP_VID_DEVICE,
					  DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_jpegdec)),
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}
#endif

	/* Video transform element */
#if (DT_HAS_CHOSEN(zephyr_videotrans))
	MPIPE_ELEMENT_INIT(&vid_trans, mpipe_vid_transform_init, VID_TRANS_ID);

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_trans,
		COND_CODE_0(CONFIG_VIDEO_ROTATION_ANGLE,
			(), (VIDEO_CID_ROTATE, CONFIG_VIDEO_ROTATION_ANGLE,)) MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret < 0) {
		goto err;
	}
#endif

	/* clang-format off */
	/* Add elements to the pipeline - order does not matter */
	ret = mpipe_bin_add((struct mpipe_bin *)&pipe,
			(struct mpipe_element *)&vid_src,
			IF_ENABLED(CONFIG_MPIPE_BASE_CAPS_FILTER,
				   ((struct mpipe_element *)&caps_filter,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&jpeg_dec,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&vid_conv,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_videotrans),
				   ((struct mpipe_element *)&vid_trans,))
			(struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}
	/* Link elements together - order does matter */
	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
			IF_ENABLED(CONFIG_MPIPE_BASE_CAPS_FILTER,
				   ((struct mpipe_element *)&caps_filter,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&jpeg_dec,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&vid_conv,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_videotrans),
				   ((struct mpipe_element *)&vid_trans,))
			(struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		goto err;
	}
	/* clang-format on */

	LOG_INF("Pipeline linked.");

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		goto err;
	}

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)mpipe_player_deinit(&player);

	LOG_INF("Done.");

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
