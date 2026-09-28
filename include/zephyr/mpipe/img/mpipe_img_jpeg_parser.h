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
 * Accumulates incoming data until a complete JPEG frame (SOI to EOI)
 * is assembled, then pushes it downstream as a single buffer.
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
 * @brief JPEG parser property identifiers.
 *
 * Extends the base parser properties defined in @ref mpipe_prop_parser.
 */
enum mpipe_prop_img_jpeg_parser {
	/**
	 * Accumulate the byte stream in the parser's own buffers (bool).
	 *
	 * Settable while the element is in @ref MPIPE_STATE_READY and applied by
	 * the buffer pool negotiation of the next READY to PAUSED transition,
	 * since it decides which pool the element upstream receives into.
	 * Setting it later returns -EBUSY, and asking for it on a build whose
	 * pool cannot serve it returns -ENOTSUP.
	 *
	 */
	MPIPE_PROP_IMG_JPEG_PARSER_ACCUMULATE_UPSTREAM = MPIPE_PROP_PARSER_LAST,
};

/**
 * @brief JPEG stream parser element.
 *
 * Extends @ref mpipe_parser to reassemble JPEG frames from a byte stream by default.
 *
 * When setting with the propoerty MPIPE_PROP_IMG_JPEG_PARSER_ACCUMULATE_UPSTREAM it the parser
 * offers its own pool upstream and hands the producer back the very buffer it is still accumulating
 * into, @ref accum_buf, so the producer appends after the bytes already there. A frame split across
 * several reads is then completed where it already sits rather than being reassembled piece by
 * piece, and the only bytes ever moved are the head of an unterminated frame,
 * carried to the front of the buffer once the frames before it are out.
 */
struct mpipe_img_jpeg_parser {
	/** Base parser element */
	struct mpipe_parser base;
	/** Partial frame buffer, accumulated with memcpy until EOI */
	struct net_buf *partial_frame;
	/** Output pool used when downstream pool is not available */
	struct mpipe_buffer_pool out_pool;
	/** Buffer the byte stream is being accumulated into, or NULL */
	struct net_buf *accum_buf;
	/** First byte of @ref accum_buf not looked at yet. */
	uint32_t scan_offset;
	/**
	 * Accumulate the byte stream in @ref accum_buf rather than reassembling
	 * frames through @ref partial_frame.
	 */
	bool accumulate_upstream;
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
