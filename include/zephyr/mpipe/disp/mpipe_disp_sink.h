/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mpipe_disp_sinks
 * @brief Display sink element for the mpipe disp plugin.
 *
 * Provides a sink element that renders video frames to a Zephyr display
 * device, supporting partial frame updates.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_DISP_MPIPE_DISP_SINK_H_
#define ZEPHYR_INCLUDE_MPIPE_DISP_MPIPE_DISP_SINK_H_

/**
 * @defgroup mpipe_disp_sinks Sinks
 * @ingroup mpipe_disp
 * @brief Display sink elements.
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include <zephyr/mpipe/mpipe_sink.h>

/** No display plane: frames go through display_write() */
#define MPIPE_DISP_SINK_NO_PLANE (-1)

/**
 * @brief Display sink property identifiers.
 *
 * Extends the base sink properties defined in @ref mpipe_prop_sink.
 * Enumeration starts from @ref MPIPE_PROP_SINK_LAST to avoid conflicts.
 */
enum {
	/** Display device property (const struct device *). */
	MPIPE_PROP_DISP_SINK_DEVICE = MPIPE_PROP_SINK_LAST,
	/**
	 * Display plane the frames are shown on (const int *), see
	 * display_set_plane(). @ref MPIPE_DISP_SINK_NO_PLANE, the default,
	 * writes the frames with display_write() instead.
	 */
	MPIPE_PROP_DISP_SINK_PLANE,
	/** x coordinate of the plane on the panel (const uint16_t *), 0 by default */
	MPIPE_PROP_DISP_SINK_X,
	/** y coordinate of the plane on the panel (const uint16_t *), 0 by default */
	MPIPE_PROP_DISP_SINK_Y,
	/** Plane alpha (const uint8_t *), 255 by default */
	MPIPE_PROP_DISP_SINK_ALPHA,
	/** Plane blend mode (const enum display_blend_mode *), DISPLAY_BLEND_NONE by default */
	MPIPE_PROP_DISP_SINK_BLEND_MODE,
};

/**
 * @brief Display sink element.
 *
 * Extends the base @ref mpipe_sink with display-specific capabilities.
 * Renders incoming video buffers to a Zephyr display device. When the
 * display holds the buffer of a full-frame write
 * (@ref SCREEN_INFO_WRITE_HOLDS_BUFFER), the sink keeps the frame on screen
 * until the next one replaces it and asks upstream for that one buffer in
 * the pool query.
 *
 * Bound to a display plane with @ref MPIPE_PROP_DISP_SINK_PLANE, the sink
 * hands each frame to the display controller through display_set_plane()
 * and display_commit(): the controller blends it over the planes below at
 * scan-out and reads the frame until the next commit, so the sink holds one
 * buffer at all times. Several sinks on several planes, each at the end of
 * its own branch or pipeline, compose their streams without any copy; the
 * application chooses the plane, position, alpha and blend mode of each.
 */
struct mpipe_disp_sink {
	/** Base sink element. */
	struct mpipe_sink sink;
	/** Display device instance. */
	const struct device *display_dev;
	/** Display plane, or @ref MPIPE_DISP_SINK_NO_PLANE */
	int plane;
	/** Plane position on the panel */
	uint16_t x;
	/** Plane position on the panel */
	uint16_t y;
	/** Plane alpha, 255 for opaque */
	uint8_t alpha;
	/** Plane blend mode */
	enum display_blend_mode blend_mode;
	/** The display holds the buffer of a full-frame write until the next one. */
	bool holds_buffer;
	/**
	 * The frame on screen, held until the next frame has replaced it or the
	 * run ends. NULL when the display copies its frames.
	 */
	struct net_buf *shown;
};

/**
 * @brief Initialize a display sink element.
 *
 * @param disp_sink Element to initialize.
 * @param id        Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_disp_sink_init(struct mpipe_disp_sink *disp_sink, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_DISP_MPIPE_DISP_SINK_H_ */
