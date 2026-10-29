/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/video/controls.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/base/mpipe_tee.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>

#include "detect.h"
#include "inference.h"
#include "preprocess.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	VID_SRC_ID,
	TEE_ID,
	APP_SINK_ID,
	QUEUE_ID,
	OVERLAY_ID,
	DISP_SINK_ID,
};

/* Box outline color and width, as in the MPP person detection demo */
#define BOX_COLOR       0x0000FFU
#define RECT_LINE_WIDTH 2U

#define DETECTION_LABEL "face"

static struct mpipe pipe;
static struct mpipe_vid_src vid_src;
static struct mpipe_app_sink app_sink;
static struct mpipe_player player;
static struct mpipe_tee tee;
static struct mpipe_queue disp_queue;
static struct mpipe_vid_overlay overlay;
static struct mpipe_disp_sink disp_sink;

static struct inference_input model_in;
static size_t num_outputs;

/*
 * Dequantized model outputs, one row per anchor. They do not fit the
 * inference thread's stack, their only user.
 */
static float outputs[DETECT_MAX_OUTPUTS];

/* Lit while a face is in view; boards without a led0 alias run without it */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

K_THREAD_STACK_DEFINE(inference_stack, CONFIG_INFERENCE_STACK_SIZE);
static struct k_thread inference_tid;

/*
 * Handshake between the streaming thread, which fills the input tensor, and
 * the inference thread, which runs the model on it. The tensor is in use
 * while a frame is being scored: with a live camera the next frames are
 * dropped rather than queued, so the result always describes a recent frame.
 */
static K_SEM_DEFINE(model_idle, 1, 1);
static K_SEM_DEFINE(frame_ready, 0, 1);
static uint32_t frame_num;
static uint32_t frame_pre_us;
static uint32_t frames_dropped;

/*
 * Runs on the streaming thread for every frame the sink delivers: a frame of
 * the model input size in RGB565, as negotiated. Converting it into the input
 * tensor here, rather than queueing the buffer for the inference thread,
 * returns the buffer at once: holding it would starve a camera driver that
 * cycles through a small set of buffers.
 */
static void frame_cb(const struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(user_data);

	size_t size = mpipe_buffer_get_meta(buf)->bytes_used;
	uint32_t start;
	int ret;

	if (k_sem_take(&model_idle,
		       IS_ENABLED(CONFIG_INFERENCE_DROP_FRAMES) ? K_NO_WAIT : K_FOREVER) != 0) {
		/* The model still holds the tensor: drop this frame */
		frames_dropped++;
		return;
	}

	start = k_cycle_get_32();
	ret = preprocess_run(buf->data, size, &model_in);
	if (ret != 0) {
		LOG_ERR("Frame %u: preprocessing failed (%d), %u bytes", frame_num, ret,
			(unsigned int)size);
		frame_num++;
		k_sem_give(&model_idle);
		return;
	}

	frame_pre_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
	k_sem_give(&frame_ready);
}

/*
 * The producer: runs the model on each frame, decodes the
 * outputs into boxes and publishes them. It never touches the pipeline or
 * the console; the periodic consumer does.
 */
static void inference_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct detect_result result = {
			.model_width = model_in.width,
			.model_height = model_in.height,
		};
		uint32_t run_us;
		int ret;

		(void)k_sem_take(&frame_ready, K_FOREVER);

		ret = inference_run(outputs, num_outputs, &run_us);
		if (ret != 0) {
			LOG_ERR("Frame %u: inference failed (%d)", frame_num, ret);
		} else {
			result.inference_us = run_us;
			detect_decode(outputs, num_outputs, &result);
			detect_result_publish(&result);
			LOG_DBG("Frame %u: %u box(es), preprocess %u us, inference %u us, "
				"%u dropped",
				frame_num, result.count, frame_pre_us, run_us, frames_dropped);
		}

		frame_num++;
		k_sem_give(&model_idle);
	}
}

/* Overlay rectangle of an area of the frame, inclusive bounds */
static struct mpipe_vid_overlay_rect overlay_rect(uint16_t left, uint16_t top, uint16_t right,
						  uint16_t bottom, uint32_t color)
{
	struct mpipe_vid_overlay_rect r = {
		.left = left,
		.top = top,
		.right = right,
		.bottom = bottom,
		.line_width = RECT_LINE_WIDTH,
		.color = color,
	};

	return r;
}

/* Outline of each box in blue; model input pixels are frame pixels */
static void update_overlay(const struct detect_result *result)
{
	struct mpipe_vid_overlay_rect rects[CONFIG_CAM_FACE_DETECTION_MAX_BOXES];
	struct mpipe_vid_overlay_rects set = {.rects = rects, .count = 0};
	int ret;

	for (uint8_t i = 0; i < result->count; i++) {
		const struct detect_box *b = &result->boxes[i];

		rects[set.count++] = overlay_rect(b->left, b->top, b->right, b->bottom, BOX_COLOR);
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&overlay,
					  MPIPE_PROP_VID_OVERLAY_RECTS, &set, MPIPE_PROP_LIST_END);
	if (ret != 0) {
		LOG_ERR("Overlay update failed (%d)", ret);
	}
}

/*
 * The consumer: once per CONFIG_CAM_FACE_DETECTION_PRINT_PERIOD_MS, take the
 * latest result, if a new one was published, update the display overlay and
 * the LED, and print it.
 */
static uint32_t last_processed_frame;

static void process_results(void)
{
	struct detect_result result;

	if (!detect_result_fetch(&result, last_processed_frame)) {
		return;
	}

	last_processed_frame = result.frame_num;

	update_overlay(&result);

	if (led.port != NULL) {
		(void)gpio_pin_set_dt(&led, (result.count > 0U) ? 1 : 0);
	}

	printk("\ninference time %u ms\n", result.inference_us / USEC_PER_MSEC);
	if (result.count == 0U) {
		printk("detect: no %s detected\n", DETECTION_LABEL);
	} else {
		for (uint8_t i = 0; i < result.count; i++) {
			printk("detect: box %u label %s score %u(%%)\n", i, DETECTION_LABEL,
			       (uint32_t)(result.boxes[i].score * 100.0f + 0.5f));
		}
	}
}

static void results_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(results_work, results_work_handler);

static void results_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	process_results();
	(void)k_work_reschedule(&results_work, K_MSEC(CONFIG_CAM_FACE_DETECTION_PRINT_PERIOD_MS));
}

static int setup_led(void)
{
	if (led.port == NULL) {
		return 0;
	}

	if (!gpio_is_ready_dt(&led)) {
		return -ENODEV;
	}

	return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

static int setup_source(void)
{
	int ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);

	if (ret != 0) {
		return ret;
	}

	/* clang-format off */
	return mpipe_object_set_properties((struct mpipe_object *)&vid_src,
		COND_CODE_0(CONFIG_PROP_NUM_BUFS, (),
			    (MPIPE_PROP_SRC_NUM_BUFS, CONFIG_PROP_NUM_BUFS,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_HFLIP, (VIDEO_CID_HFLIP, CONFIG_VIDEO_CTRL_HFLIP,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_VFLIP, (VIDEO_CID_VFLIP, CONFIG_VIDEO_CTRL_VFLIP,))
		MPIPE_PROP_LIST_END);
	/* clang-format on */
}

/*
 * The frames the model is fed: RGB565 of the model input size, which the
 * camera delivers directly. A camera that cannot deliver them fails the
 * negotiation instead of passing frames the preprocessing would refuse.
 */
static int setup_app_sink(void)
{
	static const struct mpipe_app_sink_cb cb = {.fn = frame_cb};
	struct mpipe_structure caps;
	int ret = mpipe_app_sink_init(&app_sink, APP_SINK_ID);

	if (ret != 0) {
		return ret;
	}

	/* clang-format off */
	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO,
		MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT, (uint32_t)VIDEO_PIX_FMT_RGB565,
		MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, (uint32_t)model_in.width,
		MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, (uint32_t)model_in.height,
		COND_CODE_0(CONFIG_VIDEO_FRAME_RATE, (),
			(MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT,
			 MPIPE_FRAME_INTERVAL_FROM_FPS(CONFIG_VIDEO_FRAME_RATE),))
		MPIPE_CAPS_END);
	/* clang-format on */
	if (ret != 0) {
		return ret;
	}

	return mpipe_object_set_properties((struct mpipe_object *)&app_sink,
					   MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
					   MPIPE_PROP_BASE_APP_SINK_CB, &cb, MPIPE_PROP_LIST_END);
}

/*
 * The display branch: a queue, so the frames are pushed to the display on a
 * thread of its own and a slow panel does not hold the capture up, the
 * overlay drawing the boxes, and the display sink itself.
 * The queue leaks the oldest buffer rather than blocking, since the buffers
 * it holds cannot be reused meanwhile.
 */
static int setup_display_branch(void)
{
	static const uint8_t queue_size = CONFIG_CAM_FACE_DETECTION_DISPLAY_QUEUE_SIZE;
	static const enum mpipe_base_queue_leak leak = MPIPE_BASE_QUEUE_LEAK_OLDEST;
	int ret = mpipe_queue_init(&disp_queue, QUEUE_ID);

	if (ret != 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&disp_queue,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &queue_size,
					  MPIPE_PROP_BASE_QUEUE_LEAK, &leak, MPIPE_PROP_LIST_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_vid_overlay_init(&overlay, OVERLAY_ID);
	if (ret != 0) {
		return ret;
	}

	return mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
}

/*
 * A tee feeds the application and the display the same buffers:
 *
 *   vid_src -> tee -> app_sink
 *                  -> queue -> overlay -> disp_sink
 *
 * The caps of app_sink set the camera format through the tee. The tee pushes
 * to app_sink first, whose callback has converted the frame into the input
 * tensor before the overlay draws into the same buffer.
 */
static int setup_pipeline(void)
{
	int ret = mpipe_pipeline_init(&pipe, PIPE_ID);

	if (ret != 0) {
		return ret;
	}

	ret = setup_source();
	if (ret != 0) {
		return ret;
	}

	ret = setup_app_sink();
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_tee_init(&tee, TEE_ID);
	if (ret != 0) {
		return ret;
	}

	ret = setup_display_branch();
	if (ret != 0) {
		LOG_ERR("Failed to set the display branch up (%d)", ret);
		return ret;
	}

	/* clang-format off */
	/* Add elements to the pipeline - order does not matter */
	ret = mpipe_bin_add((struct mpipe_bin *)&pipe,
			    (struct mpipe_element *)&vid_src,
			    (struct mpipe_element *)&tee,
			    (struct mpipe_element *)&app_sink,
			    (struct mpipe_element *)&disp_queue,
			    (struct mpipe_element *)&overlay,
			    (struct mpipe_element *)&disp_sink,
			    NULL);
	if (ret != 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		return ret;
	}

	/* Inference branch - order does matter */
	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
				 (struct mpipe_element *)&tee,
				 (struct mpipe_element *)&app_sink, NULL);

	if (ret != 0) {
		LOG_ERR("Failed to link the inference branch (%d)", ret);
		return ret;
	}

	/* Display branch, off the tee's second source pad */
	ret = mpipe_element_link((struct mpipe_element *)&tee, (struct mpipe_element *)&disp_queue,
				 (struct mpipe_element *)&overlay,
				 (struct mpipe_element *)&disp_sink, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to link the display branch (%d)", ret);
		return ret;
	}

	return 0;
}

int main(void)
{
	int ret;

	printk("\n");
	printk("========================================\n");
	printk("  Zephyr mpipe Face Detection Demo\n");
	printk("  TFLite Micro on the Neutron NPU\n");
	printk("========================================\n");
	printk("\n");

	ret = inference_init(&model_in, &num_outputs);
	if (ret != 0) {
		LOG_ERR("Model setup failed (%d)", ret);
		return 0;
	}

	if (!detect_outputs_supported(num_outputs)) {
		LOG_ERR("Model has %u outputs, more than %u", (unsigned int)num_outputs,
			DETECT_MAX_OUTPUTS);
		return 0;
	}

	ret = setup_led();
	if (ret != 0) {
		LOG_WRN("LED unavailable (%d)", ret);
	}

	preprocess_init(&model_in);

	ret = setup_pipeline();
	if (ret != 0) {
		LOG_ERR("Pipeline setup failed (%d)", ret);
		return 0;
	}

	LOG_INF("Pipeline linked.");

	(void)k_thread_create(&inference_tid, inference_stack,
			      K_THREAD_STACK_SIZEOF(inference_stack), inference_thread, NULL, NULL,
			      NULL, CONFIG_INFERENCE_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&inference_tid, "inference");

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		return 0;
	}

	printk("\n========================================\n");
	printk("  Pipeline Running - Detecting Faces\n");
	printk("========================================\n\n");

	(void)k_work_schedule(&results_work, K_MSEC(CONFIG_CAM_FACE_DETECTION_PRINT_PERIOD_MS));

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)k_work_cancel_delayable(&results_work);
	(void)mpipe_player_deinit(&player);

	LOG_INF("Done.");

	return 0;
}
