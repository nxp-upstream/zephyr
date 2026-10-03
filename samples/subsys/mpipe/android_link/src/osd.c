/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/video.h>
#include <zephyr/spinlock.h>

#include <zephyr/mpipe/vid/mpipe_vid_draw.h>

#include "osd.h"
#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(osd);

#define OSD_BAND_HEIGHT 80
#define OSD_TEXT_SCALE  2
#define OSD_TEXT_MAX    32
#define OSD_BACKGROUND  0x000000
#define OSD_FOREGROUND  0xFFFFFF

static struct {
	struct k_spinlock lock;
	char text[OSD_TEXT_MAX];
	int64_t deadline;
	const struct device *display_dev;
} osd;

/* The band the panel shows while nothing streams; the panel is RGB565 */
static uint8_t idle_band[SCR_WIDTH * OSD_BAND_HEIGHT * 2];

/* Paint the band with @p text centred into a frame or the idle buffer */
static void osd_paint(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		      const char *text)
{
	const struct mpipe_vid_rect band = {0, 0, width - 1, OSD_BAND_HEIGHT - 1};
	uint32_t tw = 0;
	uint32_t th = 0;
	int32_t x;
	int32_t y;

	(void)mpipe_vid_draw_fill(frame, width, height, pixfmt, &band, OSD_BACKGROUND);
	if (mpipe_vid_draw_text_size(OSD_TEXT_SCALE, text, &tw, &th) != 0) {
		return;
	}

	x = ((int32_t)width - (int32_t)tw) / 2;
	y = (OSD_BAND_HEIGHT - (int32_t)th) / 2;
	(void)mpipe_vid_draw_text(frame, width, height, pixfmt, MAX(x, 0), MAX(y, 0),
				  OSD_TEXT_SCALE, OSD_FOREGROUND, text);
}

/* The overlay element's callback, once per frame on the display thread */
static void osd_draw(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		     void *user_data)
{
	char text[OSD_TEXT_MAX];
	k_spinlock_key_t key;
	bool active;

	ARG_UNUSED(user_data);

	key = k_spin_lock(&osd.lock);
	active = k_uptime_get() < osd.deadline;
	if (active) {
		memcpy(text, osd.text, sizeof(text));
	}
	k_spin_unlock(&osd.lock, key);

	if (active) {
		osd_paint(frame, width, height, pixfmt, text);
	}
}

void osd_show(const char *text, uint32_t ms)
{
	k_spinlock_key_t key = k_spin_lock(&osd.lock);

	strncpy(osd.text, text, sizeof(osd.text) - 1);
	osd.text[sizeof(osd.text) - 1] = '\0';
	osd.deadline = k_uptime_get() + ms;
	k_spin_unlock(&osd.lock, key);
}

void osd_idle(const char *text)
{
	const struct display_buffer_descriptor desc = {
		.buf_size = sizeof(idle_band),
		.width = SCR_WIDTH,
		.height = OSD_BAND_HEIGHT,
		.pitch = SCR_WIDTH,
	};
	int ret;

	if (osd.display_dev == NULL) {
		return;
	}

	osd_paint(idle_band, SCR_WIDTH, OSD_BAND_HEIGHT, VIDEO_PIX_FMT_RGB565, text);
	ret = display_write(osd.display_dev, 0, 0, &desc, idle_band);
	if (ret != 0) {
		LOG_WRN("Could not write the panel (%d)", ret);
	}
}

int osd_init(struct mpipe_vid_overlay *overlay, const struct device *display_dev)
{
	static const struct mpipe_vid_overlay_cb cb = {.fn = osd_draw};

	osd.display_dev = display_dev;

	return mpipe_object_set_properties((struct mpipe_object *)overlay,
					   MPIPE_PROP_VID_OVERLAY_DRAW_CB, &cb,
					   MPIPE_PROP_LIST_END);
}
