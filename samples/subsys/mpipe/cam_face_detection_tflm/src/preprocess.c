/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <math.h>

#include <zephyr/sys/util.h>

#include "preprocess.h"

/*
 * Quantized value of each RGB565 component, expanded to 8 bits by replicating
 * its high bits into the low ones, for the current model input
 */
static int8_t quant_5bit[32];
static int8_t quant_6bit[64];

static int8_t quantize(const struct inference_input *input, uint32_t level)
{
	float real = ((float)level - (float)CONFIG_CAM_FACE_DETECTION_INPUT_MEAN) /
		     (float)CONFIG_CAM_FACE_DETECTION_INPUT_STD;
	int32_t q = (int32_t)lroundf(real / input->scale) + input->zero_point;

	return (int8_t)CLAMP(q, INT8_MIN, INT8_MAX);
}

void preprocess_init(const struct inference_input *input)
{
	__ASSERT_NO_MSG(input != NULL);

	for (uint32_t i = 0U; i < ARRAY_SIZE(quant_5bit); i++) {
		quant_5bit[i] = quantize(input, (i << 3) | (i >> 2));
	}

	for (uint32_t i = 0U; i < ARRAY_SIZE(quant_6bit); i++) {
		quant_6bit[i] = quantize(input, (i << 2) | (i >> 4));
	}
}

int preprocess_run(const uint8_t *data, size_t size, const struct inference_input *input)
{
	__ASSERT_NO_MSG(data != NULL);
	__ASSERT_NO_MSG(input != NULL);

	if (input->channels != 3U) {
		return -ENOTSUP;
	}

	size_t pixels = (size_t)input->width * input->height;

	if (size < pixels * 2U) {
		return -EINVAL;
	}

	int8_t *out = input->data;

	/* RGB565 is little endian: the high byte of a pixel comes second */
	for (size_t i = 0U; i < pixels; i++) {
		uint16_t px = (uint16_t)(data[0] | (data[1] << 8));

		out[0] = quant_5bit[px >> 11];
		out[1] = quant_6bit[(px >> 5) & 0x3FU];
		out[2] = quant_5bit[px & 0x1FU];
		data += 2;
		out += 3;
	}

	return 0;
}
