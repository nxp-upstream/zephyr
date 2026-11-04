/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Video overlay element.
 * @ingroup mpipe_vid_overlay
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_OVERLAY_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_OVERLAY_H_

/**
 * @defgroup mpipe_vid_overlay Overlays
 * @ingroup mpipe_vid
 * @brief Application drawing on video frames.
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/video.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>

/**
 * @brief Overlay draw callback
 *
 * Runs in the thread driving the overlay element, once per input buffer,
 * with the frame already copied - draw directly into it. What is drawn is
 * entirely the application's business: detection boxes, counters, marks.
 * Keep it short; the display branch waits for it.
 *
 * @param frame Frame pixels to draw into: the video frame in
 *              @ref MPIPE_VID_OVERLAY_MODE_FRAME, a transparent BGRA32 plane
 *              in @ref MPIPE_VID_OVERLAY_MODE_PLANE
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param pixfmt Frame pixel format, a VIDEO_PIX_FMT_* value
 * @param in The input buffer: the frame itself in frame mode, whatever the
 *           element received in plane mode (a video frame, detection results)
 * @param user_data The pointer registered with the callback
 */
typedef void (*mpipe_vid_overlay_draw_cb_t)(uint8_t *frame, uint16_t width, uint16_t height,
					    uint32_t pixfmt, const struct net_buf *in,
					    void *user_data);

/**
 * @brief Callback registration for MPIPE_PROP_VID_OVERLAY_DRAW_CB
 */
struct mpipe_vid_overlay_cb {
	/** Function to call for every frame */
	mpipe_vid_overlay_draw_cb_t fn;
	/** Passed through to the callback */
	void *user_data;
};

/**
 * @brief Where the overlay draws
 */
enum mpipe_vid_overlay_mode {
	/** Into the video frame itself, in place or into a copy */
	MPIPE_VID_OVERLAY_MODE_FRAME,
	/**
	 * Into a transparent BGRA32 frame of its own, for a display plane of the
	 * display to blend over the video; the input is consumed, not forwarded
	 */
	MPIPE_VID_OVERLAY_MODE_PLANE,
};

/**
 * @brief Video overlay property identifiers
 */
enum {
	/** Draw callback, a const struct mpipe_vid_overlay_cb pointer, copied */
	MPIPE_PROP_VID_OVERLAY_DRAW_CB = MPIPE_PROP_TRANSFORM_LAST,
	/** Where the overlay draws, a const enum mpipe_vid_overlay_mode pointer */
	MPIPE_PROP_VID_OVERLAY_MODE,
	/** Width of the plane frame in plane mode, a const uint16_t pointer */
	MPIPE_PROP_VID_OVERLAY_WIDTH,
	/** Height of the plane frame in plane mode, a const uint16_t pointer */
	MPIPE_PROP_VID_OVERLAY_HEIGHT,
};

/**
 * @brief Video overlay element
 *
 * In frame mode, a format-preserving transform handing every frame to an
 * application draw callback - the pipeline equivalent of drawing on a
 * canvas. A frame nobody else holds is drawn in place; a frame that is
 * shared, as every branch behind a tee sees the same payload, is copied
 * first and the callback draws into the copy, so the drawing never leaks
 * into a sibling branch.
 *
 * In plane mode the video is never touched: each input buffer, a video frame
 * or the results of a detector, makes the element clear a BGRA32 frame of its
 * own to transparent, hand it to the callback with the input, and push it
 * downstream, where a display sink on a display plane lets the display
 * controller blend it over the video at scan-out. The plane size is a
 * property, since the input need not carry one.
 *
 * The element itself knows nothing about what is drawn.
 */
struct mpipe_vid_overlay {
	/** Base transform element (must be first) */
	struct mpipe_transform transform;
	/** Where the overlay draws */
	enum mpipe_vid_overlay_mode mode;
	/** Frame width */
	uint16_t width;
	/** Frame height */
	uint16_t height;
	/** Negotiated pixel format */
	uint32_t pixfmt;
	/** The draw callback */
	struct mpipe_vid_overlay_cb cb;
	/** Internal output buffer pool */
	struct mpipe_buffer_pool out_pool;
	/** Free list of the pool's video buffers */
	struct k_fifo free_fifo;
	/** The pool's video buffers */
	struct video_buffer *vbufs[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX];
	/** Number of allocated video buffers */
	uint8_t vbuf_count;
	/** Frames downstream holds at once, asked of upstream and of the pool */
	uint8_t held_downstream;
};

/**
 * @brief Initialize a video overlay element
 *
 * @param overlay Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_overlay_init(struct mpipe_vid_overlay *overlay, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_OVERLAY_H_ */
