/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/ztest_assert.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/core/mp_bin.h>
#include <zephyr/mp/core/mp_bus.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_fake_sink.h>
#include <zephyr/mp/core/mp_fake_src.h>
#include <zephyr/mp/core/mp_object.h>
#include <zephyr/mp/core/mp_pipeline.h>
#include <zephyr/mp/core/mp_property.h>

extern struct k_heap _system_heap;

#define PUSH_BUF_SIZE 32

struct mp_pipeline_api_fixture {
	struct mp_pipeline pipeline;
	struct mp_fake_src src;
	struct mp_fake_sink sink;
	struct sys_memory_stats mem_before;
};

static void *pipeline_suite_setup(void)
{
	static struct mp_pipeline_api_fixture fixture;

	return &fixture;
}

static void pipeline_before(void *f)
{
	struct mp_pipeline_api_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));

	MP_ELEMENT_INIT((struct mp_element *)&fix->pipeline, mp_pipeline_init, 0);
	MP_ELEMENT_INIT((struct mp_element *)&fix->src, mp_fake_src_init, 1);
	MP_ELEMENT_INIT((struct mp_element *)&fix->sink, mp_fake_sink_init, 2);

	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);

	zassert_equal(mp_object_set_properties((struct mp_object *)&fix->src,
					       PROP_FAKE_SRC_SIZE_MODE, MP_FAKE_SRC_SIZE_FIXED,
					       PROP_FAKE_SRC_SIZE_MAX, PUSH_BUF_SIZE,
					       PROP_FAKE_SRC_FILL_MODE, MP_FAKE_SRC_FILL_ZERO,
					       PROP_FAKE_SRC_DATA_RATE, 0, PROP_LIST_END),
		      0, "fake_src default configuration shall succeed");

	zassert_ok(mp_element_link((struct mp_element *)(&fix->src),
				   (struct mp_element *)(&fix->sink), NULL),
		   "src->sink link shall succeed");
}

static void pipeline_after(void *f)
{
	struct mp_pipeline_api_fixture *fix = f;
	struct sys_memory_stats mem_after;

	sys_heap_runtime_stats_get(&_system_heap.heap, &mem_after);
	zassert_equal(fix->mem_before.allocated_bytes, mem_after.allocated_bytes,
		      "Memory leak detected: before=%zu after=%zu", fix->mem_before.allocated_bytes,
		      mem_after.allocated_bytes);
}

ZTEST_SUITE(mp_pipeline_api, NULL, pipeline_suite_setup, pipeline_before, pipeline_after, NULL);

ZTEST_F(mp_pipeline_api, test_pipeline_push_buffer)
{
	struct net_buf *buf = NULL;

	/* Add elements to pipeline and bring to PLAYING so chainfn is active */
	zassert_ok(mp_bin_add((struct mp_bin *)&fixture->pipeline,
			      (struct mp_element *)&fixture->src,
			      (struct mp_element *)&fixture->sink, NULL),
		   "mp_bin_add shall succeed");

	zassert_equal(
		mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_PLAYING),
		MP_STATE_CHANGE_SUCCESS, "pipeline shall reach PLAYING state");

	zassert_ok(fixture->src.base.pool->acquire_buffer(fixture->src.base.pool, &buf),
		   "Buffer acquisition from fake_src pool shall succeed");
	zassert_not_null(buf, "Acquired buffer shall be non-NULL");

	zassert_ok(mp_pipeline_push_buffer(&fixture->src.base.srcpad, buf),
		   "mp_pipeline_push_buffer shall succeed");

	zassert_equal(fixture->sink.buffers_consumed, 1,
		      "Sink shall have consumed the pushed buffer");

	zassert_equal(mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_READY),
		      MP_STATE_CHANGE_SUCCESS, "pipeline shall return to READY");

	mp_thread_join(&fixture->pipeline.thread, K_FOREVER);

	mp_bus_flush(&fixture->pipeline.bin.bus);
}
