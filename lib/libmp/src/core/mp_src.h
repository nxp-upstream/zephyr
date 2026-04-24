/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_src.
 */

#ifndef __MP_SRC_H__
#define __MP_SRC_H__

#include "mp_buffer.h"
#include "mp_element.h"
#include "mp_pad.h"

#define MP_SRC(self) ((struct mp_src *)self)

/**
 * @brief Base Source Element Structure
 *
 * The source element is responsible for generating data and pushing it downstream.
 * It contains a source pad for output.
 */
struct mp_src {
	/** Base element structure */
	struct mp_element element;
	/** Source pad for output data */
	struct mp_pad srcpad;
	/** @cond INTERNAL_HIDDEN */
	/** Pointer to the supported caps */
	struct mp_caps *src_caps;
	/** @endcond */
	/** Buffer pool for managing output buffers */
	struct mp_buffer_pool *pool;
	/**
	 * Number of buffers that the source outputs before sending EOS;
	 * 0 means will run forever
	 */
	uint32_t num_buffers;
	/** 
	 * Get the supported caps of the element.
	 * To get the current caps, use srcpad->caps instead
	 */
	struct mp_caps *(*get_caps)(struct mp_src *src);
	/** Set a given caps to the source pad */
	bool (*set_caps)(struct mp_src *src, struct mp_caps *caps);
	/** Decide buffer allocation strategy for the downstream peer */
	bool (*decide_allocation)(struct mp_src *self, struct mp_query *query);
};

/**
 * @brief Initialize a source element
 *
 * This function initializes the base source element structure, including
 * the source pad and default callbacks.
 *
 * @param self Pointer to the @ref struct mp_element to initialize as a source
 */
void mp_src_init(struct mp_element *self);

/**
 * @brief Set property on source element
 *
 * @param obj Pointer to the @ref struct mp_object (source element)
 * @param key Property key identifier
 * @param val Pointer to the property value to set
 * @return 0 on success, negative error code on failure
 */
int mp_src_set_property(struct mp_object *obj, uint32_t key, const void *val);

/**
 * @brief Get property from source element
 *
 * @param obj Pointer to the @ref struct mp_object (source element)
 * @param key Property key identifier
 * @param val Pointer to store the retrieved property value
 * @return 0 on success, negative error code on failure
 */
int mp_src_get_property(struct mp_object *obj, uint32_t key, void *val);

/**
 * @brief Update the capabilities of a source element
 *
 * Updates the source element's supported caps
 *
 * @param src Pointer to the source element
 * @param caps Supported caps for the src pad
 */
void mp_src_update_caps(struct mp_src *src, struct mp_caps *caps);

#endif /* __MP_SRC_H__ */
