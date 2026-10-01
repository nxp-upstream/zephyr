/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The socket source reads a connected socket the application hands it. A
 * socket pair stands in for the network: the test writes on one end, the
 * pipeline reads the other. Behind it, the JPEG parser must cut frames out
 * of the stream without copying the ones that lie inside one receive chunk.
 */

#include <errno.h>

#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>
#include <zephyr/mpipe/net/mpipe_socket_src.h>

ZBUS_MSG_SUBSCRIBER_DEFINE(test_net_sub);

enum {
	PIPE_ID,
	SRC_ID,
	PARSER_ID,
	SINK_ID,
};

#define MAX_DELIVERIES 16
#define STREAM_MAX     8192

/* What the sink saw */
static struct {
	atomic_t count;
	uint32_t total;
	uint8_t bytes[STREAM_MAX];
	uint32_t len[MAX_DELIVERIES];
	bool is_view[MAX_DELIVERIES];
	bool from_src_chunk[MAX_DELIVERIES];
} seen;

static struct mpipe_socket_src *seen_src;

static void sink_cb(const struct net_buf *buf, void *user_data)
{
	const struct mpipe_buffer_meta *m = mpipe_buffer_get_meta(buf);
	uint32_t i = (uint32_t)atomic_inc(&seen.count);

	ARG_UNUSED(user_data);

	if (seen.total + m->bytes_used <= STREAM_MAX) {
		memcpy(seen.bytes + seen.total, buf->data, m->bytes_used);
	}
	seen.total += m->bytes_used;

	if (i < MAX_DELIVERIES) {
		seen.len[i] = m->bytes_used;
		seen.is_view[i] = (m->parent != NULL);
		seen.from_src_chunk[i] =
			(m->parent != NULL) &&
			(mpipe_buffer_get_meta(m->parent)->pool == &seen_src->pool);
	}
}

struct test_socket_src_fixture {
	struct mpipe pipeline;
	struct mpipe_socket_src src;
	struct mpipe_img_jpeg_parser parser;
	struct mpipe_app_sink sink;
	struct zbus_channel *bus;
	int fds[2];
};

static void *socket_src_setup(void)
{
	static struct test_socket_src_fixture fixture;

	return &fixture;
}

static void socket_src_before(void *f)
{
	struct test_socket_src_fixture *fix = f;
	static const struct mpipe_app_sink_cb cb = {.fn = sink_cb};

	memset(fix, 0, sizeof(*fix));
	memset(&seen, 0, sizeof(seen));
	seen_src = &fix->src;
	fix->fds[0] = -1;
	fix->fds[1] = -1;

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_socket_src_init(&fix->src, SRC_ID));
	zassert_ok(mpipe_img_jpeg_parser_init(&fix->parser, PARSER_ID));
	zassert_ok(mpipe_app_sink_init(&fix->sink, SINK_ID));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->sink,
					       MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));

	fix->bus = mpipe_element_get_bus_chan((struct mpipe_element *)&fix->pipeline);
	zassert_ok(zbus_chan_add_obs(fix->bus, &test_net_sub, K_FOREVER));
}

static void socket_src_after(void *f)
{
	struct test_socket_src_fixture *fix = f;

	(void)mpipe_element_set_state((struct mpipe_element *)&fix->pipeline, MPIPE_STATE_READY);
	(void)zbus_chan_rm_obs(fix->bus, &test_net_sub, K_FOREVER);
	if (fix->fds[1] >= 0) {
		(void)zsock_close(fix->fds[1]);
	}
}

ZTEST_SUITE(test_socket_src, NULL, socket_src_setup, socket_src_before, socket_src_after, NULL);

/* Link the source straight to the sink, or through the parser */
static void build(struct test_socket_src_fixture *fix, bool with_parser)
{
	struct mpipe_element *src = (struct mpipe_element *)&fix->src;
	struct mpipe_element *parser = (struct mpipe_element *)&fix->parser;
	struct mpipe_element *sink = (struct mpipe_element *)&fix->sink;

	if (with_parser) {
		zassert_ok(
			mpipe_bin_add((struct mpipe_bin *)&fix->pipeline, src, parser, sink, NULL));
		zassert_ok(mpipe_element_link(src, parser, sink, NULL));
	} else {
		zassert_ok(mpipe_bin_add((struct mpipe_bin *)&fix->pipeline, src, sink, NULL));
		zassert_ok(mpipe_element_link(src, sink, NULL));
	}
}

/* A fresh connection: the pipeline reads fds[0], the test writes fds[1] */
static void connect_and_play(struct test_socket_src_fixture *fix)
{
	int res = zsock_socketpair(NET_AF_UNIX, NET_SOCK_STREAM, 0, fix->fds);

	zassert_equal(res, 0, "socketpair returned %d (errno %d), fds %d %d", res, errno,
		      fix->fds[0], fix->fds[1]);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->src,
					       MPIPE_PROP_SOCKET_SRC_FD, &fix->fds[0],
					       MPIPE_PROP_LIST_END),
		   "handing the socket to the source failed");
	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fix->pipeline,
					   MPIPE_STATE_PLAYING),
		   "PLAYING failed");
}

static void write_all(int fd, const uint8_t *data, uint32_t len)
{
	while (len > 0U) {
		ssize_t n = zsock_send(fd, data, len, 0);

		zassert_true(n > 0, "send failed (%d)", errno);
		data += n;
		len -= (uint32_t)n;
	}
}

/* Closing the writer ends the stream: one EOS on the bus */
static void close_and_expect_eos(struct test_socket_src_fixture *fix)
{
	const struct zbus_channel *chan;
	struct mpipe_message msg;

	(void)zsock_close(fix->fds[1]);
	fix->fds[1] = -1;

	zassert_ok(zbus_sub_wait_msg(&test_net_sub, &chan, &msg, K_SECONDS(2)),
		   "no message after the writer closed");
	zassert_equal(msg.type, MPIPE_MESSAGE_EOS, "expected EOS, got %d", msg.type);
}

/* A test frame: a start marker, a payload byte pattern, an end marker */
static uint32_t make_frame(uint8_t *dst, uint8_t seed, uint32_t payload)
{
	dst[0] = 0xFF;
	dst[1] = 0xD8;
	for (uint32_t i = 0; i < payload; i++) {
		dst[2 + i] = (uint8_t)(seed + i) == 0xFF ? 0x00 : (uint8_t)(seed + i);
	}
	dst[2 + payload] = 0xFF;
	dst[3 + payload] = 0xD9;

	return payload + 4U;
}

/* Everything written arrives, in order, as views of the source's chunks */
ZTEST_F(test_socket_src, test_bytes_arrive_in_order_as_views)
{
	uint8_t stream[3000];

	for (uint32_t i = 0; i < sizeof(stream); i++) {
		stream[i] = (uint8_t)(i * 7U);
	}

	build(fixture, false);
	connect_and_play(fixture);

	write_all(fixture->fds[1], stream, 100);
	write_all(fixture->fds[1], stream + 100, 2000);
	write_all(fixture->fds[1], stream + 2100, 900);

	close_and_expect_eos(fixture);

	zassert_equal(seen.total, sizeof(stream), "received %u bytes, wrote %u", seen.total,
		      (uint32_t)sizeof(stream));
	zassert_mem_equal(seen.bytes, stream, sizeof(stream), "the bytes differ or are reordered");
	for (uint32_t i = 0; i < MIN((uint32_t)atomic_get(&seen.count), MAX_DELIVERIES); i++) {
		zassert_true(seen.from_src_chunk[i], "delivery %u is not a view of a chunk", i);
	}
}

/* Frames inside one chunk reach the sink as views of the source's chunk: no copy anywhere */
ZTEST_F(test_socket_src, test_frames_are_views_of_the_receive_chunk)
{
	uint8_t stream[1200];
	uint32_t a = make_frame(stream, 1, 500);
	uint32_t b = make_frame(stream + a, 2, 400);

	build(fixture, true);
	connect_and_play(fixture);

	/* Odd-sized writes: the second frame's marker arrives on its own */
	write_all(fixture->fds[1], stream, 333);
	write_all(fixture->fds[1], stream + 333, a + b - 333 - 1);
	write_all(fixture->fds[1], stream + a + b - 1, 1);

	close_and_expect_eos(fixture);

	zassert_equal(atomic_get(&seen.count), 2, "expected 2 frames, got %ld",
		      atomic_get(&seen.count));
	zassert_equal(seen.len[0], a, "frame A length");
	zassert_equal(seen.len[1], b, "frame B length");
	zassert_mem_equal(seen.bytes, stream, a + b, "frame content differs");
	zassert_true(seen.from_src_chunk[0], "frame A was copied");
	zassert_true(seen.from_src_chunk[1], "frame B was copied");
}

/* More than a chunk of frames: the ones split across chunks are copied, the rest are views */
ZTEST_F(test_socket_src, test_frames_across_chunks_are_copied_intact)
{
	static uint8_t stream[7000];
	uint32_t total = 0;
	uint32_t views = 0;
	uint32_t copies = 0;

	for (uint8_t i = 0; i < 7; i++) {
		total += make_frame(stream + total, i * 10, 996);
	}

	build(fixture, true);
	connect_and_play(fixture);
	write_all(fixture->fds[1], stream, total);
	close_and_expect_eos(fixture);

	zassert_equal(atomic_get(&seen.count), 7, "expected 7 frames, got %ld",
		      atomic_get(&seen.count));
	zassert_equal(seen.total, total, "frame bytes lost");
	zassert_mem_equal(seen.bytes, stream, total, "frame content differs");

	for (uint32_t i = 0; i < 7; i++) {
		if (seen.is_view[i]) {
			views++;
		} else {
			copies++;
		}
	}
	zassert_true(copies >= 1, "7000 bytes in 4 KB chunks must split at least one frame");
	zassert_true(views >= 4, "most frames fit in a chunk and must be views, got %u", views);
}

/* A new socket for every run: the previous one is closed by the source on teardown */
ZTEST_F(test_socket_src, test_a_new_socket_each_run)
{
	uint8_t stream[64];
	uint32_t len = make_frame(stream, 3, 60);

	build(fixture, true);

	for (int run = 0; run < 3; run++) {
		memset(&seen, 0, sizeof(seen));

		connect_and_play(fixture);
		write_all(fixture->fds[1], stream, len);
		close_and_expect_eos(fixture);

		zassert_equal(atomic_get(&seen.count), 1, "run %d: expected 1 frame", run);
		zassert_mem_equal(seen.bytes, stream, len, "run %d: frame differs", run);

		zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
						   MPIPE_STATE_READY),
			   "run %d: READY failed", run);
		zassert_equal(fixture->src.fd, -1, "run %d: the source kept the socket", run);
	}
}
