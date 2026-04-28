/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MP_PARSER_H__
#define __MP_PARSER_H__

#include <zephyr/mp/core/mp_buffer.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_pad.h>

#define MP_PARSER(self) ((struct mp_parser *)self)

struct mp_parser {
	/** Base element */
	struct mp_element element;
	/** Sink pad to receive data */
	struct mp_pad sinkpad;
	/** Source pad to send data */
	struct mp_pad srcpad;
	/** @cond INTERNAL_HIDDEN */
	/** Pointer to the supported caps at sink */
	struct mp_caps *sink_caps;
	/** Pointer to the supported caps at source */
	struct mp_caps *src_caps;
	/** @endcond */
	/** Pointer to the output buffer pool */
	struct mp_buffer_pool *outpool;

	/**
	 * @brief Get the supported caps from an element's pad.
	 *
	 * To get the current caps, use sinkpad->caps or srcpad->caps instead
	 *
	 * @param parser Pointer to the parser element
	 * @param direction Pad direction (@ref mp_pad_direction)
	 * @return Pointer to supported caps, or NULL on failure
	 */
	struct mp_caps *(*get_caps)(struct mp_parser *parser, enum mp_pad_direction direction);
	/**
	 * @brief Set a given caps to an element's pad
	 *
	 * @param parser Pointer to the parser element
	 * @param direction Pad direction (@ref mp_pad_direction)
	 * @param caps Pointer to the caps structure to set
	 * @return true on success, false on failure
	 */
	bool (*set_caps)(struct mp_parser *parser, enum mp_pad_direction direction,
			 struct mp_caps *caps);
	/**
	 * @brief Propose allocation parameters to upstream
	 * @param self Pointer to the transform element
	 * @param query Allocation query (@ref struct mp_query)
	 * @return True on success, false on failure
	 */
	bool (*propose_allocation)(struct mp_parser *parser, struct mp_query *query);
	/**
	 * @brief Decide allocation parameters for downstream
	 * @param self Pointer to the transform element
	 * @param query Allocation query (@ref struct mp_query)
	 * @return true on success, false on failure
	 */
	bool (*decide_allocation)(struct mp_parser *parser, struct mp_query *query);
};

void mp_parser_update_caps(struct mp_parser *parser, struct mp_caps *sink_caps,
			   struct mp_caps *src_caps);

void mp_parser_init(struct mp_element *self);

#endif
