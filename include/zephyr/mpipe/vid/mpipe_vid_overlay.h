/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mpipe_vid_overlay
 * @brief Rectangle overlay transform element.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_OVERLAY_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_OVERLAY_H_

/**
 * @defgroup mpipe_vid_overlay Overlay
 * @ingroup mpipe_vid
 * @brief Draw rectangle outlines into video frames in place.
 *
 * The overlay is an in-place transform: it draws the outlines of a set of
 * rectangles into each frame passing through and forwards the same buffer.
 * The application replaces the set at any time with
 * @ref MPIPE_PROP_VID_OVERLAY_RECTS; the element copies it, so the caller's
 * array may be reused once the call returns, and a frame is always drawn with
 * one complete set.
 *
 * Drawing modifies the buffer. Behind a tee, link the branches that must see
 * the frame unmodified before the one holding the overlay, and put a queue in
 * front of the overlay: the tee then hands the buffer to the overlay only once
 * the earlier branches have returned it.
 *
 * Supported pixel formats: RGB565, RGB565X, BGR24, RGB24, XRGB32 and ARGB32.
 * @{
 */

#include <stdint.h>

#include <zephyr/spinlock.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>
#include <zephyr/mpipe/vid/mpipe_vid_property.h>

/** @brief Overlay property identifiers, following the vid properties */
enum mpipe_prop_vid_overlay {
	/**
	 * Rectangles to draw, a const struct mpipe_vid_overlay_rects pointer.
	 * At most CONFIG_MPIPE_VID_OVERLAY_MAX_RECTS are kept.
	 */
	MPIPE_PROP_VID_OVERLAY_RECTS = MPIPE_PROP_VID_CROP + 1,
};

/** @brief One rectangle outline, in pixels of the frame */
struct mpipe_vid_overlay_rect {
	/** Left column, inclusive */
	uint16_t left;
	/** Top row, inclusive */
	uint16_t top;
	/** Right column, inclusive */
	uint16_t right;
	/** Bottom row, inclusive */
	uint16_t bottom;
	/** Outline width in pixels, drawn inwards */
	uint8_t line_width;
	/** Outline color as 0xRRGGBB */
	uint32_t color;
};

/** @brief A set of rectangles, the value of @ref MPIPE_PROP_VID_OVERLAY_RECTS */
struct mpipe_vid_overlay_rects {
	/** Rectangles to draw */
	const struct mpipe_vid_overlay_rect *rects;
	/** Number of entries in @p rects */
	uint8_t count;
};

/** @brief Rectangle overlay element */
struct mpipe_vid_overlay {
	/** Base transform element */
	struct mpipe_transform transform;
	/** Negotiated frame width in pixels */
	uint16_t width;
	/** Negotiated frame height in pixels */
	uint16_t height;
	/** Negotiated pixel format (VIDEO_PIX_FMT_*) */
	uint32_t pixfmt;
	/** Protects @p rects and @p count */
	struct k_spinlock lock;
	/** Rectangles drawn into the next frames */
	struct mpipe_vid_overlay_rect rects[CONFIG_MPIPE_VID_OVERLAY_MAX_RECTS];
	/** Valid entries in @p rects */
	uint8_t count;
	/** Downstream buffer pool requirement, relayed to upstream */
	struct mpipe_buffer_pool_config alloc_cfg;
};

/**
 * @brief Initialize a rectangle overlay element.
 *
 * @param overlay Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 */
int mpipe_vid_overlay_init(struct mpipe_vid_overlay *overlay, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_OVERLAY_H_ */
