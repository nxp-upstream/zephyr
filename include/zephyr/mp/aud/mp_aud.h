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

#include <zephyr/mp/mp_structure.h>

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

/**
 * @brief Number of bit widths a device mask advertises.
 *
 * A device describes its sample rates and bit widths as two masks, and every
 * combination of them is a capability of its own. Enumeration walks all
 * combinations, splitting a flat index with this count.
 *
 * @param bit_width_mask Bit width mask reported by the device.
 *
 * @return Number of bit widths that map to a known one
 */
uint32_t mp_aud_count_bit_widths(uint32_t bit_width_mask);

/**
 * @brief Sample rate at @p n among those a device mask advertises.
 *
 * @param sample_rate_mask Sample rate mask reported by the device.
 * @param n Zero-based index among the rates that map to a known one.
 *
 * @return Sample rate, or 0 if there are fewer than @p n + 1 of them
 */
uint32_t mp_aud_nth_sample_rate(uint32_t sample_rate_mask, uint32_t n);

/**
 * @brief Bit width at @p n among those a device mask advertises.
 *
 * @param bit_width_mask Bit width mask reported by the device.
 * @param n Zero-based index among the widths that map to a known one.
 *
 * @return Bit width, or 0 if there are fewer than @p n + 1 of them
 */
uint32_t mp_aud_nth_bit_width(uint32_t bit_width_mask, uint32_t n);

/**
 * @brief Read a fixed unsigned field that a capability is required to carry.
 *
 * Audio elements configure their device from several caps fields at once and
 * cannot proceed if one is missing, so this reports the absent field rather
 * than leaving the caller to dereference nothing.
 *
 * @param caps Pointer to the capability to read from.
 * @param field_id Field identifier, see @ref mp_caps_field.
 * @param out Pointer to storage for the value, untouched unless 0 is returned.
 *
 * @retval 0 on success
 * @retval -ENOENT if the capability does not carry the field
 * @retval -EINVAL if @p out is NULL or the field is not a fixed unsigned value
 */
int mp_aud_get_uint(const struct mp_structure *caps, uint8_t field_id, uint32_t *out);

/** @} */

#endif /* __MP_AUD_H__ */
