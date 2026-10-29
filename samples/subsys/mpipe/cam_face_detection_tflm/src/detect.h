/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CAM_FACE_DETECTION_DETECT_H_
#define CAM_FACE_DETECTION_DETECT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/*
 * The Ultra-Light face detector output: one row per anchor holding the
 * no-face and face probabilities and the box corners left, top, right,
 * bottom, each relative to the input size. The model decodes the anchors
 * itself. The anchor count follows the input size: 4420 at 320x240, 9984 at
 * 480x360, the largest input whose tensors fit the NPU-reachable SRAM.
 */
#define DETECT_ANCHOR_CHANNELS  6U
#define DETECT_MAX_ANCHORS      9984U
#define DETECT_MAX_OUTPUTS      (DETECT_MAX_ANCHORS * DETECT_ANCHOR_CHANNELS)

/* Boxes above the threshold kept for the non-maximum suppression, at most */
#define DETECT_MAX_CANDIDATES 196U

/** One detected face, in pixels of the model input, which are those of the frame */
struct detect_box {
	/** Left column, inclusive */
	uint16_t left;
	/** Top row, inclusive */
	uint16_t top;
	/** Right column, inclusive */
	uint16_t right;
	/** Bottom row, inclusive */
	uint16_t bottom;
	/** Confidence in [0, 1] */
	float score;
};

/** The outcome of one inference, as published to the consumer */
struct detect_result {
	/** Detected faces */
	struct detect_box boxes[CONFIG_CAM_FACE_DETECTION_MAX_BOXES];
	/** Valid entries in @p boxes */
	uint8_t count;
	/** Model input size the boxes are expressed in */
	uint16_t model_width;
	/** Model input size the boxes are expressed in */
	uint16_t model_height;
	/** Duration of the inference in microseconds */
	uint32_t inference_us;
	/** Increments with every published result, starting at 1 */
	uint32_t frame_num;
};

/**
 * @brief Publish a result, replacing the previous one
 *
 * Called by the producer only. The result is copied; the frame number is
 * assigned here.
 *
 * @param result Result to publish
 */
void detect_result_publish(const struct detect_result *result);

/**
 * @brief Fetch the latest published result
 *
 * @param[out] result Copy of the latest result
 * @param last_frame Frame number the caller already processed
 *
 * @retval true @p result holds a result newer than @p last_frame
 * @retval false Nothing new was published
 */
bool detect_result_fetch(struct detect_result *result, uint32_t last_frame);

/**
 * @brief Check that the model output matches the decoding built in
 *
 * @param num_outputs Number of values in the model output
 *
 * @retval true detect_decode() can read this output
 * @retval false The output is not whole anchor rows, or holds more than
 *               DETECT_MAX_ANCHORS of them
 */
bool detect_outputs_supported(size_t num_outputs);

/**
 * @brief Turn the model outputs into boxes
 *
 * The detector yields one box per anchor: the boxes scoring below
 * CONFIG_CAM_FACE_DETECTION_THRESHOLD are dropped and the best
 * DETECT_MAX_CANDIDATES others go through a non-maximum suppression at
 * CONFIG_CAM_FACE_DETECTION_NMS_THRESHOLD, best score first.
 *
 * Not reentrant: the detector decodes into a static candidate list.
 *
 * @param scores Dequantized model outputs
 * @param num_scores Entries in @p scores
 * @param[in,out] result Result whose model size is set; boxes are written
 */
void detect_decode(const float *scores, size_t num_scores, struct detect_result *result);

#endif /* CAM_FACE_DETECTION_DETECT_H_ */
