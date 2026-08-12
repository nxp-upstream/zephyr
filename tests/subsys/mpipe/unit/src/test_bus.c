/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe_bus.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_message.h>

struct mpipe_bus_api_fixture {
	struct mpipe_bus bus;
	struct mpipe_element elem;
};

static void *bus_suite_setup(void)
{
	static struct mpipe_bus_api_fixture fixture;

	return &fixture;
}

static void bus_before(void *f)
{
	struct mpipe_bus_api_fixture *fix = f;
	struct mpipe_message out;

	mpipe_bus_init(&fix->bus);
	memset(&fix->elem, 0, sizeof(fix->elem));
	mpipe_element_init(&fix->elem, 42);

	zassert_equal(mpipe_bus_peek(&fix->bus, &out), -ENOMSG, "bus not empty after init");
}

ZTEST_SUITE(mpipe_bus_api, NULL, bus_suite_setup, bus_before, NULL, NULL);

static int handler_call_count;
static enum mpipe_message_type handler_last_type;
static struct mpipe_element *handler_last_src;

/* Sync handler that records what it saw and drops every message it is given. */
static enum mpipe_bus_sync_reply test_drop_handler(struct mpipe_bus *bus,
						   struct mpipe_message *message, void *user_data)
{
	ARG_UNUSED(bus);
	ARG_UNUSED(user_data);

	handler_call_count++;
	handler_last_type = message->type;
	handler_last_src = message->origin;

	return MPIPE_BUS_DROP;
}

/* Sync handler that records what it saw and passes every message through. */
static enum mpipe_bus_sync_reply test_pass_handler(struct mpipe_bus *bus,
						   struct mpipe_message *message, void *user_data)
{
	ARG_UNUSED(bus);
	ARG_UNUSED(user_data);

	handler_call_count++;
	handler_last_type = message->type;
	handler_last_src = message->origin;

	return MPIPE_BUS_PASS;
}

ZTEST_F(mpipe_bus_api, test_post_peek_pop)
{
	struct mpipe_element *src = &fixture->elem;
	struct mpipe_message msg = {.origin = src, .type = MPIPE_MESSAGE_EOS};
	struct mpipe_message peeked;
	struct mpipe_message popped;

	zassert_ok(mpipe_bus_post(&fixture->bus, &msg), "mpipe_bus_post failed");

	zassert_ok(mpipe_bus_peek(&fixture->bus, &peeked), "mpipe_bus_peek failed");
	zassert_equal(peeked.type, MPIPE_MESSAGE_EOS, "peeked type != EOS");
	zassert_equal(peeked.origin, src, "peeked src mismatch");

	zassert_ok(mpipe_bus_pop(&fixture->bus, &popped), "mpipe_bus_pop failed");
	zassert_equal(popped.type, peeked.type, "peek and pop type mismatch");
	zassert_equal(popped.origin, peeked.origin, "peek and pop src mismatch");

	zassert_equal(mpipe_bus_peek(&fixture->bus, &peeked), -ENOMSG, "bus not empty after pop");
}

ZTEST_F(mpipe_bus_api, test_post_multiple_fifo_order)
{
	struct mpipe_element *src = &fixture->elem;
	struct mpipe_message msg1 = {.origin = src, .type = MPIPE_MESSAGE_EOS};
	struct mpipe_message msg2 = {.type = MPIPE_MESSAGE_ERROR};
	struct mpipe_message first;
	struct mpipe_message second;

	mpipe_bus_post(&fixture->bus, &msg1);
	mpipe_bus_post(&fixture->bus, &msg2);

	zassert_ok(mpipe_bus_pop(&fixture->bus, &first));
	zassert_ok(mpipe_bus_pop(&fixture->bus, &second));

	zassert_equal(first.type, MPIPE_MESSAGE_EOS, "first type != EOS");
	zassert_equal(first.origin, src, "first src mismatch");
	zassert_equal(second.type, MPIPE_MESSAGE_ERROR, "second type != ERROR");
	zassert_is_null(second.origin, "second src != NULL");
}

ZTEST_F(mpipe_bus_api, test_sanity)
{
	struct mpipe_message msg = {.type = MPIPE_MESSAGE_EOS};
	struct mpipe_message out;

	zassert_true(mpipe_bus_post(NULL, &msg) < 0, "post to NULL bus did not fail");
	zassert_true(mpipe_bus_post(&fixture->bus, NULL) < 0, "post NULL msg did not fail");
	zassert_true(mpipe_bus_peek(NULL, &out) < 0, "peek on NULL bus did not fail");
	zassert_true(mpipe_bus_peek(&fixture->bus, NULL) < 0, "peek with NULL out did not fail");
	zassert_true(mpipe_bus_pop(NULL, &out) < 0, "pop on NULL bus did not fail");
	zassert_true(mpipe_bus_pop(&fixture->bus, NULL) < 0, "pop with NULL out did not fail");
}

ZTEST_F(mpipe_bus_api, test_pop_msg_filters_by_type)
{
	struct mpipe_element *src = &fixture->elem;
	struct mpipe_message eos = {.origin = src, .type = MPIPE_MESSAGE_EOS};
	struct mpipe_message err = {.origin = src, .type = MPIPE_MESSAGE_ERROR};
	struct mpipe_message found;

	mpipe_bus_post(&fixture->bus, &eos);
	mpipe_bus_post(&fixture->bus, &err);

	zassert_ok(mpipe_bus_pop_msg(&fixture->bus, MPIPE_MESSAGE_ERROR, &found));
	zassert_equal(found.type, MPIPE_MESSAGE_ERROR, "found type != ERROR");
	zassert_equal(found.origin, src, "found src mismatch");
}

ZTEST_F(mpipe_bus_api, test_flush_clears_all)
{
	struct mpipe_message msg1 = {.type = MPIPE_MESSAGE_EOS};
	struct mpipe_message msg2 = {.type = MPIPE_MESSAGE_ERROR};
	struct mpipe_message out;

	mpipe_bus_post(&fixture->bus, &msg1);
	mpipe_bus_post(&fixture->bus, &msg2);

	mpipe_bus_flush(&fixture->bus);

	zassert_equal(mpipe_bus_peek(&fixture->bus, &out), -ENOMSG, "bus not empty after flush");
}

ZTEST_F(mpipe_bus_api, test_sync_handler_pass_enqueues)
{
	struct mpipe_element *src = &fixture->elem;
	struct mpipe_message msg = {.origin = src, .type = MPIPE_MESSAGE_EOS};
	struct mpipe_message out;

	handler_call_count = 0;
	handler_last_type = MPIPE_MESSAGE_UNKNOWN;
	handler_last_src = NULL;

	zassert_ok(mpipe_bus_set_sync_handler(&fixture->bus, test_pass_handler, NULL),
		   "installing sync handler failed");

	mpipe_bus_post(&fixture->bus, &msg);

	/* Handler ran once and saw the message. */
	zassert_equal(handler_call_count, 1, "handler call count != 1");
	zassert_equal(handler_last_type, MPIPE_MESSAGE_EOS, "handler type != EOS");
	zassert_equal(handler_last_src, src, "handler src mismatch");

	/* MPIPE_BUS_PASS means the message must be enqueued for the consumer. */
	zassert_ok(mpipe_bus_peek(&fixture->bus, &out), "PASS did not enqueue the message");
	zassert_equal(out.type, MPIPE_MESSAGE_EOS, "enqueued type != EOS");
}

ZTEST_F(mpipe_bus_api, test_sync_handler_drop_discards)
{
	struct mpipe_element *src = &fixture->elem;
	struct mpipe_message msg = {.origin = src, .type = MPIPE_MESSAGE_EOS};
	struct mpipe_message out;

	handler_call_count = 0;

	zassert_ok(mpipe_bus_set_sync_handler(&fixture->bus, test_drop_handler, NULL),
		   "installing sync handler failed");

	zassert_ok(mpipe_bus_post(&fixture->bus, &msg), "post with dropping handler failed");

	/* Handler ran, but MPIPE_BUS_DROP means nothing is enqueued. */
	zassert_equal(handler_call_count, 1, "handler call count != 1");
	zassert_equal(mpipe_bus_peek(&fixture->bus, &out), -ENOMSG,
		      "DROP still enqueued the message");
}

ZTEST_F(mpipe_bus_api, test_sync_handler_clear)
{
	struct mpipe_element *src = &fixture->elem;
	struct mpipe_message msg = {.origin = src, .type = MPIPE_MESSAGE_EOS};
	struct mpipe_message out;

	handler_call_count = 0;

	/* Install then clear: posting must no longer invoke the handler and the
	 * message must be enqueued (default PASS behavior).
	 */
	zassert_ok(mpipe_bus_set_sync_handler(&fixture->bus, test_drop_handler, NULL));
	zassert_ok(mpipe_bus_set_sync_handler(&fixture->bus, NULL, NULL),
		   "clearing handler failed");

	mpipe_bus_post(&fixture->bus, &msg);

	zassert_equal(handler_call_count, 0, "cleared handler was still called");
	zassert_ok(mpipe_bus_peek(&fixture->bus, &out),
		   "message not enqueued after clearing handler");
}
