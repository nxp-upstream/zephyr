/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Audio source element header file.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_SRC_H_
#define ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_SRC_H_

/**
 * @defgroup mpipe_aud_sources Sources
 * @ingroup mpipe_aud
 * @brief Audio source elements backed by audio devices.
 * @{
 */

#include <zephyr/device.h>

#include <zephyr/mpipe/mpipe_src.h>

#include <zephyr/mpipe/aud/mpipe_aud.h>

/**
 * @brief Audio source property identifiers
 *
 * Enumeration defining property IDs specific to source elements.
 * These properties extend the base source properties.
 */
enum mpipe_prop_aud_src {
	/** Pointer to source memory slab for audio buffer management */
	MPIPE_PROP_AUD_SRC_SLAB_PTR = MPIPE_PROP_SRC_LAST,
	/** Audio source device */
	MPIPE_PROP_AUD_SRC_DEVICE,
};

/**
 * @struct mpipe_aud_src
 * @brief Audio source element structure
 *
 * This structure represents an audio source element.
 */
struct mpipe_aud_src {
	struct mpipe_src src;
	int (*get_audio_caps)(const struct device *dev, struct audio_caps *caps);
};

void mpipe_aud_src_update_caps(struct mpipe_src *src);

/**
 * @brief Initialize an audio source element
 *
 *
 * This function initializes the audio source element with default
 * values and sets up the function pointers.
 *
 * @param self Pointer to the mpipe_element structure to be initialized as an
 *             audio source element.
 */
void mpipe_aud_src_init(struct mpipe_element *self);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_SRC_H_ */
