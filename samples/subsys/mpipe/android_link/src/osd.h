/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRMIRROR_OSD_H
#define SCRMIRROR_OSD_H

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>

/*
 * Text on the panel: a band at the top of the frames the display sink shows,
 * drawn by the overlay element's callback while a message is active, and the
 * same band written to the panel directly while no phone streams.
 */

/* Set the overlay element's draw callback up and remember the panel */
int osd_init(struct mpipe_vid_overlay *overlay, const struct device *display_dev);
/* Show @p text on the frames for @p ms milliseconds; callable from any context */
void osd_show(const char *text, uint32_t ms);
/* Write @p text to the panel now, for when no pipeline draws on it */
void osd_idle(const char *text);

#endif
