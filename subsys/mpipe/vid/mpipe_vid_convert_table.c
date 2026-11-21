/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>

#include <zephyr/mpipe/vid/mpipe_vid_convert.h>
#include <zephyr/mpipe/vid/mpipe_vid_convert_table.h>

static int convert_nv12_to_rgb565(struct mpipe_vid_convert *conv, const struct net_buf *in,
				  struct net_buf *out)
{
	const uint8_t *y_plane = in->data;
	const uint8_t *uv_plane = in->data + ((uint32_t)conv->width * (uint32_t)conv->height);

	conv->impl.nv12_to_rgb565(conv->width, conv->height, y_plane, uv_plane,
				  (uint16_t *)out->data);

	return 0;
}

static int convert_xrgb_to_argb(struct mpipe_vid_convert *conv, const struct net_buf *in,
				struct net_buf *out)
{
	uint32_t pixels = (uint32_t)conv->width * (uint32_t)conv->height;
	const uint8_t *src = in->data;
	uint8_t *dst = out->data;

	for (uint32_t i = 0; i < pixels; i++) {
		dst[0] = 0xFF; /* A */
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
		src += 4;
		dst += 4;
	}

	return 0;
}

static int convert_argb_to_xrgb(struct mpipe_vid_convert *conv, const struct net_buf *in,
				struct net_buf *out)
{
	uint32_t pixels = (uint32_t)conv->width * (uint32_t)conv->height;
	const uint8_t *src = in->data;
	uint8_t *dst = out->data;

	for (uint32_t i = 0; i < pixels; i++) {
		dst[0] = 0xFF; /* X */
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
		src += 4;
		dst += 4;
	}

	return 0;
}

/*
 * BT.601 with the limited range video codecs work with: 16 to 235 for luma,
 * 16 to 240 for chroma. Coefficients are scaled by 256.
 */

/*
 * How far ahead of the pixels being read the source is fetched, in bytes: a
 * frame that a camera wrote lies in no cache, and in a slow memory the loads
 * of the conversion would wait for one cache line after the other.
 */
#define PIXEL32_FETCH_AHEAD  128U
/* Pixels of the source in one cache line of 32 bytes */
#define PIXEL32_FETCH_PIXELS 8U

/* Luma of a BGRX32 pixel, whose components are added to the sums of its block */
static inline uint8_t bgrx32_to_luma(const uint8_t *pixel, int32_t *r_sum, int32_t *g_sum,
				     int32_t *b_sum)
{
	int32_t b = pixel[0];
	int32_t g = pixel[1];
	int32_t r = pixel[2];

	*r_sum += r;
	*g_sum += g;
	*b_sum += b;

	return (uint8_t)((((66 * r) + (129 * g) + (25 * b) + 128) >> 8) + 16);
}

/* Offsets of the components of a VUYX32 pixel */
#define VUYX32_V 0U
#define VUYX32_U 1U
#define VUYX32_Y 2U

/* The luma as it is, and one chroma sample for every block of two by two pixels: their mean */
static int convert_vuyx32_to_yuv420(struct mpipe_vid_convert *conv, const struct net_buf *in,
				    struct net_buf *out)
{
	uint32_t width = conv->width;
	uint32_t height = conv->height;
	const uint8_t *top = in->data;
	uint8_t *y_top = out->data;
	uint8_t *u_plane = y_top + (width * height);
	uint8_t *v_plane = u_plane + ((width / 2U) * (height / 2U));

	if ((width % 2U) != 0U || (height % 2U) != 0U) {
		return -EINVAL;
	}

	for (uint32_t row = 0U; row < height; row += 2U) {
		const uint8_t *bottom = top + (4U * width);
		uint8_t *y_bottom = y_top + width;

		for (uint32_t col = 0U; col < width; col += 2U) {
			if ((col % PIXEL32_FETCH_PIXELS) == 0U) {
				__builtin_prefetch(top + PIXEL32_FETCH_AHEAD);
				__builtin_prefetch(bottom + PIXEL32_FETCH_AHEAD);
			}

			y_top[0] = top[VUYX32_Y];
			y_top[1] = top[4U + VUYX32_Y];
			y_bottom[0] = bottom[VUYX32_Y];
			y_bottom[1] = bottom[4U + VUYX32_Y];
			*u_plane = (uint8_t)((top[VUYX32_U] + top[4U + VUYX32_U] +
					      bottom[VUYX32_U] + bottom[4U + VUYX32_U] + 2U) / 4U);
			*v_plane = (uint8_t)((top[VUYX32_V] + top[4U + VUYX32_V] +
					      bottom[VUYX32_V] + bottom[4U + VUYX32_V] + 2U) / 4U);

			top += 8U;
			bottom += 8U;
			y_top += 2U;
			y_bottom += 2U;
			u_plane++;
			v_plane++;
		}

		/* The inner loop went through the top row: skip the bottom one */
		top = bottom;
		y_top = y_bottom;
	}

	return 0;
}

static int convert_bgrx32_to_yuv420(struct mpipe_vid_convert *conv, const struct net_buf *in,
				    struct net_buf *out)
{
	uint32_t width = conv->width;
	uint32_t height = conv->height;
	const uint8_t *top = in->data;
	uint8_t *y_top = out->data;
	uint8_t *u_plane = y_top + (width * height);
	uint8_t *v_plane = u_plane + ((width / 2U) * (height / 2U));

	if ((width % 2U) != 0U || (height % 2U) != 0U) {
		return -EINVAL;
	}

	/* One chroma sample for every block of two by two pixels, from their sum */
	for (uint32_t row = 0U; row < height; row += 2U) {
		const uint8_t *bottom = top + (4U * width);
		uint8_t *y_bottom = y_top + width;

		for (uint32_t col = 0U; col < width; col += 2U) {
			int32_t r = 0;
			int32_t g = 0;
			int32_t b = 0;

			if ((col % PIXEL32_FETCH_PIXELS) == 0U) {
				__builtin_prefetch(top + PIXEL32_FETCH_AHEAD);
				__builtin_prefetch(bottom + PIXEL32_FETCH_AHEAD);
			}

			y_top[0] = bgrx32_to_luma(&top[0], &r, &g, &b);
			y_top[1] = bgrx32_to_luma(&top[4], &r, &g, &b);
			y_bottom[0] = bgrx32_to_luma(&bottom[0], &r, &g, &b);
			y_bottom[1] = bgrx32_to_luma(&bottom[4], &r, &g, &b);

			/* Four pixels in the sums: two more bits to shift out */
			*u_plane = (uint8_t)((((112 * b) - (38 * r) - (74 * g) + 512) >> 10) + 128);
			*v_plane = (uint8_t)((((112 * r) - (94 * g) - (18 * b) + 512) >> 10) + 128);

			top += 8U;
			bottom += 8U;
			y_top += 2U;
			y_bottom += 2U;
			u_plane++;
			v_plane++;
		}

		/* The inner loop went through the top row: skip the bottom one */
		top = bottom;
		y_top = y_bottom;
	}

	return 0;
}

static inline uint16_t yuv_to_rgb565(uint8_t y, int32_t r_add, int32_t g_add, int32_t b_add)
{
	int32_t luma = 298 * ((int32_t)y - 16);
	uint32_t r = (uint32_t)CLAMP((luma + r_add) >> 8, 0, 255);
	uint32_t g = (uint32_t)CLAMP((luma + g_add) >> 8, 0, 255);
	uint32_t b = (uint32_t)CLAMP((luma + b_add) >> 8, 0, 255);

	return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

static int convert_yuv420_to_rgb565(struct mpipe_vid_convert *conv, const struct net_buf *in,
				    struct net_buf *out)
{
	uint32_t width = conv->width;
	uint32_t height = conv->height;
	const uint8_t *y_top = in->data;
	const uint8_t *u_plane = y_top + (width * height);
	const uint8_t *v_plane = u_plane + ((width / 2U) * (height / 2U));
	uint16_t *dst_top = (uint16_t *)out->data;

	if ((width % 2U) != 0U || (height % 2U) != 0U) {
		return -EINVAL;
	}

	/* The chroma terms are shared by the two by two pixels of a block */
	for (uint32_t row = 0U; row < height; row += 2U) {
		const uint8_t *y_bottom = y_top + width;
		uint16_t *dst_bottom = dst_top + width;

		for (uint32_t col = 0U; col < width; col += 2U) {
			int32_t u = (int32_t)*u_plane - 128;
			int32_t v = (int32_t)*v_plane - 128;
			int32_t r_add = (409 * v) + 128;
			int32_t g_add = 128 - (100 * u) - (208 * v);
			int32_t b_add = (516 * u) + 128;

			dst_top[0] = yuv_to_rgb565(y_top[0], r_add, g_add, b_add);
			dst_top[1] = yuv_to_rgb565(y_top[1], r_add, g_add, b_add);
			dst_bottom[0] = yuv_to_rgb565(y_bottom[0], r_add, g_add, b_add);
			dst_bottom[1] = yuv_to_rgb565(y_bottom[1], r_add, g_add, b_add);

			y_top += 2U;
			y_bottom += 2U;
			dst_top += 2U;
			dst_bottom += 2U;
			u_plane++;
			v_plane++;
		}

		/* The inner loop went through the top row: skip the bottom one */
		y_top = y_bottom;
		dst_top = dst_bottom;
	}

	return 0;
}

const struct mpipe_vid_convert_desc mpipe_vid_convert_descs[] = {
	{VIDEO_PIX_FMT_NV12, VIDEO_PIX_FMT_RGB565, convert_nv12_to_rgb565},
	{VIDEO_PIX_FMT_XRGB32, VIDEO_PIX_FMT_ARGB32, convert_xrgb_to_argb},
	{VIDEO_PIX_FMT_ARGB32, VIDEO_PIX_FMT_XRGB32, convert_argb_to_xrgb},
	{VIDEO_PIX_FMT_BGRX32, VIDEO_PIX_FMT_YUV420, convert_bgrx32_to_yuv420},
	{VIDEO_PIX_FMT_VUYX32, VIDEO_PIX_FMT_YUV420, convert_vuyx32_to_yuv420},
	{VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, convert_yuv420_to_rgb565},
};

const size_t mpipe_vid_convert_descs_len = ARRAY_SIZE(mpipe_vid_convert_descs);
