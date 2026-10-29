/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CAM_FACE_DETECTION_PREPROCESS_H_
#define CAM_FACE_DETECTION_PREPROCESS_H_

#include <stddef.h>
#include <stdint.h>

#include "inference.h"

/**
 * @brief Prepare the quantization tables for a model input
 *
 * Each 8-bit pixel level p is normalized to (p - CONFIG_CAM_FACE_DETECTION_INPUT_MEAN) /
 * CONFIG_CAM_FACE_DETECTION_INPUT_STD, then quantized for the input tensor.
 *
 * @param input Model input tensor description
 */
void preprocess_init(const struct inference_input *input);

/**
 * @brief Convert an RGB565 frame into the model input
 *
 * The pipeline negotiates RGB565 frames of the model input size, so each
 * pixel is converted in place: no crop, no scaling.
 *
 * @param data RGB565 frame, with no row padding
 * @param size Valid bytes in @p data
 * @param input Model input tensor to fill
 *
 * @retval 0 Success
 * @retval -ENOTSUP The model input is not 3-channel RGB
 * @retval -EINVAL @p size does not hold a frame of the model input size
 */
int preprocess_run(const uint8_t *data, size_t size, const struct inference_input *input);

#endif /* CAM_FACE_DETECTION_PREPROCESS_H_ */
