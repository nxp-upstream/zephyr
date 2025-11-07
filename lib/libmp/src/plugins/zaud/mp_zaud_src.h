/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Audio source element header file.
 */

#ifndef __MP_ZAUD_SRC_H__
#define __MP_ZAUD_SRC_H__

#include <zephyr/device.h>

#include <src/core/mp_src.h>

#include "mp_zaud.h"
#include "mp_zaud_buffer_pool.h"

/** @brief Cast object pointer to mp_zaud_src pointer */
#define MP_ZAUD_SRC(self) ((mp_zaud_src *)self)

/**
 * @struct mp_zaud_src
 * @brief Audio source element structure
 *
 * This structure represents an audio source element.
 */
typedef struct {
	MpSrc src;
	int (*get_audio_caps)(const struct device *dev, struct audio_caps *caps);
} mp_zaud_src;

/**
 * @brief Initialize an audio source element
 *
 *
 * This function initializes the audio source element with default
 * values and sets up the function pointers.
 *
 * @param self Pointer to the MpElement structure to be initialized as an
 *             audio source element.
 */
void mp_zaud_src_init(MpElement *self);

#endif /* __MP_ZAUD_SRC_H__ */
