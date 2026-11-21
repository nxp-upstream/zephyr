/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The pipeline is described in pipeline.dtsi and built and played at boot.
 * The application brings the stream it pushes into the source and what this
 * core draws on the frames.
 */

#include <stdio.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_draw.h>

#define FEED_POLL K_MSEC(100)

static const uint8_t mjpeg[] = {
#include "mjpeg.inc"
};

/* What this core adds to the frames the other core decoded and drew on */
void sample_draw_hud(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		     const struct net_buf *in, void *user_data)
{
	static uint32_t frames;
	char text[24];

	ARG_UNUSED(in);
	ARG_UNUSED(user_data);

	frames++;
	(void)snprintf(text, sizeof(text), "M7 %u", (unsigned int)frames);
	(void)mpipe_vid_draw_text(frame, width, height, pixfmt, 4, 4, 1, 0xFF000000U, text);
}

/*
 * Push the stream every time the pipeline plays, then declare its end: a
 * replay asked of the player gets the stream again.
 */
int main(void)
{
	struct mpipe_player *player = MPIPE_DT_PLAYER_GET(DT_NODELABEL(pipeline));
	struct mpipe_app_src *app_src =
		(struct mpipe_app_src *)MPIPE_DT_ELEMENT_GET(DT_NODELABEL(stream_src));
	size_t offset;
	uint32_t len;

	for (;;) {
		if (mpipe_player_wait_state(player, MPIPE_STATE_PLAYING, FEED_POLL) != 0) {
			continue;
		}

		offset = 0;
		while (offset < sizeof(mjpeg)) {
			len = (uint32_t)MIN(sizeof(mjpeg) - offset,
					    CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ);

			if (mpipe_app_src_push(app_src, &mjpeg[offset], len, FEED_POLL) == 0) {
				offset += len;
				continue;
			}

			/* Stopped while the stream was being pushed; busy or paused otherwise */
			if (mpipe_player_wait_state(player, MPIPE_STATE_READY, K_NO_WAIT) == 0) {
				break;
			}
		}

		if (offset == sizeof(mjpeg)) {
			(void)mpipe_app_src_eos(app_src, K_FOREVER);
		}

		/* The player stops the pipeline at the end of the stream */
		while (mpipe_player_wait_state(player, MPIPE_STATE_READY, FEED_POLL) != 0) {
		}
	}

	return 0;
}
