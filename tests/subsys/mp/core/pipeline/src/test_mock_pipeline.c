/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pipeline integration tests using mp_fake_src and mp_fake_sink.
 *
 * Covers: end-to-end data flow and heap integrity.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/core/mp.h>
#include <zephyr/mp/core/mp_caps.h>
#include <zephyr/mp/zbase/mp_capsfilter.h>
#include "zephyr/mp/core/mp_element.h"
#include <zephyr/mp/core/mp_fake_src.h>
#include <zephyr/mp/core/mp_fake_sink.h>
#include "zephyr/mp/core/mp_object.h"
#include <zephyr/mp/zbase/mp_queue.h>
#include <zephyr/mp/core/mp_transform.h>
#include <zephyr/mp/core/mp_thread.h>

extern struct k_heap _system_heap;

#define PIPE_ID       0
#define SRC_ID        1
#define TRANSFORM_ID  2
#define SINK_ID       3
#define CAPSFILTER_ID 4
#define QUEUE_ID      5

/** Packet size produced by fake_src (bytes) */
#define TEST_BUFFER_SIZE 64

/** Number of buffers produced per run before EOS */
#define TEST_NUM_BUFFERS 5

struct test_mock_pipeline_fixture {
	struct mp_pipeline pipeline;
	struct mp_fake_src src;
	struct mp_transform transform;
	struct mp_fake_sink sink;
	struct mp_caps_filter caps_filter;
	struct mp_queue queue;
	struct sys_memory_stats mem_before, mem_after;
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
	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);

	MP_ELEMENT_INIT(&fix->pipeline, mp_pipeline_init, PIPE_ID);
	MP_ELEMENT_INIT(&fix->src, mp_fake_src_init, SRC_ID);
	MP_ELEMENT_INIT(&fix->transform, mp_transform_init, TRANSFORM_ID);
	MP_ELEMENT_INIT(&fix->sink, mp_fake_sink_init, SINK_ID);
	MP_ELEMENT_INIT(&fix->caps_filter, mp_caps_filter_init, CAPSFILTER_ID);
	MP_ELEMENT_INIT(&fix->queue, mp_queue_init, QUEUE_ID);

	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);
	zassert_equal(mp_object_set_properties((struct mp_object *)&fix->src,
					       PROP_FAKE_SRC_SIZE_MODE, MP_FAKE_SRC_SIZE_FIXED,
					       PROP_FAKE_SRC_SIZE_MAX, TEST_BUFFER_SIZE,
					       PROP_FAKE_SRC_FILL_MODE, MP_FAKE_SRC_FILL_NONE,
					       PROP_FAKE_SRC_DATA_RATE, 0, PROP_NUM_BUFS,
					       TEST_NUM_BUFFERS, PROP_LIST_END),
		      0, "fake_src property configuration shall succeed");
}

static void pipeline_after(void *f)
{
	struct test_mock_pipeline_fixture *fix = f;
	struct sys_memory_stats mem_after;

	sys_heap_runtime_stats_get(&_system_heap.heap, &mem_after);
	zassert_equal(fix->mem_before.allocated_bytes, mem_after.allocated_bytes,
		      "Memory leak detected: before=%zu after=%zu", fix->mem_before.allocated_bytes,
		      mem_after.allocated_bytes);
}

ZTEST_F(test_mock_pipeline, test_pipeline_fake_src_transform_sink)
{
	struct mp_bus *bus;
	struct mp_message *msg;

	/* Add elements and link: src → transform → sink */
	zassert_ok(mp_bin_add((struct mp_bin *)&fixture->pipeline,
			      (struct mp_element *)&fixture->src,
			      (struct mp_element *)&fixture->transform,
			      (struct mp_element *)&fixture->sink, NULL),
		   "mp_bin_add shall succeed");

	zassert_ok(mp_element_link((struct mp_element *)&fixture->src,
				   (struct mp_element *)&fixture->transform,
				   (struct mp_element *)&fixture->sink, NULL),
		   "mp_element_link src→transform→sink shall succeed");

	bus = mp_element_get_bus((struct mp_element *)&fixture->pipeline);

	zassert_equal(
		mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_PLAYING),
		MP_STATE_CHANGE_SUCCESS, "pipeline shall start PLAYING");

	msg = mp_bus_pop_msg(bus, MP_MESSAGE_EOS | MP_MESSAGE_ERROR);
	zassert_not_null(msg, "a message shall be posted after completion");
	zassert_equal(msg->type, MP_MESSAGE_EOS, "message shall be EOS");
	mp_message_destroy(msg);

	zassert_equal(mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_READY),
		      MP_STATE_CHANGE_SUCCESS, "pipeline shall return to READY");
	mp_thread_join(&fixture->pipeline.thread, K_FOREVER);

	zassert_equal(fixture->src.packets_sent, TEST_NUM_BUFFERS,
		      "source shall have produced %d packets", TEST_NUM_BUFFERS);
	zassert_equal(fixture->sink.buffers_consumed, TEST_NUM_BUFFERS,
		      "sink shall have consumed %d buffers", TEST_NUM_BUFFERS);
	zassert_equal(fixture->sink.bytes_consumed, (uint64_t)TEST_NUM_BUFFERS * TEST_BUFFER_SIZE,
		      "sink bytes_consumed shall be %d", TEST_NUM_BUFFERS * TEST_BUFFER_SIZE);
}

ZTEST_F(test_mock_pipeline, test_complexe_pipeline)
{
	struct mp_bus *bus;
	struct mp_message *msg;
	uint16_t queue_max_size = 2;

	zassert_ok(mp_object_set_properties((struct mp_object *)&fixture->queue, PROP_QUEUE_SIZE,
					    &queue_max_size, PROP_LIST_END),
		   "queue PROP_QUEUE_MAX_SIZE configuration shall succeed");

	/* Add elements and link: src → caps_filter → queue → transform → sink */
	zassert_ok(mp_bin_add((struct mp_bin *)&fixture->pipeline,
			      (struct mp_element *)&fixture->src,
			      (struct mp_element *)&fixture->caps_filter,
			      (struct mp_element *)&fixture->queue,
			      (struct mp_element *)&fixture->transform,
			      (struct mp_element *)&fixture->sink, NULL),
		   "mp_bin_add shall succeed for all five elements");

	zassert_ok(mp_element_link((struct mp_element *)&fixture->src,
				   (struct mp_element *)&fixture->caps_filter,
				   (struct mp_element *)&fixture->queue,
				   (struct mp_element *)&fixture->transform,
				   (struct mp_element *)&fixture->sink, NULL),
		   "mp_element_link full chain shall succeed");

	bus = mp_element_get_bus((struct mp_element *)&fixture->pipeline);

	zassert_equal(
		mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_PLAYING),
		MP_STATE_CHANGE_SUCCESS, "pipeline shall start PLAYING");

	msg = mp_bus_pop_msg(bus, MP_MESSAGE_EOS | MP_MESSAGE_ERROR);
	zassert_not_null(msg, "a message shall be posted after completion");
	zassert_equal(msg->type, MP_MESSAGE_EOS, "message shall be EOS");
	mp_message_destroy(msg);

	zassert_equal(mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_READY),
		      MP_STATE_CHANGE_SUCCESS, "pipeline shall return to READY");
	mp_thread_join(&fixture->pipeline.thread, K_FOREVER);

	zassert_equal(fixture->src.packets_sent, TEST_NUM_BUFFERS,
		      "source shall have produced %d packets", TEST_NUM_BUFFERS);
	zassert_equal(fixture->sink.buffers_consumed, TEST_NUM_BUFFERS,
		      "sink shall have consumed %d buffers", TEST_NUM_BUFFERS);
	zassert_equal(fixture->sink.bytes_consumed, (uint64_t)TEST_NUM_BUFFERS * TEST_BUFFER_SIZE,
		      "sink bytes_consumed shall be %llu",
		      (uint64_t)TEST_NUM_BUFFERS * TEST_BUFFER_SIZE);
}

ZTEST_SUITE(test_mock_pipeline, NULL, pipeline_suite_setup, pipeline_before, pipeline_after, NULL);
