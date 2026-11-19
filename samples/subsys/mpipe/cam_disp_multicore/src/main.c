/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The frames of a video source the other core owns, displayed here:
 *
 *   vid_src_client -> caps_filter -> disp_sink
 *
 * The source element is the client of a pipeline of the other core, which
 * starts with the real source. See remote/src/main.c.
 */

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_src_client.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	VID_SRC_ID,
	CAPS_FILTER_ID,
	DISP_SINK_ID,
};

/* IPC endpoint of the source: the other core serves the same name */
#define CAM_ENDPOINT "mpipe_cam"

int main(void)
{
	static struct mpipe pipe;
	/* The source runs on the other core; this element is its client */
	static struct mpipe_vid_src_client vid_src;
	static struct mpipe_caps_filter caps_filter;
	static struct mpipe_disp_sink disp_sink;
	static struct mpipe_player player;
	struct mpipe_vid_rpc_channel cam_channel;
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		goto err;
	}

	/* The source is reached through an eRPC channel; its init waits for the server */
	ret = mpipe_erpc_vid_channel_get(CAM_ENDPOINT, 0U, &cam_channel);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_vid_src_client_init(&vid_src, VID_SRC_ID, &cam_channel);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
	if (ret < 0) {
		goto err;
	}

	/* The frames must fit in the memory the two cores share */
	ret = mpipe_structure_init_fields(
		&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		VIDEO_PIX_FMT_RGB565, MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT,
		CONFIG_VIDEO_FRAME_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT,
		CONFIG_VIDEO_FRAME_HEIGHT, MPIPE_CAPS_END);
	if (ret != 0) {
		goto err;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&vid_src,
			    (struct mpipe_element *)&caps_filter,
			    (struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
				 (struct mpipe_element *)&caps_filter,
				 (struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		goto err;
	}

	LOG_INF("Pipeline linked.");

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		goto err;
	}

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)mpipe_player_deinit(&player);

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
