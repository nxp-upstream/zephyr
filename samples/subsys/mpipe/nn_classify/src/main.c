/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The pipeline is described in pipeline.dtsi and built and played at boot.
 * The application brings what the devicetree names: the model, its working
 * memory and the function receiving the results.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/nn/mpipe_nn.h>
#include <zephyr/mpipe/nn/mpipe_nn_results.h>
#if !defined(CONFIG_MPIPE_NN_BACKEND_TFLM)
#include <zephyr/mpipe/nn/mpipe_nn_backend_fake.h>
#endif

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#if defined(CONFIG_MPIPE_NN_BACKEND_TFLM)
static const uint8_t model_data[] __aligned(16) = {
#include "nn_model.inc"
};

static uint8_t arena[CONFIG_SAMPLE_NN_ARENA_SIZE] __aligned(16);

/* Class order of the person detection model */
static const char *const labels[] = {"no person", "person"};

const struct mpipe_nn_model sample_nn_model = {.data = model_data, .size = sizeof(model_data)};
#else  /* fake backend */
static const struct mpipe_nn_fake_model fake_model_cfg = {
	.in_w = 96,
	.in_h = 96,
	.channels = 1,
	.type = MPIPE_NN_TENSOR_TYPE_INT8,
	.num_classes = 2,
};

static uint8_t arena[64];

static const char *const labels[] = {"class 0", "class 1"};

const struct mpipe_nn_model sample_nn_model = {.data = (const uint8_t *)&fake_model_cfg,
					       .size = sizeof(fake_model_cfg)};
#endif /* CONFIG_MPIPE_NN_BACKEND_TFLM */

const struct mpipe_nn_arena sample_nn_arena = {.data = arena, .size = sizeof(arena)};

/* Runs in the inference branch thread: report the classification of one frame */
void sample_results_cb(const struct net_buf *buf, void *user_data)
{
	const struct mpipe_nn_results *res =
		(const struct mpipe_nn_results *)(const void *)buf->data;

	ARG_UNUSED(user_data);

	if (mpipe_buffer_get_meta(buf)->bytes_used < sizeof(*res) || res->count == 0U) {
		return;
	}

	LOG_INF("Frame %u: %s (score %d%%)", res->seq,
		(res->cls[0].class_id < ARRAY_SIZE(labels)) ? labels[res->cls[0].class_id] : "?",
		(int)(res->cls[0].score * 100.0f));
}
