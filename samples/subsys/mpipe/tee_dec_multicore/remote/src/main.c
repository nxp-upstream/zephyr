/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The two pipelines this core runs for the pipeline of the other core, each
 * behind a server its client element talks to:
 *
 *   decoding:   app_src -> jpeg_decoder -> vid_overlay -> app_sink
 *   checking:   app_src -> caps_filter -> app_sink
 */

#include <errno.h>

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_decoder.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>
#include <zephyr/mpipe/vid/mpipe_vid_draw.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	APP_SRC_ID,
	JPEG_DEC_ID,
	OVERLAY_ID,
	APP_SINK_ID,
	CAPS_FILTER_ID,
	CHECK_SINK_ID,
};

/* The mark of this core on every frame: a bar growing with the frames it decoded */
/* IPC endpoints of the two served pipelines: the client elements use the same names */
#define DEC_ENDPOINT   "mpipe_dec"
#define CHECK_ENDPOINT "mpipe_check"

#define BAR_HEIGHT 6U
#define BAR_STEP   2U

/* The checking pipeline reports once for this many frames */
#define CHECK_REPORT_FRAMES 30U

static struct mpipe_pipeline_server check_server;
/* Each endpoint serves one pipeline: its function 0 */
static struct mpipe_pipeline_server *const check_servers[] = {&check_server};

static void draw_bar(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		     const struct net_buf *in, void *user_data)
{
	static uint32_t frames;
	struct mpipe_vid_rect bar = {
		.x0 = 0,
		.y0 = (int32_t)height - (int32_t)BAR_HEIGHT,
		.x1 = (int32_t)((frames * BAR_STEP) % width),
		.y1 = (int32_t)height,
	};

	ARG_UNUSED(in);
	ARG_UNUSED(user_data);

	frames++;
	(void)mpipe_vid_draw_fill(frame, width, height, pixfmt, &bar, 0xFFFF0000U);
}

/* A JPEG frame starts with the SOI marker and ends with the EOI marker */
static void check_frame(const struct net_buf *buf, void *user_data)
{
	static uint32_t frames;
	static uint32_t damaged;
	const uint8_t *data = buf->data;
	size_t len = buf->len;

	ARG_UNUSED(user_data);

	if (len < 4U || data[0] != 0xFFU || data[1] != 0xD8U || data[len - 2U] != 0xFFU ||
	    data[len - 1U] != 0xD9U) {
		damaged++;
	}

	frames++;
	if ((frames % CHECK_REPORT_FRAMES) == 0U) {
		LOG_INF("Checked %u frames, %u damaged", (unsigned int)frames,
			(unsigned int)damaged);
	}
}

/* app_src -> jpeg_decoder -> vid_overlay -> app_sink, served to the transform client */
static int build_decoding(struct mpipe_pipeline_server *server)
{
	static struct mpipe pipe;
	static struct mpipe_app_src app_src;
	static struct mpipe_img_jpeg_decoder jpeg_dec;
	static struct mpipe_vid_overlay overlay;
	static struct mpipe_app_sink app_sink;
	static const struct mpipe_vid_overlay_cb bar = {.fn = draw_bar};
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_app_src_init(&app_src, APP_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_img_jpeg_decoder_init(&jpeg_dec, JPEG_DEC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_vid_overlay_init(&overlay, OVERLAY_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_app_sink_init(&app_sink, APP_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&overlay,
					  MPIPE_PROP_VID_OVERLAY_DRAW_CB, &bar,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&app_src,
			    (struct mpipe_element *)&jpeg_dec, (struct mpipe_element *)&overlay,
			    (struct mpipe_element *)&app_sink, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_element_link(
		(struct mpipe_element *)&app_src, (struct mpipe_element *)&jpeg_dec,
		(struct mpipe_element *)&overlay, (struct mpipe_element *)&app_sink, NULL);
	if (ret < 0) {
		return ret;
	}

	return mpipe_pipeline_server_init(server, &pipe, &app_src, &app_sink);
}

/* app_src -> caps_filter -> app_sink, served to the sink client */
static int build_checking(struct mpipe_pipeline_server *server)
{
	static struct mpipe pipe;
	static struct mpipe_app_src app_src;
	static struct mpipe_caps_filter caps_filter;
	static struct mpipe_app_sink app_sink;
	static const struct mpipe_app_sink_cb check = {.fn = check_frame};
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_app_src_init(&app_src, APP_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_app_sink_init(&app_sink, CHECK_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	/* An app_sink takes anything: the filter is what tells the client what is checked */
	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					  MPIPE_TYPE_UINT, VIDEO_PIX_FMT_JPEG, MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&app_sink,
					  MPIPE_PROP_BASE_APP_SINK_CB, &check,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&app_src,
			    (struct mpipe_element *)&caps_filter,
			    (struct mpipe_element *)&app_sink, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_element_link((struct mpipe_element *)&app_src,
				 (struct mpipe_element *)&caps_filter,
				 (struct mpipe_element *)&app_sink, NULL);
	if (ret < 0) {
		return ret;
	}

	/* The pipeline ends here: its app_sink hands nothing back to the client */
	return mpipe_pipeline_server_init(server, &pipe, &app_src, NULL);
}

/* The checking pipeline streams on this thread, the decoding one on main() */
static void check_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)mpipe_erpc_vid_server_run(check_servers, ARRAY_SIZE(check_servers), CHECK_ENDPOINT);
}

K_THREAD_STACK_DEFINE(check_stack, CONFIG_CHECK_STACK_SIZE);

int main(void)
{
	static struct mpipe_pipeline_server dec_server;
	static struct mpipe_pipeline_server *const dec_servers[] = {&dec_server};
	static struct k_thread check;
	int ret;

	LOG_INF("Remote started");

	ret = build_decoding(&dec_server);
	if (ret != 0) {
		LOG_ERR("Failed to build the decoding pipeline (%d)", ret);
		goto err;
	}

	ret = build_checking(&check_server);
	if (ret != 0) {
		LOG_ERR("Failed to build the checking pipeline (%d)", ret);
		goto err;
	}

	/* Each server has its endpoint: the two clients never wait for each other */
	(void)k_thread_create(&check, check_stack, K_THREAD_STACK_SIZEOF(check_stack),
			      check_thread, NULL, NULL, NULL, CONFIG_MAIN_THREAD_PRIORITY, 0,
			      K_NO_WAIT);

	(void)mpipe_erpc_vid_server_run(dec_servers, ARRAY_SIZE(dec_servers), DEC_ENDPOINT);

err:
	LOG_ERR("Aborting sample");
	return 0;
}
