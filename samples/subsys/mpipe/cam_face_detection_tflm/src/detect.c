/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include "detect.h"

/*
 * Latest-result mailbox between the inference thread and the periodic
 * consumer. Both sides copy a small structure under a spinlock, so neither
 * ever waits for the other to finish processing, and the consumer always sees
 * one complete result.
 */
static struct k_spinlock result_lock;
static struct detect_result latest;
static uint32_t published;

void detect_result_publish(const struct detect_result *result)
{
	__ASSERT_NO_MSG(result != NULL);

	k_spinlock_key_t key = k_spin_lock(&result_lock);

	latest = *result;
	latest.frame_num = ++published;
	k_spin_unlock(&result_lock, key);
}

bool detect_result_fetch(struct detect_result *result, uint32_t last_frame)
{
	__ASSERT_NO_MSG(result != NULL);

	bool fresh;
	k_spinlock_key_t key = k_spin_lock(&result_lock);

	fresh = (published != 0U) && (published != last_frame);
	if (fresh) {
		*result = latest;
	}
	k_spin_unlock(&result_lock, key);

	return fresh;
}

bool detect_outputs_supported(size_t num_outputs)
{
	return (num_outputs > 0U) && ((num_outputs % DETECT_ANCHOR_CHANNELS) == 0U) &&
	       (num_outputs <= DETECT_MAX_OUTPUTS);
}

static uint32_t box_area(const struct detect_box *b)
{
	return (uint32_t)(b->right - b->left + 1U) * (uint32_t)(b->bottom - b->top + 1U);
}

/* Whether a and b overlap by at least CONFIG_CAM_FACE_DETECTION_NMS_THRESHOLD percent */
static bool boxes_overlap(const struct detect_box *a, const struct detect_box *b)
{
	uint16_t left = MAX(a->left, b->left);
	uint16_t top = MAX(a->top, b->top);
	uint16_t right = MIN(a->right, b->right);
	uint16_t bottom = MIN(a->bottom, b->bottom);

	if (right < left || bottom < top) {
		return false;
	}

	uint32_t inter = (uint32_t)(right - left + 1U) * (uint32_t)(bottom - top + 1U);
	uint32_t uni = box_area(a) + box_area(b) - inter;

	/* inter / uni >= threshold / 100, without division */
	return inter * 100U >= uni * (uint32_t)CONFIG_CAM_FACE_DETECTION_NMS_THRESHOLD;
}

/* Pixel coordinate of a normalized position, clamped to [0, size - 1] */
static uint16_t to_pixel(float pos, uint16_t size)
{
	float px = pos * (float)size;

	return (uint16_t)CLAMP(px, 0.0f, (float)(size - 1U));
}

/*
 * Candidate boxes of the current inference, best score first. Only the
 * inference thread decodes, so a single static list serves every call and
 * keeps the candidates off its stack.
 */
static struct detect_box candidates[DETECT_MAX_CANDIDATES];

/* Insert a box in the sorted candidates; a full list drops its lowest score */
static size_t candidate_insert(size_t num, const struct detect_box *box)
{
	size_t pos = MIN(num, (size_t)DETECT_MAX_CANDIDATES - 1U);

	if (num == DETECT_MAX_CANDIDATES && candidates[pos].score >= box->score) {
		return num;
	}

	/* Insertion sort: there are few candidates above the threshold */
	while (pos > 0U && candidates[pos - 1U].score < box->score) {
		candidates[pos] = candidates[pos - 1U];
		pos--;
	}
	candidates[pos] = *box;

	return MIN(num + 1U, (size_t)DETECT_MAX_CANDIDATES);
}

/* Greedy non-maximum suppression over the sorted candidates */
static void candidates_suppress(size_t num, struct detect_result *result)
{
	for (size_t i = 0U; i < num && result->count < CONFIG_CAM_FACE_DETECTION_MAX_BOXES; i++) {
		bool suppressed = false;

		for (uint8_t k = 0U; k < result->count; k++) {
			if (boxes_overlap(&result->boxes[k], &candidates[i])) {
				suppressed = true;
				break;
			}
		}

		if (!suppressed) {
			result->boxes[result->count] = candidates[i];
			result->count++;
		}
	}
}

static void anchor_decode(const float *scores, size_t num_scores, struct detect_result *result)
{
	const float threshold = (float)CONFIG_CAM_FACE_DETECTION_THRESHOLD / 100.0f;
	const size_t num_anchors = num_scores / DETECT_ANCHOR_CHANNELS;
	size_t num = 0U;

	for (size_t i = 0U; i < num_anchors; i++) {
		const float *anchor = &scores[i * DETECT_ANCHOR_CHANNELS];
		float score = anchor[1];

		if (score < threshold) {
			continue;
		}

		/* The model decodes the anchors: corners relative to the input */
		struct detect_box box = {
			.left = to_pixel(anchor[2], result->model_width),
			.top = to_pixel(anchor[3], result->model_height),
			.right = to_pixel(anchor[4], result->model_width),
			.bottom = to_pixel(anchor[5], result->model_height),
			.score = CLAMP(score, 0.0f, 1.0f),
		};

		if (box.right <= box.left || box.bottom <= box.top) {
			continue;
		}

		num = candidate_insert(num, &box);
	}

	candidates_suppress(num, result);
}

void detect_decode(const float *scores, size_t num_scores, struct detect_result *result)
{
	__ASSERT_NO_MSG(scores != NULL);
	__ASSERT_NO_MSG(result != NULL);

	result->count = 0U;

	if (!detect_outputs_supported(num_scores)) {
		return;
	}

	anchor_decode(scores, num_scores, result);
}
