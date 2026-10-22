/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_fake_src.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/mpipe_transform.h>

/*
 * A teardown must not run under a push: an element that releases what it holds
 * on PAUSED -> READY would otherwise free a buffer the streaming thread is still
 * writing into. The slow transform below stays in its process function for a
 * while and records whether its teardown ran meanwhile.
 */

enum {
	PIPE_ID,
	SRC_ID,
	SLOW_ID,
	SINK_ID,
};

#define SLOW_PUSH_MS 100

struct slow_transform {
	struct mpipe_transform transform;
	int (*base_change_state)(struct mpipe_element *self, enum mpipe_state_change transition);
	struct k_sem in_push;
	atomic_t pushing;
	bool torn_down_while_pushing;
};

struct test_teardown_fixture {
	struct mpipe pipeline;
	struct mpipe_fake_src fake_src;
	struct slow_transform slow;
	struct mpipe_sink sink;
};

static int slow_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct slow_transform *slow = (struct slow_transform *)pad->object.container;

	atomic_set(&slow->pushing, 1);
	k_sem_give(&slow->in_push);
	k_sleep(K_MSEC(SLOW_PUSH_MS));
	atomic_set(&slow->pushing, 0);

	*out_buf = in_buf;

	return 0;
}

static int slow_change_state(struct mpipe_element *self, enum mpipe_state_change transition)
{
	struct slow_transform *slow = (struct slow_transform *)self;

	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY && atomic_get(&slow->pushing) != 0) {
		slow->torn_down_while_pushing = true;
	}

	return slow->base_change_state(self, transition);
}

static void *teardown_suite_setup(void)
{
	static struct test_teardown_fixture fixture;

	return &fixture;
}

static void teardown_before(void *f)
{
	struct test_teardown_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_fake_src_init(&fix->fake_src, SRC_ID));
	zassert_ok(mpipe_transform_init(&fix->slow.transform, SLOW_ID));
	zassert_ok(mpipe_sink_init(&fix->sink, SINK_ID));

	k_sem_init(&fix->slow.in_push, 0, 1);
	fix->slow.transform.sink_pad.process_fn = slow_process_fn;
	fix->slow.base_change_state = fix->slow.transform.element.change_state;
	fix->slow.transform.element.change_state = slow_change_state;

	zassert_ok(mpipe_bin_add(
		(struct mpipe_bin *)&fix->pipeline, (struct mpipe_element *)&fix->fake_src,
		(struct mpipe_element *)&fix->slow, (struct mpipe_element *)&fix->sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&fix->fake_src,
				      (struct mpipe_element *)&fix->slow,
				      (struct mpipe_element *)&fix->sink, NULL));
}

ZTEST_SUITE(test_teardown, NULL, teardown_suite_setup, teardown_before, NULL, NULL);

/* Stopping while a push is in flight waits for it before the children tear down */
ZTEST_F(test_teardown, test_stop_waits_for_the_push_in_flight)
{
	struct mpipe_element *pipeline = (struct mpipe_element *)&fixture->pipeline;
	int64_t start;

	zassert_ok(mpipe_element_set_state(pipeline, MPIPE_STATE_PLAYING));

	/* The streaming thread is now inside the slow transform for SLOW_PUSH_MS */
	zassert_ok(k_sem_take(&fixture->slow.in_push, K_SECONDS(1)), "no push started");

	start = k_uptime_get();
	zassert_ok(mpipe_element_set_state(pipeline, MPIPE_STATE_READY));

	zassert_false(fixture->slow.torn_down_while_pushing,
		      "the transform was torn down while its push was in flight");
	zassert_equal(atomic_get(&fixture->slow.pushing), 0, "a push is still in flight");
	zassert_true(k_uptime_get() - start >= SLOW_PUSH_MS / 2,
		     "the stop did not wait for the push");
}
