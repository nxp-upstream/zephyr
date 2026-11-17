/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>

/*
 * A video source that is stopped and played again.
 *
 * The pipeline is vid_src -> app_sink, with the software video generator as
 * the camera. The source ends each run by itself after FRAMES_PER_RUN frames,
 * so its thread is not waiting in video_dequeue() when the pipeline stops.
 * Each run must deliver frames that the driver has filled.
 */

enum {
	PIPE_ID,
	SRC_ID,
	SINK_ID,
};

#define RUNS_NUM       3
#define FRAMES_PER_RUN 3

/* What the callback of the app_sink saw during the current run */
static struct {
	struct k_sem frame;
	atomic_t empty_frames;
} seen;

struct test_vid_src_fixture {
	struct mpipe pipeline;
	struct mpipe_vid_src vid_src;
	struct mpipe_app_sink app_sink;
};

static void frame_cb(const struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(user_data);

	if (mpipe_buffer_get_meta(buf)->bytes_used == 0U) {
		atomic_inc(&seen.empty_frames);
	}

	k_sem_give(&seen.frame);
}

static void *vid_src_suite_setup(void)
{
	static struct test_vid_src_fixture fixture;

	return &fixture;
}

static void vid_src_before(void *f)
{
	static const struct mpipe_app_sink_cb cb = {.fn = frame_cb};
	struct test_vid_src_fixture *fix = f;
	struct mpipe_element *src = (struct mpipe_element *)&fix->vid_src;
	struct mpipe_element *sink = (struct mpipe_element *)&fix->app_sink;

	memset(fix, 0, sizeof(*fix));

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_vid_src_init(&fix->vid_src, SRC_ID));
	zassert_ok(mpipe_app_sink_init(&fix->app_sink, SINK_ID));
	zassert_ok(mpipe_object_set_properties(&src->object, MPIPE_PROP_SRC_NUM_BUFS,
					       FRAMES_PER_RUN, MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_object_set_properties(&sink->object, MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));

	zassert_ok(mpipe_bin_add((struct mpipe_bin *)&fix->pipeline, src, sink, NULL));
	zassert_ok(mpipe_element_link(src, sink, NULL));
}

static void vid_src_after(void *f)
{
	struct test_vid_src_fixture *fix = f;

	(void)mpipe_element_set_state((struct mpipe_element *)&fix->pipeline, MPIPE_STATE_READY);
}

ZTEST_SUITE(test_vid_src, NULL, vid_src_suite_setup, vid_src_before, vid_src_after, NULL);

/*
 * Stopping the video device leaves buffers in the output queue of the driver.
 * The pool must take them out, or the next run starts with a buffer that the
 * driver never filled.
 */
ZTEST_F(test_vid_src, test_stop_and_play_again)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fixture->pipeline;

	for (uint8_t run = 0; run < RUNS_NUM; run++) {
		k_sem_init(&seen.frame, 0, K_SEM_MAX_LIMIT);
		atomic_set(&seen.empty_frames, 0);

		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING), "run %u", run);

		for (uint8_t i = 0; i < FRAMES_PER_RUN; i++) {
			zassert_ok(k_sem_take(&seen.frame, K_SECONDS(2)),
				   "run %u: frame %u did not arrive", run, i);
		}

		/* Let the source reach its limit and stop acquiring buffers */
		k_sleep(K_MSEC(100));

		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY),
			   "run %u: the stop failed", run);
		zassert_equal(atomic_get(&seen.empty_frames), 0, "run %u: %ld empty frame(s)", run,
			      (long)atomic_get(&seen.empty_frames));
	}
}
