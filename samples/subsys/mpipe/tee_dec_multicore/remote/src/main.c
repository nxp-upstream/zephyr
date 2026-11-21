/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The two pipelines this core runs for the pipeline of the other core are
 * described in pipeline.dtsi and built at boot. The application puts each
 * behind the server its client element talks to, and brings what this core
 * draws on the frames and what it checks in them.
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>
#include <zephyr/mpipe/vid/mpipe_vid_draw.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* The mark of this core on every frame: a bar growing with the frames it decoded */
/*
 * IPC endpoints of the two served pipelines: the client elements name them in
 * the devicetree of the other core
 */
#define DEC_ENDPOINT   "mpipe_dec"
#define CHECK_ENDPOINT "mpipe_check"

#define BAR_HEIGHT 6U
#define BAR_STEP   2U

static struct mpipe_pipeline_server check_server;
/* Each endpoint serves one pipeline: its function 0 */
static struct mpipe_pipeline_server *const check_servers[] = {&check_server};

void sample_draw_bar(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
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

/* The checking pipeline reports once for this many frames */
#define CHECK_REPORT_FRAMES 30U

/* A JPEG frame starts with the SOI marker and ends with the EOI marker */
void sample_check_frame(const struct net_buf *buf, void *user_data)
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

	ret = mpipe_pipeline_server_init(
		&dec_server, MPIPE_DT_PIPELINE_GET(DT_NODELABEL(decoding_pipeline)),
		(struct mpipe_app_src *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(decoding_src)),
		(struct mpipe_app_sink *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(decoding_sink)));
	if (ret != 0) {
		LOG_ERR("Failed to serve the decoding pipeline (%d)", ret);
		goto err;
	}

	/* The pipeline ends here: its app_sink hands nothing back to the client */
	ret = mpipe_pipeline_server_init(
		&check_server, MPIPE_DT_PIPELINE_GET(DT_NODELABEL(checking_pipeline)),
		(struct mpipe_app_src *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(checking_src)), NULL);
	if (ret != 0) {
		LOG_ERR("Failed to serve the checking pipeline (%d)", ret);
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
