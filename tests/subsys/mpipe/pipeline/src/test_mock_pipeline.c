/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_fake_src.h>

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	SRC_ID,
	TRANSFORM_ID,
	SINK_ID,
};

/* Number of buffers the source shall produce before EOS */
#define TEST_BUFS_NUM 10

struct test_mock_pipeline_fixture {
	struct mpipe pipeline;
	struct mpipe_fake_src fake_src;
	struct mpipe_transform transform;
	struct mpipe_sink sink;
};

static void *pipeline_suite_setup(void)
{
	static struct test_mock_pipeline_fixture fixture;

	return &fixture;
}

static void pipeline_before(void *f)
{
	struct test_mock_pipeline_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));

	MPIPE_ELEMENT_INIT(&fix->pipeline, mpipe_pipeline_init, PIPE_ID);
	MPIPE_ELEMENT_INIT(&fix->fake_src, mpipe_fake_src_init, SRC_ID);
	MPIPE_ELEMENT_INIT(&fix->transform, mpipe_transform_init, TRANSFORM_ID);
	MPIPE_ELEMENT_INIT(&fix->sink, mpipe_sink_init, SINK_ID);

	/* Set number of buffers to produce before EOS */
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->fake_src,
					       MPIPE_PROP_SRC_NUM_BUFS, TEST_BUFS_NUM,
					       MPIPE_PROP_LIST_END),
		   "Failed to set fake_src MPIPE_PROP_SRC_NUM_BUFS");
}

ZTEST_SUITE(test_mock_pipeline, NULL, pipeline_suite_setup, pipeline_before, NULL, NULL);

ZTEST_F(test_mock_pipeline, test_pipeline_fake_src_transform_sink)
{
	struct mpipe_bus *bus;
	struct mpipe_message msg;

	/* Add all elements to the pipeline */
	zassert_ok(mpipe_bin_add((struct mpipe_bin *)&fixture->pipeline,
				 (struct mpipe_element *)&fixture->fake_src,
				 (struct mpipe_element *)&fixture->transform,
				 (struct mpipe_element *)&fixture->sink, NULL),
		   "Failed to add elements");

	/* Link: fake_src → transform → sink */
	zassert_ok(mpipe_element_link((struct mpipe_element *)&fixture->fake_src,
				      (struct mpipe_element *)&fixture->transform,
				      (struct mpipe_element *)&fixture->sink, NULL),
		   "Failed to link elements");

	/* Start the pipeline */
	zassert_equal(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					      MPIPE_STATE_PLAYING),
		      MPIPE_STATE_CHANGE_SUCCESS, "Pipline failed to start PLAYING");

	/* Wait for EOS posted by the sink */
	bus = mpipe_element_get_bus((struct mpipe_element *)&fixture->pipeline);
	mpipe_bus_pop_msg(bus, MPIPE_MESSAGE_EOS | MPIPE_MESSAGE_ERROR, &msg);
	zassert_equal(msg.type, MPIPE_MESSAGE_EOS, "Expected EOS Message,  got %d", msg.type);

	/* Bring pipeline back to READY and join the thread */
	zassert_equal(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					      MPIPE_STATE_READY),
		      MPIPE_STATE_CHANGE_SUCCESS, "Pipeline failed to return to READY");
}
