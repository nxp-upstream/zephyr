/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mpipe_img_jpeg_parsers
 * @brief JPEG stream parser element.
 *
 * Accumulates incoming data until a complete JPEG frame (SOI … EOI)
 * is assembled, then pushes it downstream as a single buffer.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_IMG_MPIPE_IMG_JPEG_PARSER_H_
#define ZEPHYR_INCLUDE_MPIPE_IMG_MPIPE_IMG_JPEG_PARSER_H_

/**
 * @defgroup mpipe_img img
 * @ingroup mpipe_plugins
 * @brief JPEG parser, decoder, and helper APIs.
 */

/**
 * @defgroup mpipe_img_jpeg_parsers Parsers
 * @ingroup mpipe_img
 * @brief JPEG parser elements.
 * @{
 */

#include <zephyr/mpipe/mpipe_parser.h>

/**
 * @brief JPEG stream parser element.
 *
 * Extends @ref mpipe_parser to reassemble JPEG frames from a byte stream.
 */
struct mpipe_img_jpeg_parser {
	/** Base parser element */
	struct mpipe_parser base;
	/** Partial frame buffer, accumulated with memcpy until EOI */
	struct net_buf *partial_frame;
	/** Output pool used when downstream pool is not available */
	struct mpipe_buffer_pool out_pool;
};

/**
 * @brief Initialize a JPEG stream parser element.
 *
 * @param self Pointer to the @ref mpipe_element to initialize.
 */
void mpipe_img_jpeg_parser_init(struct mpipe_element *self);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_IMG_MPIPE_IMG_JPEG_PARSER_H_ */
