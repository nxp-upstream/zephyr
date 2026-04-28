/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MP_ZVID_CONVERT_TABLE_H__
#define __MP_ZVID_CONVERT_TABLE_H__

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/video.h>

struct net_buf;

struct mp_zvid_convert;

/* Conversion function signature: converts one frame */
typedef int (*mp_zvid_convert_fn_t)(struct mp_zvid_convert *conv, const struct net_buf *in,
				   struct net_buf *out);

struct mp_zvid_convert_desc {
	uint32_t in_pixfmt;
	uint32_t out_pixfmt;
	mp_zvid_convert_fn_t fn;
};

/*
 * Supported conversions table.
 * Keep this list small and explicit; add formats as needed.
 */
extern const struct mp_zvid_convert_desc mp_zvid_convert_descs[];
extern const size_t mp_zvid_convert_descs_len;

#endif /* __MP_ZVID_CONVERT_TABLE_H__ */
