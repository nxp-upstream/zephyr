/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief mp_zjpeg_parser
 */

#ifndef __MP_ZJPEG_PARSER_H__
#define __MP_ZJPEG_PARSER_H__

#include <src/core/mp_parser.h>
#include <src/core/mp_buffer.h>

#define MP_ZJPEG_PARSER(self) ((struct mp_zjpeg_parser *)self)

struct mp_zjpeg_parser {
	/** Base structure for parser */
	struct mp_parser base;

	/** Partial frame buffer, accumulated with memcpy until EOI */
	struct net_buf *partial_frame;

	/** Staging pool used when downstream pool is not available (e.g. preroll/discovery). */
	struct mp_buffer_pool staging_pool;
};

void mp_zjpeg_parser_init(struct mp_element *self);

#endif /* __MP_ZJPEG_PARSER_H__ */
