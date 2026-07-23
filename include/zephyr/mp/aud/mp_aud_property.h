/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Audio property definitions header file.
 */

#ifndef __MP_AUD_PROPS_H__
#define __MP_AUD_PROPS_H__

/**
 * @defgroup mp_aud_properties Properties
 * @ingroup mp_aud
 * @brief Audio property identifiers for aud elements.
 * @{
 */

#include <zephyr/mp/mp_property.h>

/**
 * @brief Audio transform property identifiers
 *
 * Enumeration defining property IDs specific to transform elements.
 * These properties extend the base transform properties.
 */
enum mp_prop_aud_transform {
	/** Gain control property for audio level adjustment */
	MP_PROP_AUD_TRANSFORM_GAIN = MP_PROP_TRANSFORM_LAST,
};

/**
 * @brief Audio source property identifiers
 *
 * Enumeration defining property IDs specific to source elements.
 * These properties extend the base source properties.
 */
enum mp_prop_aud_src {
	/** Pointer to source memory slab for audio buffer management */
	MP_PROP_AUD_SRC_SLAB_PTR = MP_PROP_SRC_LAST,
	/** Audio source device */
	MP_PROP_AUD_SRC_DEVICE,
};

/**
 * @brief Audio sink property identifiers
 *
 * Enumeration defining property IDs specific to sink elements.
 * These properties extend the base sink properties.
 */
enum mp_prop_aud_sink {
	/** Pointer to sink memory slab for audio buffer management */
	MP_PROP_AUD_SINK_SLAB_PTR = MP_PROP_SINK_LAST,
	/** Clock role configuration for audio sink (controller/target) */
	MP_PROP_AUD_SINK_CLK_ROLE,
	/** I2S (SAI) sink device */
	MP_PROP_AUD_SINK_I2S_DEVICE,
	/** Codec sink device */
	MP_PROP_AUD_SINK_CODEC_DEVICE,
};

/** @} */

#endif /* __MP_AUD_PROPS_H__ */
