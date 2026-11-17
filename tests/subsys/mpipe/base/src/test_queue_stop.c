/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_queue.h>

/*
 * Stop a pipeline while its thread is blocked in a full queue.
 *
 * The pipeline is app_src -> queue -> app_sink. The queue holds one buffer and
 * never drops one. The callback of the app_sink is slow, so the queue fills up
 * and the pipeline thread blocks in the processing function of the queue. The
 * pipeline joins its thread before the queue changes state, so the queue must
 * let the thread go when the pads are flushing.
 */

enum {
	PIPE_ID,
	SRC_ID,
	QUEUE_ID,
	SINK_ID,
};

#define SLOW_SINK_MS 100
#define PAYLOAD_SIZE (CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ / 2)
#define QUEUE_SIZE   1
/*
 * One payload is in the callback. The msgq of the queue takes its size plus
 * the two slots it keeps for its sentinels. The next payload blocks the
 * pipeline thread.
 */
#define PAYLOADS_NUM (1 + QUEUE_SIZE + 2 + 1)

BUILD_ASSERT(PAYLOADS_NUM <= CONFIG_MPIPE_BASE_APP_SRC_POOL_NUM);

struct test_queue_stop_fixture {
	struct mpipe pipeline;
	struct mpipe_app_src app_src;
	struct mpipe_queue queue;
	struct mpipe_app_sink app_sink;
};

static void slow_sink_cb(const struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(user_data);

	k_sleep(K_MSEC(SLOW_SINK_MS));
}

static void *queue_stop_suite_setup(void)
{
	static struct test_queue_stop_fixture fixture;

	return &fixture;
}

static void queue_stop_before(void *f)
{
	static const struct mpipe_app_sink_cb cb = {.fn = slow_sink_cb};
	struct test_queue_stop_fixture *fix = f;
	struct mpipe_element *src = (struct mpipe_element *)&fix->app_src;
	struct mpipe_element *queue = (struct mpipe_element *)&fix->queue;
	struct mpipe_element *sink = (struct mpipe_element *)&fix->app_sink;
	enum mpipe_base_queue_leak leak = MPIPE_BASE_QUEUE_LEAK_NONE;
	struct mpipe_structure caps;
	uint8_t size = QUEUE_SIZE;

	memset(fix, 0, sizeof(*fix));

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&fix->app_src, SRC_ID));
	zassert_ok(mpipe_queue_init(&fix->queue, QUEUE_ID));
	zassert_ok(mpipe_app_sink_init(&fix->app_sink, SINK_ID));

	zassert_ok(mpipe_structure_init_fields(
		&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		VIDEO_PIX_FMT_GREY, MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, 8,
		MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, 8, MPIPE_CAPS_END));
	zassert_ok(mpipe_object_set_properties(&src->object, MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_object_set_properties(&queue->object, MPIPE_PROP_BASE_QUEUE_SIZE, &size,
					       MPIPE_PROP_BASE_QUEUE_LEAK, &leak,
					       MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_object_set_properties(&sink->object, MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
					       MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));

	zassert_ok(mpipe_bin_add((struct mpipe_bin *)&fix->pipeline, src, queue, sink, NULL));
	zassert_ok(mpipe_element_link(src, queue, sink, NULL));
}

static void queue_stop_after(void *f)
{
	struct test_queue_stop_fixture *fix = f;

	(void)mpipe_element_set_state((struct mpipe_element *)&fix->pipeline, MPIPE_STATE_READY);
}

ZTEST_SUITE(test_queue_stop, NULL, queue_stop_suite_setup, queue_stop_before, queue_stop_after,
	    NULL);

ZTEST_F(test_queue_stop, test_stop_with_the_pipeline_thread_in_a_full_queue)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fixture->pipeline;
	struct net_buf *bufs[CONFIG_MPIPE_BASE_APP_SRC_POOL_NUM];
	uint8_t payload[PAYLOAD_SIZE] = {0};
	int ret;

	for (uint8_t run = 0; run < 2U; run++) {
		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));

		for (uint8_t i = 0; i < PAYLOADS_NUM; i++) {
			ret = mpipe_app_src_push(&fixture->app_src, payload, sizeof(payload),
						 K_SECONDS(1));
			zassert_ok(ret, "run %u: push %u failed", run, i);
		}

		/* Let the queue fill up and the pipeline thread block in it */
		k_sleep(K_MSEC(SLOW_SINK_MS / 2));
		zassert_equal(k_msgq_num_free_get(&fixture->queue.msgq), 0,
			      "run %u: the queue is not full", run);

		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY),
			   "run %u: the stop did not complete", run);

		/* Every buffer is back in the pool of the app_src */
		for (uint8_t i = 0; i < CONFIG_MPIPE_BASE_APP_SRC_POOL_NUM; i++) {
			ret = mpipe_app_src_alloc(&fixture->app_src, PAYLOAD_SIZE, K_NO_WAIT,
						  &bufs[i]);
			zassert_ok(ret, "run %u: buffer %u was not released", run, i);
		}
		for (uint8_t i = 0; i < CONFIG_MPIPE_BASE_APP_SRC_POOL_NUM; i++) {
			net_buf_unref(bufs[i]);
		}
	}
}

/*
 * Stop a pipeline while its thread waits for a buffer that a paused queue holds.
 *
 * The pipeline is app_src -> burst -> queue -> app_sink. For each buffer it
 * receives, the burst transform pushes BURST_NUM buffers from its own pool,
 * which has only BURST_POOL_NUM of them, and it waits when the pool is empty.
 * Once the pipeline is paused, the queue thread no longer takes buffers out of
 * the queue. The queue then holds every buffer of the pool and the pipeline
 * thread waits in the transform for ever, unless the stop makes the queue
 * free its buffers.
 */

enum {
	BURST_ID = SINK_ID + 1,
};

#define BURST_NUM      8
#define BURST_POOL_NUM 2

NET_BUF_POOL_FIXED_DEFINE(burst_pool, BURST_POOL_NUM, 16, sizeof(struct mpipe_buffer_meta), NULL);

struct test_queue_pool_fixture {
	struct mpipe pipeline;
	struct mpipe_app_src app_src;
	struct mpipe_transform burst;
	struct mpipe_queue queue;
	struct mpipe_app_sink app_sink;
	struct k_sem burst_started;
};

static int burst_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct mpipe_transform *burst = (struct mpipe_transform *)pad->object.container;
	struct test_queue_pool_fixture *fix =
		CONTAINER_OF(burst, struct test_queue_pool_fixture, burst);

	net_buf_unref(in_buf);
	*out_buf = NULL;
	k_sem_give(&fix->burst_started);

	for (uint8_t i = 0; i < BURST_NUM; i++) {
		struct net_buf *out = net_buf_alloc(&burst_pool, K_FOREVER);

		memset(mpipe_buffer_get_meta(out), 0, sizeof(struct mpipe_buffer_meta));
		(void)net_buf_add(out, 1);
		(void)mpipe_push_buffer(&burst->src_pad, out);
	}

	return 0;
}

static void *queue_pool_suite_setup(void)
{
	static struct test_queue_pool_fixture fixture;

	return &fixture;
}

static void queue_pool_before(void *f)
{
	static const struct mpipe_app_sink_cb cb = {.fn = slow_sink_cb};
	struct test_queue_pool_fixture *fix = f;
	struct mpipe_element *src = (struct mpipe_element *)&fix->app_src;
	struct mpipe_element *burst = (struct mpipe_element *)&fix->burst;
	struct mpipe_element *queue = (struct mpipe_element *)&fix->queue;
	struct mpipe_element *sink = (struct mpipe_element *)&fix->app_sink;
	struct mpipe_structure caps;

	memset(fix, 0, sizeof(*fix));
	k_sem_init(&fix->burst_started, 0, 1);

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&fix->app_src, SRC_ID));
	zassert_ok(mpipe_transform_init(&fix->burst, BURST_ID));
	zassert_ok(mpipe_queue_init(&fix->queue, QUEUE_ID));
	zassert_ok(mpipe_app_sink_init(&fix->app_sink, SINK_ID));
	fix->burst.sink_pad.process_fn = burst_process_fn;

	zassert_ok(mpipe_structure_init_fields(
		&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		VIDEO_PIX_FMT_GREY, MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, 8,
		MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, 8, MPIPE_CAPS_END));
	zassert_ok(mpipe_object_set_properties(&src->object, MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_object_set_properties(&sink->object, MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
					       MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));

	zassert_ok(
		mpipe_bin_add((struct mpipe_bin *)&fix->pipeline, src, burst, queue, sink, NULL));
	zassert_ok(mpipe_element_link(src, burst, queue, sink, NULL));
}

static void queue_pool_after(void *f)
{
	struct test_queue_pool_fixture *fix = f;

	(void)mpipe_element_set_state((struct mpipe_element *)&fix->pipeline, MPIPE_STATE_READY);
}

ZTEST_SUITE(test_queue_pool, NULL, queue_pool_suite_setup, queue_pool_before, queue_pool_after,
	    NULL);

ZTEST_F(test_queue_pool, test_stop_with_the_pool_held_by_a_paused_queue)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fixture->pipeline;
	struct net_buf *bufs[BURST_POOL_NUM];
	uint8_t payload[PAYLOAD_SIZE] = {0};

	for (uint8_t run = 0; run < 2U; run++) {
		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));
		zassert_ok(mpipe_app_src_push(&fixture->app_src, payload, sizeof(payload),
					      K_SECONDS(1)));
		zassert_ok(k_sem_take(&fixture->burst_started, K_SECONDS(1)),
			   "run %u: the transform got no buffer", run);

		/* The queue thread parks; the transform fills the queue and then waits */
		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PAUSED));
		k_sleep(K_MSEC(2 * SLOW_SINK_MS));
		zassert_is_null(net_buf_alloc(&burst_pool, K_NO_WAIT),
				"run %u: the pool is not empty", run);

		zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY),
			   "run %u: the stop did not complete", run);

		for (uint8_t i = 0; i < BURST_POOL_NUM; i++) {
			bufs[i] = net_buf_alloc(&burst_pool, K_NO_WAIT);
			zassert_not_null(bufs[i], "run %u: buffer %u was not freed", run, i);
		}
		for (uint8_t i = 0; i < BURST_POOL_NUM; i++) {
			net_buf_unref(bufs[i]);
		}
	}
}
