/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_sink.
 */

#ifndef ZEPHYR_INCLUDE_MP_ZBASE_MP_APPSINK_H_
#define ZEPHYR_INCLUDE_MP_ZBASE_MP_APPSINK_H_

/**
 * @defgroup mp_sink Sinks
 * @ingroup mp_core
 * @brief Terminal elements that consume data from a pipeline.
 *
 * @{
 */

#include <zephyr/mp/mp_sink.h>

/**
 * @brief Sink Element Structure
 *
 * Represents a sink element in the media pipeline. Sink elements are terminal
 * elements that consume data from upstream elements through their sink pad.
 */
struct mp_appsink {
	/** Base element structure */
	struct mp_sink sink;
	/** Callback invoked upon every frame completion */
	int (*hook)(struct mp_element *element, uint8_t pad_idx);
	/** Extra data structure for use with the frame hook */
	void *app_data;
};

/** @brief Properties for appsink elements */
enum mp_appsink_prop {
	/** Frame Hook callback pointer */
	MP_APPSINK_PROP_HOOK = MP_PROP_SINK_LAST,
	/** For use by elements based on appsink */
	MP_APPSINK_PROP_LAST,
};

/**
 * @brief Initialize an appsink element
 *
 * Initializes the base appsink element structure, based on the struct mp_sink,
 * and configures default callbacks.
 *
 * @param self Pointer to the @ref mp_element to initialize as a sink
 */
void mp_appsink_init(struct mp_element *self);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_ZBASE_MP_APPSINK_H_ */
