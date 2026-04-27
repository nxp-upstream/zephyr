/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief mp_zjpeg_decoder (software JPEG decoder)
 */

#ifndef __MP_ZJPEG_DECODER_H__
#define __MP_ZJPEG_DECODER_H__

#include <src/core/mp_transform.h>

#include "JPEGDEC.h"

#define MP_ZJPEG_DECODER(self) ((struct mp_zjpeg_decoder *)self)

struct mp_zjpeg_decoder {
	/** Base transform */
	struct mp_transform transform;

	/** JPEG decoder state */
	JPEGIMAGE jpg;

	/** Output pixel format (VIDEO_PIX_FMT_*) */
	uint32_t out_pixfmt;

	/** Internal output pool (fallback when downstream does not propose a pool) */
	struct mp_buffer_pool out_pool;
};

void mp_zjpeg_decoder_init(struct mp_element *self);


#endif /* __MP_ZJPEG_DECODER_H__ */
