/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_fake_src.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/mpipe_transform.h>

/*
 * fake_src -> splitter -> counter -> sink
 *
 * The splitter turns every source buffer into a chain of CHAIN_LEN buffers,
 * as a parser does. The walk must hand them to the counter and the sink one
 * at a time, in order, each reaching the sink before the next is processed.
 */

ZBUS_MSG_SUBSCRIBER_DEFINE(test_chain_sub);

enum {
	PIPE_ID,
	SRC_ID,
	SPLITTER_ID,
	COUNTER_ID,
	SINK_ID,
};

#define SRC_BUFS  3
#define CHAIN_LEN 4
#define UNITS     (SRC_BUFS * CHAIN_LEN)

enum stage {
	STAGE_COUNTER,
	STAGE_SINK,
};

struct event {
	enum stage stage;
	uint64_t seq;
};

static struct event event_log[2 * UNITS];
static atomic_t event_count;
static atomic_t freed;

static void chain_destroy(struct net_buf *buf)
{
	atomic_inc(&freed);
	mpipe_buffer_destroy(buf);
}

NET_BUF_POOL_FIXED_DEFINE(chain_pool, 2 * CHAIN_LEN, 16, sizeof(struct mpipe_buffer_meta),
			  chain_destroy);

static void record(enum stage stage, const struct net_buf *buf)
{
	atomic_val_t i = atomic_inc(&event_count);

	if (i < ARRAY_SIZE(event_log)) {
		event_log[i].stage = stage;
		event_log[i].seq = mpipe_buffer_get_meta(buf)->pts;
	}
}

struct splitter {
	struct mpipe_transform transform;
	uint64_t seq;
};

struct counter {
	struct mpipe_transform transform;
	atomic_t calls;
	atomic_t chained;
	uint32_t fail_at;
};

struct chain_sink {
	struct mpipe_sink sink;
	atomic_t calls;
	atomic_t chained;
};

struct test_chain_fixture {
	struct mpipe pipeline;
	struct mpipe_fake_src fake_src;
	struct splitter splitter;
	struct counter counter;
	struct chain_sink sink;
};

/* One input in, CHAIN_LEN numbered buffers out as a chain */
static int splitter_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
			       struct net_buf **out_buf)
{
	struct splitter *splitter = (struct splitter *)pad->object.container;

	net_buf_unref(in_buf);
	*out_buf = NULL;

	for (int i = 0; i < CHAIN_LEN; i++) {
		struct net_buf *buf = net_buf_alloc_len(&chain_pool, 16, K_NO_WAIT);
		struct mpipe_buffer_meta *meta;

		if (buf == NULL) {
			if (*out_buf != NULL) {
				net_buf_unref(*out_buf);
				*out_buf = NULL;
			}
			return -ENOBUFS;
		}

		meta = mpipe_buffer_get_meta(buf);
		meta->pool = NULL;
		meta->bytes_used = 16;
		meta->pts = splitter->seq++;
		meta->driver_buf = NULL;
		meta->priv = NULL;
		meta->parent = NULL;
		buf->len = 16;

		if (*out_buf == NULL) {
			*out_buf = buf;
		} else {
			net_buf_frag_add(*out_buf, buf);
		}
	}

	return 0;
}

static int counter_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
			      struct net_buf **out_buf)
{
	struct counter *counter = (struct counter *)pad->object.container;
	atomic_val_t calls = atomic_inc(&counter->calls) + 1;

	if (in_buf->frags != NULL) {
		atomic_inc(&counter->chained);
	}

	record(STAGE_COUNTER, in_buf);

	if (counter->fail_at != 0U && calls == counter->fail_at) {
		net_buf_unref(in_buf);
		*out_buf = NULL;
		return -EIO;
	}

	*out_buf = in_buf;

	return 0;
}

static int sink_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct chain_sink *sink = (struct chain_sink *)pad->object.container;

	atomic_inc(&sink->calls);
	if (in_buf->frags != NULL) {
		atomic_inc(&sink->chained);
	}

	record(STAGE_SINK, in_buf);

	net_buf_unref(in_buf);
	*out_buf = NULL;

	return 0;
}

static void *chain_suite_setup(void)
{
	static struct test_chain_fixture fixture;

	return &fixture;
}

static void chain_before(void *f)
{
	struct test_chain_fixture *fix = f;
	struct zbus_channel *bus;

	memset(fix, 0, sizeof(*fix));
	memset(event_log, 0, sizeof(event_log));
	atomic_set(&event_count, 0);
	atomic_set(&freed, 0);

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_fake_src_init(&fix->fake_src, SRC_ID));
	zassert_ok(mpipe_transform_init(&fix->splitter.transform, SPLITTER_ID));
	zassert_ok(mpipe_transform_init(&fix->counter.transform, COUNTER_ID));
	zassert_ok(mpipe_sink_init(&fix->sink.sink, SINK_ID));

	fix->splitter.transform.sink_pad.process_fn = splitter_process_fn;
	fix->counter.transform.sink_pad.process_fn = counter_process_fn;
	fix->sink.sink.sink_pad.process_fn = sink_process_fn;

	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->fake_src,
					       MPIPE_PROP_SRC_NUM_BUFS, SRC_BUFS,
					       MPIPE_PROP_LIST_END));

	zassert_ok(mpipe_bin_add(
		(struct mpipe_bin *)&fix->pipeline, (struct mpipe_element *)&fix->fake_src,
		(struct mpipe_element *)&fix->splitter, (struct mpipe_element *)&fix->counter,
		(struct mpipe_element *)&fix->sink, NULL));
	zassert_ok(mpipe_element_link(
		(struct mpipe_element *)&fix->fake_src, (struct mpipe_element *)&fix->splitter,
		(struct mpipe_element *)&fix->counter, (struct mpipe_element *)&fix->sink, NULL));

	bus = mpipe_element_get_bus_chan((struct mpipe_element *)&fix->pipeline);
	zassert_ok(zbus_chan_add_obs(bus, &test_chain_sub, K_FOREVER));
}

static void chain_after(void *f)
{
	struct test_chain_fixture *fix = f;
	struct zbus_channel *bus =
		mpipe_element_get_bus_chan((struct mpipe_element *)&fix->pipeline);

	(void)zbus_chan_rm_obs(bus, &test_chain_sub, K_FOREVER);
}

ZTEST_SUITE(test_chain, NULL, chain_suite_setup, chain_before, chain_after, NULL);

/* Every fragment reaches the counter and the sink alone, in order, depth first */
ZTEST_F(test_chain, test_chain_is_pushed_one_fragment_at_a_time)
{
	const struct zbus_channel *chan;
	struct mpipe_message msg;

	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					   MPIPE_STATE_PLAYING));
	zassert_ok(zbus_sub_wait_msg(&test_chain_sub, &chan, &msg, K_SECONDS(5)));
	zassert_equal(msg.type, MPIPE_MESSAGE_EOS, "expected EOS, got %d", msg.type);
	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					   MPIPE_STATE_READY));

	zassert_equal(atomic_get(&fixture->counter.calls), UNITS, "counter saw %ld buffers",
		      atomic_get(&fixture->counter.calls));
	zassert_equal(atomic_get(&fixture->sink.calls), UNITS, "sink saw %ld buffers",
		      atomic_get(&fixture->sink.calls));
	zassert_equal(atomic_get(&fixture->counter.chained), 0, "the counter was handed a chain");
	zassert_equal(atomic_get(&fixture->sink.chained), 0, "the sink was handed a chain");
	zassert_equal(atomic_get(&freed), UNITS, "%ld chain buffers released", atomic_get(&freed));

	zassert_equal(atomic_get(&event_count), 2 * UNITS);
	for (uint32_t i = 0; i < UNITS; i++) {
		zassert_equal(event_log[2 * i].stage, STAGE_COUNTER, "event %u", 2 * i);
		zassert_equal(event_log[2 * i].seq, i, "event %u: buffer %llu", 2 * i,
			      event_log[2 * i].seq);
		zassert_equal(event_log[2 * i + 1].stage, STAGE_SINK, "event %u", 2 * i + 1);
		zassert_equal(event_log[2 * i + 1].seq, i, "event %u: buffer %llu", 2 * i + 1,
			      event_log[2 * i + 1].seq);
	}
}

/* A failure on one fragment releases the ones not pushed yet */
ZTEST_F(test_chain, test_failure_releases_the_rest_of_the_chain)
{
	const struct zbus_channel *chan;
	struct mpipe_message msg;

	fixture->counter.fail_at = 2;

	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					   MPIPE_STATE_PLAYING));
	zassert_ok(zbus_sub_wait_msg(&test_chain_sub, &chan, &msg, K_SECONDS(5)));
	zassert_equal(msg.type, MPIPE_MESSAGE_ERROR, "expected an error, got %d", msg.type);
	zassert_equal(msg.domain, MPIPE_ERROR_FLOW);
	zassert_equal(msg.code, -EIO);
	zassert_equal(msg.origin, (struct mpipe_element *)&fixture->counter);

	/* READY joins the streaming thread, so the walk has released its leftovers */
	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
					   MPIPE_STATE_READY));

	zassert_equal(atomic_get(&fixture->counter.calls), 2);
	zassert_equal(atomic_get(&fixture->sink.calls), 1, "the sink saw %ld buffers",
		      atomic_get(&fixture->sink.calls));
	zassert_equal(atomic_get(&freed), CHAIN_LEN, "%ld of %d chain buffers released",
		      atomic_get(&freed), CHAIN_LEN);
}
