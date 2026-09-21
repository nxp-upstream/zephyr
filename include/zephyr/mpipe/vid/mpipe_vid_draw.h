/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Simple drawing helpers for video frames.
 * @ingroup mpipe_vid_overlay
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_DRAW_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_DRAW_H_

/**
 * @addtogroup mpipe_vid_overlay
 * @{
 */

#include <stdint.h>

/**
 * @brief A rectangle in pixel coordinates
 */
struct mpipe_vid_rect {
	/** Left edge */
	int32_t x0;
	/** Top edge */
	int32_t y0;
	/** Right edge */
	int32_t x1;
	/** Bottom edge */
	int32_t y1;
};

/**
 * @brief Draw the outline of a rectangle into a video frame
 *
 * Supported pixel formats: RGB565, BGRX32. Coordinates are clamped to the
 * frame.
 *
 * @param frame Frame pixels
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param pixfmt Frame pixel format, a VIDEO_PIX_FMT_* value
 * @param rect The rectangle
 * @param argb Color as 0xAARRGGBB; alpha is ignored
 * @param thickness Edge thickness in pixels
 *
 * @retval 0 Success
 * @retval -EINVAL @p frame or @p rect is NULL
 * @retval -ENOTSUP Unsupported pixel format
 */
int mpipe_vid_draw_rect(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			const struct mpipe_vid_rect *rect, uint32_t argb, uint8_t thickness);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_DRAW_H_ */
