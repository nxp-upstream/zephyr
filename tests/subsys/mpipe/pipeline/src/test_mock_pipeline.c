/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_fake_src.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/mpipe_transform.h>

ZBUS_MSG_SUBSCRIBER_DEFINE(test_pipeline_sub);

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

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_fake_src_init(&fix->fake_src, SRC_ID));
	zassert_ok(mpipe_transform_init(&fix->transform, TRANSFORM_ID));
	zassert_ok(mpipe_sink_init(&fix->sink, SINK_ID));

	/* Set number of buffers to produce before EOS */
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->fake_src,
					       MPIPE_PROP_SRC_NUM_BUFS, TEST_BUFS_NUM,
					       MPIPE_PROP_LIST_END),
		   "Failed to set fake_src MPIPE_PROP_SRC_NUM_BUFS");
}

static void pipeline_after(void *f)
{
	struct test_mock_pipeline_fixture *fix = f;
	(void)zbus_chan_rm_obs(&fix->pipeline.bin.bus.channel, &test_pipeline_sub, K_FOREVER);
	(void)mpipe_bin_deinit_bus(&fix->pipeline.bin);
}

ZTEST_SUITE(test_mock_pipeline, NULL, pipeline_suite_setup, pipeline_before, pipeline_after, NULL);

ZTEST_F(test_mock_pipeline, test_pipeline_fake_src_transform_sink)
{
	const struct zbus_channel *chan;
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

	/* Attach the subscriber to the pipeline bin's channel at runtime. */
	zassert_ok(zbus_chan_add_obs(&fixture->pipeline.bin.bus.channel, &test_pipeline_sub,
				     K_FOREVER),
		   "Failed to add observer to pipeline channel");

	/* Start the pipeline */
	zassert_equal(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					      MPIPE_STATE_PLAYING),
		      MPIPE_STATE_CHANGE_SUCCESS, "Pipline failed to start PLAYING");

	/* Wait for EOS posted by the sink */
	zassert_ok(zbus_sub_wait_msg(&test_pipeline_sub, &chan, &msg, K_FOREVER),
		   "Timed out waiting for pipeline message");
	zassert_equal(msg.type, MPIPE_MESSAGE_EOS, "Expected EOS Message,  got %d", msg.type);

	/* Bring pipeline back to READY and join the thread */
	zassert_equal(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					      MPIPE_STATE_READY),
		      MPIPE_STATE_CHANGE_SUCCESS, "Pipeline failed to return to READY");

	/* Detach the runtime observer */
	zassert_ok(
		zbus_chan_rm_obs(&fixture->pipeline.bin.bus.channel, &test_pipeline_sub, K_FOREVER),
		"Failed to remove observer from pipeline channel");

	/* The bus channel is torn down in pipeline_after, which also runs on early failure. */
}
