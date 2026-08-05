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
 * @brief Produce the capability a device advertises at an enumeration index.
 *
 * A device describes its sample rates and bit widths as two masks, and every
 * combination of them is a capability of its own. The enumeration index spans
 * both, so each capability is a plain fixed structure instead of one structure
 * holding two list values. An element whose capability comes from more than one
 * device passes what the devices agree on.
 *
 * @param caps Audio capabilities to enumerate.
 * @param index Zero-based enumeration index.
 * @param filter Capability the result must satisfy, may be NULL.
 * @param out Pointer to storage for the capability at @p index.
 *
 * @retval 0 on success
 * @retval -EAGAIN if the capability at @p index cannot satisfy @p filter
 * @retval -ENOENT if @p index is past the last capability
 * @retval -EINVAL if @p caps or @p out is NULL
 */
int mp_aud_enum_caps(const struct audio_caps *caps, uint32_t index,
		     const struct mp_structure *filter, struct mp_structure *out);

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
int mp_aud_caps_get_uint(const struct mp_structure *caps, uint8_t field_id, uint32_t *out);

/** @} */

#endif /* __MP_AUD_H__ */
