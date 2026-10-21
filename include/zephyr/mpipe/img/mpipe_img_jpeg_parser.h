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
 * Cuts a byte stream of concatenated JPEG frames at the end-of-image markers
 * and pushes each frame downstream as one buffer.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_IMG_MPIPE_IMG_JPEG_PARSER_H_
#define ZEPHYR_INCLUDE_MPIPE_IMG_MPIPE_IMG_JPEG_PARSER_H_

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
 * Extends @ref mpipe_parser. The input buffers are kept as they arrive until
 * an end-of-image marker completes a frame, and only the bytes that arrived
 * since the last call are searched. A frame that lies inside one input chunk
 * is emitted as a view of it (see @ref mpipe_buffer_view), so nothing is
 * copied; a frame split across two chunks is copied into the parser's own
 * pool. When the element downstream offers a pool of its own, every frame is
 * copied into that pool instead, since that element needs its memory.
 */
struct mpipe_img_jpeg_parser {
	/** Base parser element */
	struct mpipe_parser base;
	/** Pool for the frames that have to be copied */
	struct mpipe_buffer_pool out_pool;
	/** Input not yet cut into frames, one piece per input chunk */
	struct net_buf *pending;
	/** Bytes of @ref pending already searched for a marker */
	uint32_t scanned;
	/** Every frame is copied into the pool the element downstream offered */
	bool copy_out;
};

/**
 * @brief Initialize a JPEG stream parser element.
 *
 * @param jpeg_parser Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_img_jpeg_parser_init(struct mpipe_img_jpeg_parser *jpeg_parser, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_IMG_MPIPE_IMG_JPEG_PARSER_H_ */
