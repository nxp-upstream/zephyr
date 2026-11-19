/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Remote side: this core owns the video source and serves its pipeline to the
 * source client of the other core.
 *
 *   vid_src -> app_sink
 *
 * The pipeline streams on its own thread; the client of the other core takes
 * each frame that reaches the app_sink.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

enum {
	PIPE_ID,
	VID_SRC_ID,
	APP_SINK_ID,
};

/* IPC endpoint of the served pipeline: the source client uses the same name */
#define CAM_ENDPOINT "mpipe_cam"

int main(void)
{
	static struct mpipe pipe;
	static struct mpipe_vid_src vid_src;
	static struct mpipe_app_sink app_sink;
	static struct mpipe_pipeline_server server;
	static struct mpipe_pipeline_server *const servers[] = {&server};
	int ret;

	LOG_INF("Remote started");

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_app_sink_init(&app_sink, APP_SINK_ID);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&vid_src,
			    (struct mpipe_element *)&app_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
				 (struct mpipe_element *)&app_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		goto err;
	}

	/* The pipeline has a source of its own: the server is given no application source */
	ret = mpipe_pipeline_server_init(&server, &pipe, NULL, &app_sink);
	if (ret < 0) {
		LOG_ERR("Failed to init the pipeline server (%d)", ret);
		goto err;
	}

	(void)mpipe_erpc_vid_server_run(servers, ARRAY_SIZE(servers), CAM_ENDPOINT);

err:
	LOG_ERR("Aborting sample");
	return 0;
}
