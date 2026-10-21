/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The raw PCM parser frames a byte stream into blocks of the negotiated
 * duration. Whatever sizes the input arrives in, every block out must be
 * exactly one block long, hold the stream's bytes in order, and come from the
 * slab the sink will hand to its driver.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>
#include <zephyr/mpipe/aud/mpipe_aud_raw_parser.h>

/* 8 kHz, 16-bit, stereo, 10 ms: 320-byte blocks */
#define RATE        8000
#define BITS        16
#define CHANNELS    2
#define INTERVAL    10000
#define BLOCK_SIZE  320
/* The sink would ask for two; the parser adds the one it fills */
#define SINK_BLOCKS 2
#define SLAB_BLOCKS (SINK_BLOCKS + 1)

NET_BUF_POOL_FIXED_DEFINE(input_pool, 4, 1024, sizeof(struct mpipe_buffer_meta),
			  mpipe_buffer_destroy);

static struct k_mem_slab test_slab;

struct mpipe_aud_raw_parser_fixture {
	struct mpipe_aud_raw_parser parser;
	uint8_t stream[2048];
};

static void *raw_parser_setup(void)
{
	static struct mpipe_aud_raw_parser_fixture fixture;

	return &fixture;
}

static struct mpipe_element *element(struct mpipe_aud_raw_parser_fixture *fix)
{
	return &fix->parser.base.element;
}

static void raw_parser_before(void *f)
{
	struct mpipe_aud_raw_parser_fixture *fix = f;
	struct mpipe_dispatch query = {.type = MPIPE_DISPATCH_BUFFER_POOL};
	struct mpipe_structure caps;
	uint32_t rate = RATE, bits = BITS, channels = CHANNELS, interval = INTERVAL;

	memset(fix, 0, sizeof(*fix));
	for (uint32_t i = 0; i < sizeof(fix->stream); i++) {
		fix->stream[i] = (uint8_t)(i * 3U + 1U);
	}

	zassert_ok(mpipe_aud_raw_parser_init(&fix->parser, 1), "parser init failed");
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->parser,
					       MPIPE_PROP_AUD_RAW_PARSER_SLAB_PTR, &test_slab,
					       MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE, &rate,
					       MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH, &bits,
					       MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS, &channels,
					       MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL, &interval,
					       MPIPE_PROP_LIST_END),
		   "setting the properties failed");

	/* What the base parser does on the pool query: decide, configure, start */
	query.pool_cfg.min_buffers = SINK_BLOCKS;
	zassert_ok(fix->parser.base.decide_buffer_pool(&fix->parser.base, &query));
	zassert_ok(mpipe_pad_enum_caps(&fix->parser.base.src_pad, 0, NULL, &caps));
	zassert_ok(mpipe_buffer_pool_configure(fix->parser.base.out_pool, &caps),
		   "pool configure failed");
	zassert_equal(fix->parser.base.out_pool->config.size, BLOCK_SIZE, "block size");
	zassert_equal(fix->parser.base.out_pool->config.min_buffers, SLAB_BLOCKS, "block count");
	zassert_equal(mpipe_buffer_pool_start(fix->parser.base.out_pool), -ENOSYS);

	/* A bin records the state its children reach; here the test does */
	zassert_ok(element(fix)->change_state(element(fix), MPIPE_STATE_CHANGE_READY_TO_PAUSED));
	element(fix)->current_state = MPIPE_STATE_PAUSED;
}

static void raw_parser_after(void *f)
{
	struct mpipe_aud_raw_parser_fixture *fix = f;

	zassert_ok(element(fix)->change_state(element(fix), MPIPE_STATE_CHANGE_PAUSED_TO_READY));
	element(fix)->current_state = MPIPE_STATE_READY;
	zassert_equal(k_mem_slab_num_free_get(&test_slab), SLAB_BLOCKS,
		      "teardown left blocks allocated");
}

ZTEST_SUITE(mpipe_aud_raw_parser, NULL, raw_parser_setup, raw_parser_before, raw_parser_after,
	    NULL);

/* Push @p len bytes of the stream from @p offset; returns the blocks produced */
static struct net_buf *feed(struct mpipe_aud_raw_parser_fixture *fix, uint32_t offset, uint32_t len)
{
	struct mpipe_pad *sink_pad = &fix->parser.base.sink_pad;
	struct net_buf *in = net_buf_alloc_len(&input_pool, 1024, K_NO_WAIT);
	struct mpipe_buffer_meta *m;
	struct net_buf *out = NULL;

	zassert_not_null(in, "input allocation failed");
	zassert_true(len <= 1024, "input too long for the test pool");
	memcpy(in->data, fix->stream + offset, len);
	m = mpipe_buffer_get_meta(in);
	m->bytes_used = len;
	m->parent = NULL;
	in->len = len;

	zassert_ok(sink_pad->process_fn(sink_pad, in, &out), "chain failed");

	return out;
}

static void expect_block(struct net_buf *block, const uint8_t *expected)
{
	zassert_not_null(block, "no block produced");
	zassert_equal(mpipe_buffer_get_meta(block)->bytes_used, BLOCK_SIZE, "block of %u bytes",
		      mpipe_buffer_get_meta(block)->bytes_used);
	zassert_mem_equal(block->data, expected, BLOCK_SIZE, "block content differs");
	zassert_true(block->data >= (uint8_t *)test_slab.buffer &&
			     block->data < (uint8_t *)test_slab.buffer + SLAB_BLOCKS * BLOCK_SIZE,
		     "the block is not a slab block");
}

/* The source capability is the format set by the properties, fixed */
ZTEST_F(mpipe_aud_raw_parser, test_the_format_comes_from_the_properties)
{
	struct mpipe_structure caps;
	const struct mpipe_value *v;

	zassert_ok(mpipe_pad_enum_caps(&fixture->parser.base.src_pad, 0, NULL, &caps));
	zassert_true(mpipe_structure_is_fixed(&caps), "the format must be fixed");

	v = mpipe_structure_get_value(&caps, MPIPE_CAPS_SAMPLE_RATE);
	zassert_equal(mpipe_value_get_uint(v), RATE, "sample rate");
	v = mpipe_structure_get_value(&caps, MPIPE_CAPS_BIT_WIDTH);
	zassert_equal(mpipe_value_get_uint(v), BITS, "bit width");
	v = mpipe_structure_get_value(&caps, MPIPE_CAPS_NUM_CHANNELS);
	zassert_equal(mpipe_value_get_uint(v), CHANNELS, "channels");
	v = mpipe_structure_get_value(&caps, MPIPE_CAPS_FRAME_INTERVAL);
	zassert_equal(mpipe_value_get_uint(v), INTERVAL, "frame interval");
	v = mpipe_structure_get_value(&caps, MPIPE_CAPS_INTERLEAVED);
	zassert_true(mpipe_value_get_boolean(v), "interleaved");

	zassert_equal(mpipe_pad_enum_caps(&fixture->parser.base.src_pad, 1, NULL, &caps), -ENOENT,
		      "one format only");
}

/* Input of any size comes out as whole blocks, bytes in order, nothing lost */
ZTEST_F(mpipe_aud_raw_parser, test_odd_sized_input_makes_whole_blocks)
{
	struct net_buf *out;

	zassert_is_null(feed(fixture, 0, 100), "a block out of 100 bytes");
	out = feed(fixture, 100, 500); /* 600 bytes in: one block, 280 pending */
	expect_block(out, fixture->stream);
	zassert_is_null(out->frags, "a second block out of 600 bytes");
	net_buf_unref(out);

	zassert_is_null(feed(fixture, 600, 7), "a block out of 607 bytes");
	out = feed(fixture, 607, 33); /* 640 bytes in: the second block completes */
	expect_block(out, fixture->stream + BLOCK_SIZE);
	net_buf_unref(out);
}

/* One large input yields a chain of blocks, and the tail waits for more */
ZTEST_F(mpipe_aud_raw_parser, test_a_large_input_makes_a_chain)
{
	struct net_buf *out = feed(fixture, 0, 2 * BLOCK_SIZE + 40);

	expect_block(out, fixture->stream);
	expect_block(out->frags, fixture->stream + BLOCK_SIZE);
	zassert_is_null(out->frags->frags, "a third block out of two and a bit");
	zassert_not_null(fixture->parser.block, "the 40-byte tail should be pending");
	zassert_equal(mpipe_buffer_get_meta(fixture->parser.block)->bytes_used, 40, "tail length");

	net_buf_unref(out);
	/* after() checks that teardown frees the pending block */
}

/* With every block held downstream, new bytes are dropped rather than waited for */
ZTEST_F(mpipe_aud_raw_parser, test_no_free_block_drops_the_input)
{
	struct net_buf *held = feed(fixture, 0, SLAB_BLOCKS * BLOCK_SIZE);
	struct net_buf *out;

	zassert_not_null(held, "no blocks produced");
	zassert_equal(k_mem_slab_num_free_get(&test_slab), 0, "all blocks should be out");

	out = feed(fixture, 0, BLOCK_SIZE);
	zassert_is_null(out, "a block appeared with none free");

	/* Releasing the held blocks puts them back for the next input */
	net_buf_unref(held);
	zassert_equal(k_mem_slab_num_free_get(&test_slab), SLAB_BLOCKS, "blocks not returned");
	out = feed(fixture, 0, BLOCK_SIZE);
	expect_block(out, fixture->stream);
	net_buf_unref(out);
}

/* The format is configuration for the next negotiation: not while streaming */
ZTEST_F(mpipe_aud_raw_parser, test_the_format_is_fixed_once_negotiated)
{
	uint32_t rate = 16000;
	uint32_t bits = 12;

	zassert_equal(mpipe_object_set_properties((struct mpipe_object *)&fixture->parser,
						  MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE, &rate,
						  MPIPE_PROP_LIST_END),
		      -EBUSY, "the rate changed while PAUSED");

	zassert_ok(element(fixture)->change_state(element(fixture),
						  MPIPE_STATE_CHANGE_PAUSED_TO_READY));
	element(fixture)->current_state = MPIPE_STATE_READY;
	zassert_equal(mpipe_object_set_properties((struct mpipe_object *)&fixture->parser,
						  MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH, &bits,
						  MPIPE_PROP_LIST_END),
		      -EINVAL, "a 12-bit width was accepted");
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fixture->parser,
					       MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE, &rate,
					       MPIPE_PROP_LIST_END),
		   "the rate did not change in READY");
	zassert_equal(fixture->parser.sample_rate, 16000, "rate not stored");

	/* Back where after() expects it */
	zassert_ok(element(fixture)->change_state(element(fixture),
						  MPIPE_STATE_CHANGE_READY_TO_PAUSED));
	element(fixture)->current_state = MPIPE_STATE_PAUSED;
}
