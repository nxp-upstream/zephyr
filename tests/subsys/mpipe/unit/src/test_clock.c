/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe_clock.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pipeline.h>

ZTEST_SUITE(mpipe_clock_api, NULL, NULL, NULL, NULL, NULL);

/* A settable clock so time arithmetic is tested deterministically */
static uint64_t fake_now;

static uint64_t fake_get_time(struct mpipe_clock *clock)
{
	ARG_UNUSED(clock);

	return fake_now;
}

static const struct mpipe_clock_ops fake_ops = {
	.get_time = fake_get_time,
};

static struct mpipe_clock fake_clock = {
	.ops = &fake_ops,
};

ZTEST(mpipe_clock_api, test_monotonic)
{
	struct mpipe_clock *clock = mpipe_clock_monotonic();
	uint64_t t1;
	uint64_t t2;

	zassert_not_null(clock, "no monotonic clock");

	t1 = mpipe_clock_get_time(clock);
	k_sleep(K_MSEC(10));
	t2 = mpipe_clock_get_time(clock);

	zassert_true(t2 >= t1 + 9000ULL, "clock did not advance: %llu -> %llu", t1, t2);
	zassert_equal(mpipe_clock_get_time(NULL), 0, "NULL clock != 0");
}

/*
 * The running time advances while PLAYING, freezes while PAUSED, resumes
 * without a jump, and restarts from zero after READY.
 */
ZTEST(mpipe_clock_api, test_running_time)
{
	static struct mpipe pipeline;
	struct mpipe_element *element = (struct mpipe_element *)&pipeline;

	zassert_ok(mpipe_pipeline_init(&pipeline, 0), "pipeline init failed");

	zassert_ok(mpipe_pipeline_set_clock(&pipeline, &fake_clock), "set_clock failed");
	zassert_equal(mpipe_pipeline_running_time(&pipeline), 0, "initial running time != 0");

	/* READY -> PAUSED -> PLAYING at fake time 1000 */
	fake_now = 1000;
	zassert_ok(mpipe_element_set_state(element, MPIPE_STATE_PLAYING),
		   "failed to reach PLAYING");
	zassert_equal(mpipe_pipeline_running_time(&pipeline), 0, "running time at start != 0");

	fake_now = 5000;
	zassert_equal(mpipe_pipeline_running_time(&pipeline), 4000, "running time while PLAYING");

	/* Pause at 5000: the running time freezes at 4000 */
	zassert_ok(mpipe_element_set_state(element, MPIPE_STATE_PAUSED), "failed to reach PAUSED");
	fake_now = 9000;
	zassert_equal(mpipe_pipeline_running_time(&pipeline), 4000,
		      "running time moved while PAUSED");

	/* Resume at 9000: the pause did not count */
	zassert_ok(mpipe_element_set_state(element, MPIPE_STATE_PLAYING),
		   "failed to resume PLAYING");
	fake_now = 10000;
	zassert_equal(mpipe_pipeline_running_time(&pipeline), 5000, "running time after resume");

	/* Back to READY: the running time restarts from zero on the next run */
	zassert_ok(mpipe_element_set_state(element, MPIPE_STATE_READY), "failed to reach READY");
	fake_now = 20000;
	zassert_ok(mpipe_element_set_state(element, MPIPE_STATE_PLAYING),
		   "failed to restart PLAYING");
	zassert_equal(mpipe_pipeline_running_time(&pipeline), 0, "running time after restart != 0");

	(void)mpipe_element_set_state(element, MPIPE_STATE_READY);
}

ZTEST(mpipe_clock_api, test_from_element)
{
	static struct mpipe pipeline;
	static struct mpipe_element orphan;

	zassert_ok(mpipe_pipeline_init(&pipeline, 0), "pipeline init failed");
	zassert_ok(mpipe_element_init(&orphan, 1), "element init failed");

	zassert_is_null(mpipe_pipeline_from_element(&orphan), "orphan element has a pipeline");
	zassert_equal(mpipe_element_running_time(&orphan), 0, "orphan running time != 0");
	zassert_is_null(mpipe_pipeline_from_element(NULL), "NULL element has a pipeline");
}

/*
 * A driver stamps with the kernel uptime; the running time it maps to is the
 * running time now less the age of the stamp, and zero before the run began.
 */
ZTEST(mpipe_clock_api, test_running_time_at)
{
	static struct mpipe pipeline;
	static struct mpipe_element orphan;
	struct mpipe_element *element = (struct mpipe_element *)&pipeline;
	uint64_t now_up;
	uint64_t rt;

	zassert_ok(mpipe_pipeline_init(&pipeline, 0), "pipeline init failed");
	zassert_ok(mpipe_element_init(&orphan, 1), "element init failed");
	zassert_ok(mpipe_pipeline_set_clock(&pipeline, &fake_clock), "set_clock failed");

	fake_now = 1000;
	zassert_ok(mpipe_element_set_state(element, MPIPE_STATE_PLAYING),
		   "failed to reach PLAYING");
	fake_now = 5000;

	/* 500 us ago on the uptime clock is 500 us ago in running time */
	now_up = k_ticks_to_us_floor64((uint64_t)k_uptime_ticks());
	rt = mpipe_pipeline_running_time_at(&pipeline, now_up - 500U);
	zassert_true(rt >= 3400U && rt <= 3500U, "running time at 500 us ago: %llu", rt);

	/* Before the run started, or in the future: unset */
	zassert_equal(mpipe_pipeline_running_time_at(&pipeline, now_up - 10000U), 0,
		      "an instant before the run has a running time");
	zassert_equal(mpipe_pipeline_running_time_at(&pipeline, now_up + 1000000U), 0,
		      "a future instant has a running time");
	zassert_equal(mpipe_element_running_time_at(&orphan, now_up), 0,
		      "orphan element has a running time");

	(void)mpipe_element_set_state(element, MPIPE_STATE_READY);
}
