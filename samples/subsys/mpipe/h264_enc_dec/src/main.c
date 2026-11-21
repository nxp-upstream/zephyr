/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The pipeline is described in pipeline.dtsi and built and played at boot.
 * The application only brings the test pattern.
 */

#include <zephyr/drivers/video.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#define PATTERN_BLOCK_SIZE 96U

void sample_draw_pattern(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			 const struct net_buf *in, void *user_data);

/*
 * The draw callback pattern.overlay names: a gradient, and a textured block
 * that moves and changes from frame to frame.
 */
void sample_draw_pattern(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			 const struct net_buf *in, void *user_data)
{
	static uint32_t n;
	uint32_t *pixel = (uint32_t *)frame;
	uint32_t block_x = (4U * n) % (width - PATTERN_BLOCK_SIZE);
	uint32_t block_y = (2U * n) % (height - PATTERN_BLOCK_SIZE);

	ARG_UNUSED(in);
	ARG_UNUSED(user_data);

	if (pixfmt != VIDEO_PIX_FMT_BGRX32) {
		return;
	}

	for (uint32_t y = 0U; y < height; y++) {
		for (uint32_t x = 0U; x < width; x++) {
			uint32_t grey = (x + (2U * y)) / 2U;

			if ((x - block_x) < PATTERN_BLOCK_SIZE &&
			    (y - block_y) < PATTERN_BLOCK_SIZE) {
				grey = ((4U * (x - block_x)) ^ (4U * (y - block_y))) + (4U * n);
			}

			*pixel++ = (grey & 0xFFU) * 0x010101U;
		}
	}

	n++;
}
