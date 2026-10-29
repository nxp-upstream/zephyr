/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CAM_FACE_DETECTION_INFERENCE_H_
#define CAM_FACE_DETECTION_INFERENCE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Model input tensor: an int8 image in NHWC order, filled by the caller */
struct inference_input {
	/** Tensor data, width * height * channels bytes */
	int8_t *data;
	/** Image width in pixels */
	uint16_t width;
	/** Image height in pixels */
	uint16_t height;
	/** 1 (grey) or 3 (RGB) */
	uint16_t channels;
	/** Quantization: real = scale * (q - zero_point) */
	float scale;
	/** Quantization zero point */
	int32_t zero_point;
};

/**
 * @brief Initialize the NPU and load the model
 *
 * @param[out] input Description of the input tensor to fill before each run
 * @param[out] num_outputs Number of values in the model output, batch excluded
 *
 * @retval 0 Success
 * @retval -EIO The Neutron driver failed to initialize
 * @retval -EINVAL Unsupported model (operator, input type or shape)
 * @retval -ENOMEM The tensor arena is too small
 */
int inference_init(struct inference_input *input, size_t *num_outputs);

/**
 * @brief Run the model on the current input tensor
 *
 * @param[out] scores Dequantized output values, in the row-major order of the
 *                    output tensor
 * @param num_scores Room in @p scores
 * @param[out] usec Duration of the inference in microseconds
 *
 * @retval 0 Success
 * @retval -EIO The execution failed
 */
int inference_run(float *scores, size_t num_scores, uint32_t *usec);

#ifdef __cplusplus
}
#endif

#endif /* CAM_FACE_DETECTION_INFERENCE_H_ */
