/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/video.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/vid/mpipe_vid_draw.h>

/* One pixel of the target format, prepared once per drawing call */
struct vid_draw_px {
	uint8_t bytes[4];
	uint8_t size;
};

static int vid_draw_pack(uint32_t pixfmt, uint32_t argb, struct vid_draw_px *px)
{
	uint8_t r = (uint8_t)(argb >> 16);
	uint8_t g = (uint8_t)(argb >> 8);
	uint8_t b = (uint8_t)argb;

	switch (pixfmt) {
	case VIDEO_PIX_FMT_RGB565: {
		uint16_t v = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));

		px->bytes[0] = (uint8_t)v;
		px->bytes[1] = (uint8_t)(v >> 8);
		px->size = 2;

		return 0;
	}
	case VIDEO_PIX_FMT_BGRX32:
		px->bytes[0] = b;
		px->bytes[1] = g;
		px->bytes[2] = r;
		px->bytes[3] = 0;
		px->size = 4;

		return 0;
	default:
		return -ENOTSUP;
	}
}

static void vid_draw_hline(uint8_t *frame, uint16_t width, uint16_t height,
			   const struct vid_draw_px *px, int32_t x0, int32_t x1, int32_t y)
{
	if (y < 0 || y >= (int32_t)height) {
		return;
	}

	x0 = CLAMP(x0, 0, (int32_t)width - 1);
	x1 = CLAMP(x1, 0, (int32_t)width - 1);

	for (int32_t x = x0; x <= x1; x++) {
		uint8_t *dst = &frame[((uint32_t)y * width + (uint32_t)x) * px->size];

		for (uint8_t i = 0; i < px->size; i++) {
			dst[i] = px->bytes[i];
		}
	}
}

static void vid_draw_vline(uint8_t *frame, uint16_t width, uint16_t height,
			   const struct vid_draw_px *px, int32_t x, int32_t y0, int32_t y1)
{
	if (x < 0 || x >= (int32_t)width) {
		return;
	}

	y0 = CLAMP(y0, 0, (int32_t)height - 1);
	y1 = CLAMP(y1, 0, (int32_t)height - 1);

	for (int32_t y = y0; y <= y1; y++) {
		uint8_t *dst = &frame[((uint32_t)y * width + (uint32_t)x) * px->size];

		for (uint8_t i = 0; i < px->size; i++) {
			dst[i] = px->bytes[i];
		}
	}
}

int mpipe_vid_draw_rect(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			const struct mpipe_vid_rect *rect, uint32_t argb, uint8_t thickness)
{
	struct vid_draw_px px;
	int ret;

	if (frame == NULL || rect == NULL) {
		return -EINVAL;
	}

	ret = vid_draw_pack(pixfmt, argb, &px);
	if (ret != 0) {
		return ret;
	}

	for (uint8_t t = 0; t < thickness; t++) {
		vid_draw_hline(frame, width, height, &px, rect->x0, rect->x1, rect->y0 + t);
		vid_draw_hline(frame, width, height, &px, rect->x0, rect->x1, rect->y1 - t);
		vid_draw_vline(frame, width, height, &px, rect->x0 + t, rect->y0, rect->y1);
		vid_draw_vline(frame, width, height, &px, rect->x1 - t, rect->y0, rect->y1);
	}

	return 0;
}
