/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Audio definitions and utilities header file.
 */

#ifndef __MP_AUD_H__
#define __MP_AUD_H__

/**
 * @defgroup mp_aud aud
 * @ingroup mp_plugins
 * @brief Audio plugin elements, properties, and utility APIs.
 */

/**
 * @defgroup mp_aud_utils Utilities
 * @ingroup mp_aud
 * @brief Audio definitions and utility helpers.
 * @{
 */

#include <zephyr/audio/audio_caps.h>

/**
 * @brief Supported sample rates (Hz)
 * @{
 */
/** @brief 8 kHz sample rate */
#define MP_AUD_SAMPLE_RATE_8000  8000
/** @brief 16 kHz sample rate */
#define MP_AUD_SAMPLE_RATE_16000 16000
/** @brief 32 kHz sample rate */
#define MP_AUD_SAMPLE_RATE_32000 32000
/** @brief 44.1 kHz sample rate */
#define MP_AUD_SAMPLE_RATE_44100 44100
/** @brief 48 kHz sample rate */
#define MP_AUD_SAMPLE_RATE_48000 48000
/** @brief 96 kHz sample rate */
#define MP_AUD_SAMPLE_RATE_96000 96000
/** @} */

/**
 * @brief Supported bit widths
 * @{
 */
/** @brief 16 bit width */
#define MP_AUD_BIT_WIDTH_16 16
/** @brief 24 bit width */
#define MP_AUD_BIT_WIDTH_24 24
/** @brief 32 bit width */
#define MP_AUD_BIT_WIDTH_32 32
/** @} */

/**
 * @brief Convert an audio sample rate mask to a sample rate
 *
 * @param sample_rate_mask Audio driver sample rate mask
 * @return Sample rate in Hz, or 0 if the mask is unknown
 */
uint32_t audio2mp_sample_rate(uint32_t sample_rate_mask);

/**
 * @brief Convert an audio bit width mask to a bit width
 *
 * @param bit_width_mask Audio driver bit width mask
 * @return Bit width, or 0 if the mask is unknown
 */
uint32_t audio2mp_bit_width(uint32_t bit_width_mask);

/** @} */

#endif /* __MP_AUD_H__ */
