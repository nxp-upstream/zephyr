/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The JPEG parser cuts a byte stream at the end-of-image markers. These tests
 * feed it the way a source does: a chunk the bytes were received into, pushed
 * as one view per read, in whatever sizes the reads happened to have. A frame
 * inside one chunk must come out as a view of that chunk; a frame split
 * across two chunks, or a marker split across two reads, must still come out
 * right, as a copy.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>

#define CHUNK_SIZE 2048

/* The source's receive chunks */
NET_BUF_POOL_FIXED_DEFINE(chunk_pool, 2, CHUNK_SIZE, sizeof(struct mpipe_buffer_meta),
			  mpipe_buffer_destroy);

struct mpipe_img_jpeg_parser_fixture {
	struct mpipe_img_jpeg_parser parser;
	struct net_buf *chunk;
	uint32_t fill;
};

static void *jpeg_parser_setup(void)
{
	static struct mpipe_img_jpeg_parser_fixture fixture;

	return &fixture;
}

static struct net_buf *new_chunk(void)
{
	struct net_buf *chunk = net_buf_alloc_len(&chunk_pool, CHUNK_SIZE, K_NO_WAIT);
	struct mpipe_buffer_meta *m;

	zassert_not_null(chunk, "chunk allocation failed");
	m = mpipe_buffer_get_meta(chunk);
	m->bytes_used = 0;
	m->timestamp = 0;
	m->parent = NULL;
	chunk->len = 0;

	return chunk;
}

static void jpeg_parser_before(void *f)
{
	struct mpipe_img_jpeg_parser_fixture *fix = f;
	struct mpipe_dispatch query = {.type = MPIPE_DISPATCH_BUFFER_POOL};
	struct mpipe_element *element;

	memset(fix, 0, sizeof(*fix));

	zassert_ok(mpipe_img_jpeg_parser_init(&fix->parser, 1), "parser init failed");
	element = &fix->parser.base.element;

	/* Nothing downstream proposes a pool: frames are views, copies go to the own pool */
	zassert_ok(fix->parser.base.decide_buffer_pool(&fix->parser.base, &query),
		   "pool decision failed");
	zassert_ok(element->change_state(element, MPIPE_STATE_CHANGE_READY_TO_PAUSED),
		   "READY to PAUSED failed");

	fix->chunk = new_chunk();
}

static void jpeg_parser_after(void *f)
{
	struct mpipe_img_jpeg_parser_fixture *fix = f;
	struct mpipe_element *element = &fix->parser.base.element;

	zassert_ok(element->change_state(element, MPIPE_STATE_CHANGE_PAUSED_TO_READY),
		   "PAUSED to READY failed");
	if (fix->chunk != NULL) {
		net_buf_unref(fix->chunk);
	}
}

ZTEST_SUITE(mpipe_img_jpeg_parser, NULL, jpeg_parser_setup, jpeg_parser_before, jpeg_parser_after,
	    NULL);

/*
 * Receive @p len bytes of @p data into the current chunk and push them to the
 * parser as the source would: one view over the new bytes. Returns what the
 * parser produced, a chain of frames or NULL.
 */
static struct net_buf *receive(struct mpipe_img_jpeg_parser_fixture *fix, const uint8_t *data,
			       uint32_t len)
{
	struct mpipe_pad *sink_pad = &fix->parser.base.sink_pad;
	struct mpipe_buffer_meta *m = mpipe_buffer_get_meta(fix->chunk);
	struct net_buf *view;
	struct net_buf *out = NULL;

	zassert_true(fix->fill + len <= CHUNK_SIZE, "test chunk overflow");
	memcpy(fix->chunk->data + fix->fill, data, len);
	m->bytes_used = fix->fill + len;
	fix->chunk->len = m->bytes_used;

	view = mpipe_buffer_view(fix->chunk, fix->fill, len, K_NO_WAIT);
	zassert_not_null(view, "view creation failed");
	fix->fill += len;

	zassert_ok(sink_pad->process_fn(sink_pad, view, &out), "chain failed");

	return out;
}

/* The source moved on to a new chunk, having let go of the old one */
static void next_chunk(struct mpipe_img_jpeg_parser_fixture *fix)
{
	net_buf_unref(fix->chunk);
	fix->chunk = new_chunk();
	fix->fill = 0;
}

/* A test frame: a start marker, a payload byte pattern, an end marker */
static uint32_t make_frame(uint8_t *dst, uint8_t seed, uint32_t payload)
{
	dst[0] = 0xFF;
	dst[1] = 0xD8;
	for (uint32_t i = 0; i < payload; i++) {
		/* Never produce FF D9 inside the payload */
		dst[2 + i] = (uint8_t)(seed + i) == 0xFF ? 0x00 : (uint8_t)(seed + i);
	}
	dst[2 + payload] = 0xFF;
	dst[3 + payload] = 0xD9;

	return payload + 4U;
}

static void expect_frame(struct net_buf *frame, const uint8_t *expected, uint32_t len)
{
	zassert_not_null(frame, "no frame produced");
	zassert_equal(mpipe_buffer_get_meta(frame)->bytes_used, len, "frame length %u, expected %u",
		      mpipe_buffer_get_meta(frame)->bytes_used, len);
	zassert_mem_equal(frame->data, expected, len, "frame content differs");
}

static bool points_into(const struct net_buf *frame, const struct net_buf *chunk)
{
	return frame->data >= chunk->data && frame->data < chunk->data + CHUNK_SIZE;
}

/* A frame received in several reads comes out once, as a view of the chunk */
ZTEST_F(mpipe_img_jpeg_parser, test_a_frame_in_one_chunk_is_a_view)
{
	uint8_t frame[300];
	uint32_t len = make_frame(frame, 1, 296);
	struct net_buf *out;

	zassert_is_null(receive(fixture, frame, 100), "a frame came out of an incomplete input");
	zassert_is_null(receive(fixture, frame + 100, 100), "a frame came out early");
	out = receive(fixture, frame + 200, len - 200);

	expect_frame(out, frame, len);
	zassert_true(points_into(out, fixture->chunk), "the frame is not a view of the chunk");
	zassert_equal_ptr(mpipe_buffer_get_meta(out)->parent, fixture->chunk,
			  "the frame does not reference the chunk");
	zassert_is_null(out->frags, "more than one frame came out");

	net_buf_unref(out);
}

/* Several frames in one read come out as a chain, in order, all views */
ZTEST_F(mpipe_img_jpeg_parser, test_several_frames_in_one_read)
{
	uint8_t stream[600];
	uint32_t a = make_frame(stream, 10, 100);
	uint32_t b = make_frame(stream + a, 20, 200);
	uint32_t c = make_frame(stream + a + b, 30, 50);
	struct net_buf *out = receive(fixture, stream, a + b + c);

	expect_frame(out, stream, a);
	expect_frame(out->frags, stream + a, b);
	expect_frame(out->frags->frags, stream + a + b, c);
	zassert_is_null(out->frags->frags->frags, "a fourth frame came out");
	zassert_true(points_into(out->frags->frags, fixture->chunk), "frames are not views");

	net_buf_unref(out);
}

/* The end marker itself may be split across two reads */
ZTEST_F(mpipe_img_jpeg_parser, test_a_marker_split_across_two_reads)
{
	uint8_t frame[64];
	uint32_t len = make_frame(frame, 5, 60);
	struct net_buf *out;

	zassert_is_null(receive(fixture, frame, len - 1), "a frame came out on half a marker");
	out = receive(fixture, frame + len - 1, 1);

	expect_frame(out, frame, len);
	net_buf_unref(out);
}

/* The bytes after a frame's marker are the next frame's head and are kept */
ZTEST_F(mpipe_img_jpeg_parser, test_the_next_frame_starts_right_after_the_marker)
{
	uint8_t stream[400];
	uint32_t a = make_frame(stream, 1, 100);
	uint32_t b = make_frame(stream + a, 2, 100);
	struct net_buf *out;

	/* One read holds all of A and the first 50 bytes of B */
	out = receive(fixture, stream, a + 50);
	expect_frame(out, stream, a);
	zassert_is_null(out->frags, "B came out incomplete");
	net_buf_unref(out);

	out = receive(fixture, stream + a + 50, b - 50);
	expect_frame(out, stream + a, b);
	zassert_true(points_into(out, fixture->chunk), "B is not a view of the chunk");
	net_buf_unref(out);
}

/* A frame split across two chunks cannot be a view, so it is copied, intact */
ZTEST_F(mpipe_img_jpeg_parser, test_a_frame_across_two_chunks_is_copied)
{
	uint8_t frame[300];
	uint32_t len = make_frame(frame, 7, 296);
	struct net_buf *first_chunk = fixture->chunk;
	struct net_buf *out;

	zassert_is_null(receive(fixture, frame, 120), "a frame came out early");
	next_chunk(fixture);
	out = receive(fixture, frame + 120, len - 120);

	expect_frame(out, frame, len);
	zassert_false(points_into(out, first_chunk), "a frame across chunks cannot be a view");
	zassert_false(points_into(out, fixture->chunk), "a frame across chunks cannot be a view");
	zassert_is_null(mpipe_buffer_get_meta(out)->parent, "a copy has no parent");
	zassert_equal_ptr(mpipe_buffer_get_meta(out)->pool, &fixture->parser.out_pool,
			  "the copy is not from the parser's pool");

	net_buf_unref(out);
}

/* The chunk returns to the source only when the parser and the frames let go */
ZTEST_F(mpipe_img_jpeg_parser, test_the_chunk_lives_as_long_as_its_frames)
{
	uint8_t stream[300];
	uint32_t a = make_frame(stream, 1, 100);
	struct net_buf *out;

	/* Frame A and the head of the next frame: both pin the chunk */
	out = receive(fixture, stream, a + 20);
	zassert_equal(fixture->chunk->ref, 3, "frame and pending head should hold the chunk");

	net_buf_unref(out);
	zassert_equal(fixture->chunk->ref, 2, "the pending head should still hold the chunk");

	/* Teardown releases the pending head */
	zassert_ok(fixture->parser.base.element.change_state(&fixture->parser.base.element,
							     MPIPE_STATE_CHANGE_PAUSED_TO_READY),
		   "PAUSED to READY failed");
	zassert_equal(fixture->chunk->ref, 1, "teardown did not release the pending head");

	/* Put the element back where after() expects it */
	zassert_ok(fixture->parser.base.element.change_state(&fixture->parser.base.element,
							     MPIPE_STATE_CHANGE_READY_TO_PAUSED),
		   "READY to PAUSED failed");
}

/* Bytes longer than a frame with no marker are dropped, and the parser resyncs */
ZTEST_F(mpipe_img_jpeg_parser, test_garbage_longer_than_a_frame_is_dropped)
{
	uint8_t garbage[1100];
	uint8_t frame[64];
	uint32_t len = make_frame(frame, 9, 60);
	struct net_buf *out;

	memset(garbage, 0x11, sizeof(garbage));

	zassert_is_null(receive(fixture, garbage, 600), "garbage produced a frame");
	zassert_is_null(receive(fixture, garbage + 600, 500), "garbage produced a frame");
	zassert_equal(fixture->chunk->ref, 1, "the dropped bytes still pin the chunk");

	out = receive(fixture, frame, len);
	expect_frame(out, frame, len);
	net_buf_unref(out);
}
