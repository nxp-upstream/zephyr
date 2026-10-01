/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The socket sink writes every buffer it receives to a connected socket the
 * application hands it. A socket pair stands in for the network: the
 * application source pushes on one end through the pipeline, the test reads
 * the other.
 */

#include <errno.h>

#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/net/mpipe_socket_sink.h>

ZBUS_MSG_SUBSCRIBER_DEFINE(test_sink_sub);

enum {
	PIPE_ID,
	SRC_ID,
	SINK_ID,
};

struct test_socket_sink_fixture {
	struct mpipe pipeline;
	struct mpipe_app_src src;
	struct mpipe_socket_sink sink;
	struct zbus_channel *bus;
	int fds[2];
};

static void *socket_sink_setup(void)
{
	static struct test_socket_sink_fixture fixture;

	return &fixture;
}

static void socket_sink_before(void *f)
{
	struct test_socket_sink_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));
	fix->fds[0] = -1;
	fix->fds[1] = -1;

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&fix->src, SRC_ID));
	zassert_ok(mpipe_socket_sink_init(&fix->sink, SINK_ID));

	zassert_ok(mpipe_bin_add((struct mpipe_bin *)&fix->pipeline,
				 (struct mpipe_element *)&fix->src,
				 (struct mpipe_element *)&fix->sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&fix->src,
				      (struct mpipe_element *)&fix->sink, NULL));

	fix->bus = mpipe_element_get_bus_chan((struct mpipe_element *)&fix->pipeline);
	zassert_ok(zbus_chan_add_obs(fix->bus, &test_sink_sub, K_FOREVER));
}

static void socket_sink_after(void *f)
{
	struct test_socket_sink_fixture *fix = f;

	(void)mpipe_element_set_state((struct mpipe_element *)&fix->pipeline, MPIPE_STATE_READY);
	(void)zbus_chan_rm_obs(fix->bus, &test_sink_sub, K_FOREVER);
	if (fix->fds[1] >= 0) {
		(void)zsock_close(fix->fds[1]);
	}
}

ZTEST_SUITE(test_socket_sink, NULL, socket_sink_setup, socket_sink_before, socket_sink_after, NULL);

/* A fresh connection: the pipeline writes fds[0], the test reads fds[1] */
static void connect_and_play(struct test_socket_sink_fixture *fix)
{
	int res = zsock_socketpair(NET_AF_UNIX, NET_SOCK_STREAM, 0, fix->fds);

	zassert_equal(res, 0, "socketpair returned %d (errno %d)", res, errno);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->sink,
					       MPIPE_PROP_SOCKET_SINK_FD, &fix->fds[0],
					       MPIPE_PROP_LIST_END),
		   "handing the socket to the sink failed");
	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fix->pipeline,
					   MPIPE_STATE_PLAYING),
		   "PLAYING failed");
}

/* Read exactly @p len bytes, or fail */
static void read_all(int fd, uint8_t *data, uint32_t len)
{
	while (len > 0U) {
		ssize_t n = zsock_recv(fd, data, len, 0);

		zassert_true(n > 0, "recv returned %d (errno %d)", (int)n, errno);
		data += n;
		len -= (uint32_t)n;
	}
}

static void expect_message(uint32_t type)
{
	const struct zbus_channel *chan;
	struct mpipe_message msg;

	zassert_ok(zbus_sub_wait_msg(&test_sink_sub, &chan, &msg, K_SECONDS(2)),
		   "no message on the bus");
	zassert_equal(msg.type, type, "expected message %u, got %u", type, msg.type);
}

/* Every pushed buffer comes out of the socket, whole and in order */
ZTEST_F(test_socket_sink, test_bytes_come_out_in_order)
{
	uint8_t stream[3000];
	uint8_t got[sizeof(stream)];

	for (uint32_t i = 0; i < sizeof(stream); i++) {
		stream[i] = (uint8_t)(i * 13U);
	}

	connect_and_play(fixture);

	zassert_ok(mpipe_app_src_push(&fixture->src, stream, 100, K_SECONDS(1)));
	zassert_ok(mpipe_app_src_push(&fixture->src, stream + 100, 2000, K_SECONDS(1)));
	zassert_ok(mpipe_app_src_push(&fixture->src, stream + 2100, 900, K_SECONDS(1)));

	read_all(fixture->fds[1], got, sizeof(got));
	zassert_mem_equal(got, stream, sizeof(stream), "the bytes differ or are reordered");

	/* The end of the stream reaches the bus once the sink has written everything */
	zassert_ok(mpipe_app_src_eos(&fixture->src, K_SECONDS(1)));
	expect_message(MPIPE_MESSAGE_EOS);
}

/* A peer that went away fails the stream rather than hanging it */
ZTEST_F(test_socket_sink, test_closed_peer_fails_the_stream)
{
	uint8_t data[64] = {0};

	connect_and_play(fixture);

	(void)zsock_close(fixture->fds[1]);
	fixture->fds[1] = -1;

	zassert_ok(mpipe_app_src_push(&fixture->src, data, sizeof(data), K_SECONDS(1)));
	expect_message(MPIPE_MESSAGE_ERROR);
}

/* A new socket for every run: the previous one is closed by the sink on teardown */
ZTEST_F(test_socket_sink, test_a_new_socket_each_run)
{
	uint8_t data[32];
	uint8_t got[sizeof(data)];

	for (int run = 0; run < 3; run++) {
		memset(data, (uint8_t)(run + 1), sizeof(data));

		connect_and_play(fixture);
		zassert_ok(mpipe_app_src_push(&fixture->src, data, sizeof(data), K_SECONDS(1)));
		read_all(fixture->fds[1], got, sizeof(got));
		zassert_mem_equal(got, data, sizeof(data), "run %d: data differs", run);

		zassert_ok(mpipe_element_set_state((struct mpipe_element *)&fixture->pipeline,
						   MPIPE_STATE_READY),
			   "run %d: READY failed", run);
		zassert_equal(fixture->sink.fd, -1, "run %d: the sink kept the socket", run);

		(void)zsock_close(fixture->fds[1]);
		fixture->fds[1] = -1;
	}
}
