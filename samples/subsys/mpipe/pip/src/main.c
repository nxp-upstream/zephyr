/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <ff.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/video.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/fs/mpipe_file_src.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_decoder.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs, unique within a pipeline; the pipeline id names the player */
enum {
	CAM_PIPE_ID,
	PIP_PIPE_ID,
	VID_SRC_ID,
	CAM_CAPS_FILTER_ID,
	CAM_DISP_SINK_ID,
	FILE_SRC_ID,
	JPEG_PARSER_ID,
	PIP_CAPS_FILTER_ID,
	JPEG_DEC_ID,
	PIP_DISP_SINK_ID,
};

#define MNT_POINT "/SD:"

static FATFS fat_fs;

static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MNT_POINT,
};

static const uint8_t mjpeg[] = {
#include "mjpeg.inc"
};

/* Write the embedded MJPEG file to the volume unless it is already there */
static int embed_test_file(void)
{
	struct fs_dirent ent;
	struct fs_file_t f;
	ssize_t w;
	int ret = fs_stat(CONFIG_FILE_INPUT_PATH, &ent);

	if (ret == 0 && ent.size == sizeof(mjpeg)) {
		return 0;
	}
	if (ret != 0 && ret != -ENOENT) {
		LOG_ERR("fs_stat(%s) failed (%d)", CONFIG_FILE_INPUT_PATH, ret);
		return ret;
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, CONFIG_FILE_INPUT_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		LOG_ERR("fs_open(%s) failed (%d)", CONFIG_FILE_INPUT_PATH, ret);
		return ret;
	}

	w = fs_write(&f, mjpeg, sizeof(mjpeg));
	(void)fs_close(&f);
	if (w < 0 || (size_t)w != sizeof(mjpeg)) {
		LOG_ERR("fs_write failed (%d)", (int)w);
		return (w < 0) ? (int)w : -EIO;
	}

	LOG_INF("Created %s (%u bytes)", CONFIG_FILE_INPUT_PATH, (unsigned int)sizeof(mjpeg));

	return 0;
}

/* The camera pipeline: vid_src -> caps_filter -> disp_sink on the camera plane */
static struct mpipe cam_pipe;

/* The window pipeline: file_src -> jpeg_parser -> caps_filter -> jpeg_decoder -> disp_sink */
static struct mpipe pip_pipe;

static int build_cam_pipeline(void)
{
	static struct mpipe_vid_src vid_src;
	static struct mpipe_caps_filter cam_caps_filter;
	static struct mpipe_disp_sink cam_disp_sink;
	static const int plane = CONFIG_VIDEO_PLANE;
	struct mpipe_structure caps;
	struct mpipe_value pixfmt;
	int ret;

	ret = mpipe_pipeline_init(&cam_pipe, CAM_PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_caps_filter_init(&cam_caps_filter, CAM_CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_disp_sink_init(&cam_disp_sink, CAM_DISP_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	/* clang-format off */
	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO,
		COND_CODE_0(CONFIG_VIDEO_FRAME_WIDTH,
			(), (MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_WIDTH,))
		COND_CODE_0(CONFIG_VIDEO_FRAME_HEIGHT,
			(), (MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_HEIGHT,))
		MPIPE_CAPS_END);
	/* clang-format on */
	if (ret != 0) {
		return ret;
	}
	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		mpipe_value_set(&pixfmt, MPIPE_TYPE_UINT,
				VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT));
		mpipe_structure_append_value(&caps, MPIPE_CAPS_PIXEL_FORMAT, &pixfmt);
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&cam_caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&cam_disp_sink,
					  MPIPE_PROP_DISP_SINK_PLANE, &plane, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&cam_pipe, (struct mpipe_element *)&vid_src,
			    (struct mpipe_element *)&cam_caps_filter,
			    (struct mpipe_element *)&cam_disp_sink, NULL);
	if (ret < 0) {
		return ret;
	}

	return mpipe_element_link((struct mpipe_element *)&vid_src,
				  (struct mpipe_element *)&cam_caps_filter,
				  (struct mpipe_element *)&cam_disp_sink, NULL);
}

static int build_pip_pipeline(void)
{
	static struct mpipe_file_src file_src;
	static struct mpipe_img_jpeg_parser jpeg_parser;
	static struct mpipe_caps_filter pip_caps_filter;
	static struct mpipe_img_jpeg_decoder jpeg_dec;
	static struct mpipe_disp_sink pip_disp_sink;
	static const int plane = CONFIG_PIP_PLANE;
	static const uint16_t x = CONFIG_PIP_X;
	static const uint16_t y = CONFIG_PIP_Y;
	static const uint8_t alpha = CONFIG_PIP_ALPHA;
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_init(&pip_pipe, PIP_PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_file_src_init(&file_src, FILE_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_img_jpeg_parser_init(&jpeg_parser, JPEG_PARSER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_caps_filter_init(&pip_caps_filter, PIP_CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_img_jpeg_decoder_init(&jpeg_dec, JPEG_DEC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_disp_sink_init(&pip_disp_sink, PIP_DISP_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&file_src, MPIPE_PROP_FS_SRC_PATH,
					  CONFIG_FILE_INPUT_PATH, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_structure_init_fields(
		&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		VIDEO_PIX_FMT_JPEG, MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT,
		CONFIG_JPEG_IMAGE_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT,
		CONFIG_JPEG_IMAGE_HEIGHT, MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&pip_caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	/* The window: its own plane, placed over the camera frame, translucent */
	ret = mpipe_object_set_properties((struct mpipe_object *)&pip_disp_sink,
					  MPIPE_PROP_DISP_SINK_PLANE, &plane,
					  MPIPE_PROP_DISP_SINK_X, &x, MPIPE_PROP_DISP_SINK_Y, &y,
					  MPIPE_PROP_DISP_SINK_ALPHA, &alpha, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_bin_add(
		(struct mpipe_bin *)&pip_pipe, (struct mpipe_element *)&file_src,
		(struct mpipe_element *)&jpeg_parser, (struct mpipe_element *)&pip_caps_filter,
		(struct mpipe_element *)&jpeg_dec, (struct mpipe_element *)&pip_disp_sink, NULL);
	if (ret < 0) {
		return ret;
	}

	return mpipe_element_link(
		(struct mpipe_element *)&file_src, (struct mpipe_element *)&jpeg_parser,
		(struct mpipe_element *)&pip_caps_filter, (struct mpipe_element *)&jpeg_dec,
		(struct mpipe_element *)&pip_disp_sink, NULL);
}

int main(void)
{
	static struct mpipe_player cam_player;
	static struct mpipe_player pip_player;
	int ret = fs_mount(&mp);

	if (ret != 0) {
		LOG_ERR("fs_mount failed (%d)", ret);
		goto err;
	}
	LOG_INF("SDCard mounted at %s", MNT_POINT);

	ret = embed_test_file();
	if (ret != 0) {
		goto err;
	}

	ret = build_cam_pipeline();
	if (ret < 0) {
		LOG_ERR("Failed to build the camera pipeline (%d)", ret);
		goto err;
	}
	ret = build_pip_pipeline();
	if (ret < 0) {
		LOG_ERR("Failed to build the window pipeline (%d)", ret);
		goto err;
	}
	LOG_INF("Pipelines linked.");

	ret = mpipe_player_init(&cam_player, &cam_pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init the camera player (%d)", ret);
		goto err;
	}
	ret = mpipe_player_init(&pip_player, &pip_pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init the window player (%d)", ret);
		goto err;
	}

	(void)mpipe_player_play(&cam_player);
	(void)mpipe_player_play(&pip_player);
	(void)mpipe_player_wait_quit(&cam_player);
	(void)mpipe_player_wait_quit(&pip_player);
	(void)mpipe_player_deinit(&cam_player);
	(void)mpipe_player_deinit(&pip_player);

	ret = fs_unmount(&mp);
	if (ret != 0) {
		LOG_ERR("fs_unmount failed (%d)", ret);
	}

	LOG_INF("Done.");

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
