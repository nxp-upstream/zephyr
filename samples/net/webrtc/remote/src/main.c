/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The pipeline this core runs for the display leg of the other core, behind
 * the server its transform client talks to:
 *
 *   app_src -> vid_h264_decoder -> app_sink
 *
 * The other core gives a piece of the H.264 stream of the peer with every
 * call and gets the decoded frame back, in buffers of its own memory.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_decoder.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	APP_SRC_ID,
	DECODER_ID,
	APP_SINK_ID,
};

/* IPC endpoint of the served pipeline: the client element uses the same name */
#define DECODER_ENDPOINT "webrtc_dec"

/* app_src -> vid_h264_decoder -> app_sink, served to the transform client */
static int build_decoding(struct mpipe_pipeline_server *server)
{
	static struct mpipe pipe;
	static struct mpipe_app_src app_src;
	static struct mpipe_vid_h264_decoder decoder;
	static struct mpipe_app_sink app_sink;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret == 0) {
		ret = mpipe_app_src_init(&app_src, APP_SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_vid_h264_decoder_init(&decoder, DECODER_ID);
	}
	if (ret == 0) {
		ret = mpipe_app_sink_init(&app_sink, APP_SINK_ID);
	}
	if (ret == 0) {
		ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&app_src,
				    (struct mpipe_element *)&decoder,
				    (struct mpipe_element *)&app_sink, NULL);
	}
	if (ret == 0) {
		ret = mpipe_element_link((struct mpipe_element *)&app_src,
					 (struct mpipe_element *)&decoder,
					 (struct mpipe_element *)&app_sink, NULL);
	}
	if (ret != 0) {
		return ret;
	}

	return mpipe_pipeline_server_init(server, &pipe, &app_src, &app_sink);
}

int main(void)
{
	static struct mpipe_pipeline_server server;
	static struct mpipe_pipeline_server *const servers[] = {&server};
	int ret;

	LOG_INF("Remote started");

	ret = build_decoding(&server);
	if (ret != 0) {
		LOG_ERR("Failed to build the decoding pipeline (%d)", ret);
		return 0;
	}

	ret = mpipe_erpc_vid_server_run(servers, ARRAY_SIZE(servers), DECODER_ENDPOINT);
	LOG_ERR("The server stopped (%d)", ret);

	return 0;
}
