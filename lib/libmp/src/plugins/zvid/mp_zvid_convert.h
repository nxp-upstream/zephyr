/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Software pixel format / colorspace conversion element (videoconvert-like).
 *
 * This element is intended for pixel format conversion only (colorspace conversion,
 * subsampling, dithering). No scaling/rotation.
 *
 * Currently supported conversions:
 *  - NV12 -> RGB565
 *  - XRGB32 -> ARGB32
 *  - ARGB32 -> XRGB32
 */

#ifndef __MP_ZVID_CONVERT_H__
#define __MP_ZVID_CONVERT_H__

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>

#include <src/core/mp_buffer.h>
#include <src/core/mp_caps.h>
#include <src/core/mp_transform.h>

#define MP_ZVID_CONVERT(self) ((struct mp_zvid_convert *)self)

struct mp_zvid_convert_desc;

struct mp_zvid_convert {
	struct mp_transform transform;

	uint16_t width;
	uint16_t height;
	uint32_t in_pixfmt;
	uint32_t out_pixfmt;

	/* Internal output pool that allocates video_buffer-backed memory */
	struct mp_buffer_pool out_pool;

	/* Pre-allocated buffers returned by out_pool.acquire_buffer */
	struct k_fifo free_fifo;
	struct video_buffer *vbufs[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX];
	uint8_t vbuf_count;

	/* Selected conversion entry based on negotiated caps */
	const struct mp_zvid_convert_desc *desc;

	/* Internal implementation hooks (shared with table code) */
	struct {
		void (*nv12_to_rgb565)(uint16_t width, uint16_t height, const uint8_t *y_plane,
				       const uint8_t *uv_plane, uint16_t *rgb);
	} impl;
};

void mp_zvid_convert_init(struct mp_element *self);

#endif /* __MP_ZVID_CONVERT_H__ */
