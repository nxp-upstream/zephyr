/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The pipeline is described in the devicetree and built and played at boot.
 * The application brings what the devicetree names: the model, its working
 * memory, the function drawing the boxes and, when they are drawn in the
 * video frame, the function receiving the results.
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/nn/mpipe_nn.h>
#include <zephyr/mpipe/nn/mpipe_nn_results.h>
#include <zephyr/mpipe/vid/mpipe_vid_draw.h>
#if !defined(CONFIG_MPIPE_NN_BACKEND_TFLM)
#include <zephyr/mpipe/nn/mpipe_nn_backend_fake.h>
#endif

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* With an app_sink node the boxes are drawn in the video frame, else on a plane */
#define DRAW_IN_FRAME DT_NODE_EXISTS(DT_NODELABEL(results_sink))

/* The angle the transform turns the video by, which a plane never goes through */
#define ROTATION DT_PROP_OR(DT_NODELABEL(rotator), rotation, 0)

BUILD_ASSERT(CONFIG_APPLICATION_INIT_PRIORITY < CONFIG_MPIPE_DT_INIT_PRIORITY,
	     "The application must be ready before the pipeline is played");

#if DRAW_IN_FRAME
/* The overlay in the display branch reads what the app_sink publishes here */
static struct mpipe_nn_results_store results_store;
#endif

#if defined(CONFIG_MPIPE_NN_BACKEND_TFLM)
static const uint8_t model_data[] __aligned(16) = {
#include "nn_model.inc"
};

#if defined(CONFIG_TFLM_NXP_NEUTRON)
/* The NPU reads the model and the arena in place: both live in a region it reaches */
#define NPU_SECTION Z_GENERIC_SECTION(CONFIG_SAMPLE_NN_NPU_REGION_NAME)

static uint8_t npu_model[sizeof(model_data)] __aligned(16) NPU_SECTION;
static uint8_t arena[CONFIG_SAMPLE_NN_ARENA_SIZE] __aligned(16) NPU_SECTION;

const struct mpipe_nn_model sample_nn_model = {.data = npu_model, .size = sizeof(npu_model)};
#else
static uint8_t arena[CONFIG_SAMPLE_NN_ARENA_SIZE] __aligned(16);

const struct mpipe_nn_model sample_nn_model = {.data = model_data, .size = sizeof(model_data)};
#endif
#else  /* fake backend */
static const struct mpipe_nn_fake_model fake_model_cfg = {
	.in_w = 96,
	.in_h = 96,
	.channels = 1,
	.type = MPIPE_NN_TENSOR_TYPE_INT8,
	.num_classes = 0,
	.num_boxes = 2,
};

static uint8_t arena[64];

const struct mpipe_nn_model sample_nn_model = {.data = (const uint8_t *)&fake_model_cfg,
					       .size = sizeof(fake_model_cfg)};
#endif /* CONFIG_MPIPE_NN_BACKEND_TFLM */

const struct mpipe_nn_arena sample_nn_arena = {.data = arena, .size = sizeof(arena)};

static void log_results(const struct mpipe_nn_results *res)
{
	if (res->count > 0U) {
		LOG_INF("Frame %u: %u box(es), best score %d%% (class %u)", res->seq, res->count,
			(int)(res->boxes[0].score * 100.0f), res->boxes[0].class_id);
	} else if ((res->seq % 64U) == 0U) {
		/* A heartbeat while nothing is detected */
		LOG_INF("Frame %u: no box", res->seq);
	}
}

/*
 * A box in the camera frame, as the detector sees it, placed in the frame
 * drawn: the camera frame itself, or the panel's frame after the rotation the
 * transform applies to the video.
 */
static void box_to_rect(const struct mpipe_nn_box *box, uint16_t width, uint16_t height,
			struct mpipe_vid_rect *rect)
{
	float x0 = box->xmin;
	float y0 = box->ymin;
	float x1 = box->xmax;
	float y1 = box->ymax;

	if (!DRAW_IN_FRAME) {
		switch (ROTATION) {
		case 90:
			x0 = 1.0f - box->ymax;
			x1 = 1.0f - box->ymin;
			y0 = box->xmin;
			y1 = box->xmax;
			break;
		case 180:
			x0 = 1.0f - box->xmax;
			x1 = 1.0f - box->xmin;
			y0 = 1.0f - box->ymax;
			y1 = 1.0f - box->ymin;
			break;
		case 270:
			x0 = box->ymin;
			x1 = box->ymax;
			y0 = 1.0f - box->xmax;
			y1 = 1.0f - box->xmin;
			break;
		default:
			break;
		}
	}

	rect->x0 = (int32_t)(x0 * (float)width);
	rect->y0 = (int32_t)(y0 * (float)height);
	rect->x1 = (int32_t)(x1 * (float)width);
	rect->y1 = (int32_t)(y1 * (float)height);
}

/*
 * Render a result set: on a plane, the one that just arrived from the
 * decoder, in the thread of the inference branch; in the frame, the latest
 * published one, in the thread of the display branch.
 */
void sample_draw_cb(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		    const struct net_buf *in, void *user_data)
{
	struct mpipe_nn_results res;

	ARG_UNUSED(user_data);

#if DRAW_IN_FRAME
	ARG_UNUSED(in);

	if (mpipe_nn_results_store_get(&results_store, &res) != 0) {
		return;
	}
#else
	if (mpipe_buffer_get_meta(in)->bytes_used < sizeof(res)) {
		return;
	}

	memcpy(&res, in->data, sizeof(res));
	log_results(&res);
#endif

	if (res.type != MPIPE_NN_RESULTS_DETECTION) {
		return;
	}

	for (uint8_t i = 0; i < res.count; i++) {
		struct mpipe_vid_rect rect;

		box_to_rect(&res.boxes[i], width, height, &rect);
		(void)mpipe_vid_draw_rect(frame, width, height, pixfmt, &rect, 0xFF00FF00, 2);
	}
}

#if DRAW_IN_FRAME
/* Runs in the inference branch thread: publish the boxes for the overlay */
void sample_results_cb(const struct net_buf *buf, void *user_data)
{
	const struct mpipe_nn_results *res =
		(const struct mpipe_nn_results *)(const void *)buf->data;

	ARG_UNUSED(user_data);

	if (mpipe_buffer_get_meta(buf)->bytes_used < sizeof(*res)) {
		return;
	}

	mpipe_nn_results_store_put(&results_store, res);
	log_results(res);
}
#endif

#if DRAW_IN_FRAME || defined(CONFIG_TFLM_NXP_NEUTRON)
/* What the callbacks and the runner need before the pipeline is played */
static int sample_init(void)
{
#if DRAW_IN_FRAME
	mpipe_nn_results_store_init(&results_store);
#endif
#if defined(CONFIG_TFLM_NXP_NEUTRON)
	memcpy(npu_model, model_data, sizeof(npu_model));
#endif

	return 0;
}

SYS_INIT(sample_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
