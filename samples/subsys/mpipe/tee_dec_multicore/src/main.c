/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <ff.h>
#endif

#include <zephyr/drivers/video.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/base/mpipe_tee.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>
#include <zephyr/mpipe/fs/mpipe_file_sink.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_draw.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/mpipe/vid/mpipe_vid_sink_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_transform_client.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	APP_SRC_ID,
	JPEG_PARSER_ID,
	CAPS_FILTER_ID,
	TEE_ID,
	DEC_QUEUE_ID,
	DEC_CLIENT_ID,
	OVERLAY_ID,
	DISP_SINK_ID,
	CHECK_QUEUE_ID,
	CHECK_CLIENT_ID,
	REC_QUEUE_ID,
	FILE_SINK_ID,
};

/* IPC endpoints of the two client elements: the other core serves the same names */
#define DEC_ENDPOINT   "mpipe_dec"
#define CHECK_ENDPOINT "mpipe_check"

#define MNT_POINT "/SD:"

#define FEED_POLL K_MSEC(100)

static const uint8_t mjpeg[] = {
#include "mjpeg.inc"
};

static int mount_sd(void)
{
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
	static FATFS fat_fs;
#endif
	static struct fs_mount_t mnt = {
		.type = FS_FATFS,
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
		.fs_data = &fat_fs,
#endif
		.mnt_point = MNT_POINT,
	};
	int ret;

	ret = fs_mount(&mnt);
	if (ret != 0) {
		LOG_ERR("fs_mount failed (%d)", ret);
		return ret;
	}

	LOG_INF("SDCard mounted at %s", MNT_POINT);

	return 0;
}

/* What this core adds to the frames the other core decoded and drew on */
static void draw_hud(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		     const struct net_buf *in, void *user_data)
{
	static uint32_t frames;
	char text[24];

	ARG_UNUSED(in);
	ARG_UNUSED(user_data);

	frames++;
	(void)snprintf(text, sizeof(text), "M7 %u", (unsigned int)frames);
	(void)mpipe_vid_draw_text(frame, width, height, pixfmt, 4, 4, 1, 0xFF000000U, text);
}

/*
 * Push the stream every time the pipeline plays, then declare its end: a
 * replay asked of the player gets the stream again.
 */
static void feed(struct mpipe_player *player, struct mpipe_app_src *app_src)
{
	size_t offset;
	uint32_t len;

	for (;;) {
		if (mpipe_player_wait_state(player, MPIPE_STATE_PLAYING, FEED_POLL) != 0) {
			continue;
		}

		offset = 0;
		while (offset < sizeof(mjpeg)) {
			len = (uint32_t)MIN(sizeof(mjpeg) - offset,
					    CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ);

			if (mpipe_app_src_push(app_src, &mjpeg[offset], len, FEED_POLL) == 0) {
				offset += len;
				continue;
			}

			/* Stopped while the stream was being pushed; busy or paused otherwise */
			if (mpipe_player_wait_state(player, MPIPE_STATE_READY, K_NO_WAIT) == 0) {
				break;
			}
		}

		if (offset == sizeof(mjpeg)) {
			(void)mpipe_app_src_eos(app_src, K_FOREVER);
		}

		/* The player stops the pipeline at the end of the stream */
		while (mpipe_player_wait_state(player, MPIPE_STATE_READY, FEED_POLL) != 0) {
		}
	}
}

int main(void)
{
	static struct mpipe pipe;
	static struct mpipe_app_src app_src;
	static struct mpipe_img_jpeg_parser jpeg_parser;
	static struct mpipe_caps_filter caps_filter;
	static struct mpipe_tee tee;
	/* Each branch runs on a thread of its own: none waits for another */
	static struct mpipe_queue dec_queue;
	static struct mpipe_queue check_queue;
	static struct mpipe_queue rec_queue;
	/* The decoding pipeline of the other core, seen from here as one transform */
	static struct mpipe_vid_transform_client dec_client;
	static struct mpipe_vid_overlay overlay;
	static struct mpipe_disp_sink disp_sink;
	/* The checking pipeline of the other core, seen from here as one sink */
	static struct mpipe_vid_sink_client check_client;
	static struct mpipe_file_sink file_sink;
	static struct mpipe_player player;
	static const struct mpipe_vid_overlay_cb hud = {.fn = draw_hud};
	/* Nothing else holds the frames of the decoding client: draw in them */
	enum mpipe_vid_overlay_mode overlay_mode = MPIPE_VID_OVERLAY_MODE_IN_PLACE;
	struct mpipe_vid_rpc_channel dec_channel;
	struct mpipe_vid_rpc_channel check_channel;
	struct mpipe_structure caps;
	uint8_t queue_size = 1;
	uint8_t branches = 3;
	int ret;

	ret = mount_sd();
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_app_src_init(&app_src, APP_SRC_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_img_jpeg_parser_init(&jpeg_parser, JPEG_PARSER_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_tee_init(&tee, TEE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_queue_init(&dec_queue, DEC_QUEUE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_queue_init(&check_queue, CHECK_QUEUE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_queue_init(&rec_queue, REC_QUEUE_ID);
	if (ret < 0) {
		goto err;
	}
	/* Each client has an eRPC channel of its own, to function 0 of its endpoint */
	ret = mpipe_erpc_vid_channel_get(DEC_ENDPOINT, 0U, &dec_channel);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_erpc_vid_channel_get(CHECK_ENDPOINT, 0U, &check_channel);
	if (ret < 0) {
		goto err;
	}
	/* Each client waits here for its server on the other core */
	ret = mpipe_vid_transform_client_init(&dec_client, DEC_CLIENT_ID, &dec_channel);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_vid_sink_client_init(&check_client, CHECK_CLIENT_ID, &check_channel);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_vid_overlay_init(&overlay, OVERLAY_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_file_sink_init(&file_sink, FILE_SINK_ID);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&tee,
					  MPIPE_PROP_BASE_TEE_SRC_PADS_NUM, &branches,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	/* One frame in flight per branch is enough to overlap the two cores */
	ret = mpipe_object_set_properties((struct mpipe_object *)&dec_queue,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &queue_size,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&check_queue,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &queue_size,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&rec_queue,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &queue_size,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&file_sink,
					  MPIPE_PROP_FS_SINK_PATH, CONFIG_FILE_OUTPUT_PATH,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&overlay,
					  MPIPE_PROP_VID_OVERLAY_MODE, &overlay_mode,
					  MPIPE_PROP_VID_OVERLAY_DRAW_CB, &hud,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_structure_init_fields(
		&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		VIDEO_PIX_FMT_JPEG, MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT,
		CONFIG_JPEG_IMAGE_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT,
		CONFIG_JPEG_IMAGE_HEIGHT, MPIPE_CAPS_END);
	if (ret != 0) {
		goto err;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&app_src,
			    (struct mpipe_element *)&jpeg_parser,
			    (struct mpipe_element *)&caps_filter, (struct mpipe_element *)&tee,
			    (struct mpipe_element *)&dec_queue, (struct mpipe_element *)&dec_client,
			    (struct mpipe_element *)&overlay, (struct mpipe_element *)&disp_sink,
			    (struct mpipe_element *)&check_queue,
			    (struct mpipe_element *)&check_client,
			    (struct mpipe_element *)&rec_queue, (struct mpipe_element *)&file_sink,
			    NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	/* Decoded on the other core, completed and displayed here */
	ret = mpipe_element_link(
		(struct mpipe_element *)&app_src, (struct mpipe_element *)&jpeg_parser,
		(struct mpipe_element *)&caps_filter, (struct mpipe_element *)&tee,
		(struct mpipe_element *)&dec_queue, (struct mpipe_element *)&dec_client,
		(struct mpipe_element *)&overlay, (struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link the display branch (%d)", ret);
		goto err;
	}

	/* Checked by the other core */
	ret = mpipe_element_link((struct mpipe_element *)&tee,
				 (struct mpipe_element *)&check_queue,
				 (struct mpipe_element *)&check_client, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link the checking branch (%d)", ret);
		goto err;
	}

	/* Recorded here, on the SD card */
	ret = mpipe_element_link((struct mpipe_element *)&tee, (struct mpipe_element *)&rec_queue,
				 (struct mpipe_element *)&file_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link the recording branch (%d)", ret);
		goto err;
	}

	LOG_INF("Pipeline linked.");

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		goto err;
	}

	(void)mpipe_player_play(&player);

	feed(&player, &app_src);

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
