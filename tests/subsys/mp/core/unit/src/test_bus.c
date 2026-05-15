/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/core/mp_bus.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_messages.h>

extern struct k_heap _system_heap;

struct mp_bus_api_fixture {
	struct mp_bus bus;
	struct mp_element elem;
	struct sys_memory_stats mem_before;
};

static void *bus_suite_setup(void)
{
	static struct mp_bus_api_fixture fixture;

	return &fixture;
}

static void bus_before(void *f)
{
	struct mp_bus_api_fixture *fix = f;

	mp_bus_init(&fix->bus);
	memset(&fix->elem, 0, sizeof(fix->elem));
	mp_element_init(&fix->elem, 42);

	zassert_is_null(mp_bus_peek(&fix->bus), "Freshly initialized bus shall have no messages");

	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);
}

static void bus_after(void *f)
{
	struct mp_bus_api_fixture *fix = f;
	struct sys_memory_stats mem_after;
	struct mp_bus_sync_listener *l;
	struct mp_bus_sync_listener *tmp;

	mp_bus_flush(&fix->bus);

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&fix->bus.sync_listeners, l, tmp, node) {
		mp_bus_remove_sync_listener(&fix->bus, l);
	}

	sys_heap_runtime_stats_get(&_system_heap.heap, &mem_after);
	zassert_equal(fix->mem_before.allocated_bytes, mem_after.allocated_bytes,
		      "Memory leak detected: before=%zu after=%zu", fix->mem_before.allocated_bytes,
		      mem_after.allocated_bytes);
}

ZTEST_SUITE(mp_bus_api, NULL, bus_suite_setup, bus_before, bus_after, NULL);

static int listener_call_count;
static enum mp_message_type listener_last_type;
static struct mp_object *listener_last_src;

static bool test_listener_cb(struct mp_message *message, void *data)
{
	listener_call_count++;
	listener_last_type = message->type;
	listener_last_src = message->src;

	return true;
}

ZTEST_F(mp_bus_api, test_post_peek_pop)
{
	struct mp_object *src = (struct mp_object *)&fixture->elem;
	struct mp_message *msg = mp_message_new(MP_MESSAGE_EOS, src, NULL);

	zassert_not_null(msg, "mp_message_new shall return non-NULL");
	zassert_ok(mp_bus_post(&fixture->bus, msg), "Posting a message shall succeed");

	struct mp_message *peeked = mp_bus_peek(&fixture->bus);

	zassert_not_null(peeked, "Peek shall return the message");
	zassert_equal(peeked->type, MP_MESSAGE_EOS, "Peeked message type shall be EOS");
	zassert_equal(peeked->src, src, "Peeked message src shall match");
	zassert_is_null(peeked->data, "Peeked message data shall be NULL");

	struct mp_message *popped = mp_bus_pop(&fixture->bus);

	zassert_not_null(popped, "Message shall still be available after peek");
	zassert_equal(peeked, popped, "Peek and pop shall return same message");
	mp_message_destroy(popped);

	zassert_is_null(mp_bus_peek(&fixture->bus),
			"Bus shall be empty after popping the only message");
}

ZTEST_F(mp_bus_api, test_post_multiple_fifo_order)
{
	struct mp_object *src = (struct mp_object *)&fixture->elem;
	struct mp_message *msg1 = mp_message_new(MP_MESSAGE_EOS, src, NULL);
	struct mp_message *msg2 = mp_message_new(MP_MESSAGE_ERROR, NULL, NULL);

	zassert_not_null(msg1);
	zassert_not_null(msg2);

	mp_bus_post(&fixture->bus, msg1);
	mp_bus_post(&fixture->bus, msg2);

	struct mp_message *first = mp_bus_pop(&fixture->bus);
	struct mp_message *second = mp_bus_pop(&fixture->bus);

	zassert_equal(first->type, MP_MESSAGE_EOS, "First out shall be EOS");
	zassert_equal(first->src, src, "First msg src shall match");
	zassert_equal(second->type, MP_MESSAGE_ERROR, "Second out shall be ERROR");
	zassert_is_null(second->src, "Second msg src shall be NULL");

	mp_message_destroy(first);
	mp_message_destroy(second);
}

ZTEST_F(mp_bus_api, test_sanity)
{
	struct mp_message *msg = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);

	zassert_not_null(msg);

	zassert_true(mp_bus_post(NULL, msg) < 0, "Posting to NULL bus shall fail");
	zassert_true(mp_bus_post(&fixture->bus, NULL) < 0, "Posting NULL message shall fail");

	mp_message_destroy(msg);
}

ZTEST_F(mp_bus_api, test_pop_msg_filters_by_type)
{
	struct mp_object *src = (struct mp_object *)&fixture->elem;
	struct mp_message *eos = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);
	struct mp_message *err = mp_message_new(MP_MESSAGE_ERROR, src, NULL);

	zassert_not_null(eos);
	zassert_not_null(err);

	mp_bus_post(&fixture->bus, eos);
	mp_bus_post(&fixture->bus, err);

	struct mp_message *found = mp_bus_pop_msg(&fixture->bus, MP_MESSAGE_ERROR);

	zassert_not_null(found, "pop_msg shall find matching message");
	zassert_equal(found->type, MP_MESSAGE_ERROR, "Returned message shall match filter type");
	zassert_equal(found->src, src, "Returned message src shall match");
	zassert_is_null(found->data, "Returned message data shall be NULL");

	mp_message_destroy(found);
}

ZTEST_F(mp_bus_api, test_flush_clears_all)
{
	struct mp_message *msg1 = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);
	struct mp_message *msg2 = mp_message_new(MP_MESSAGE_ERROR, NULL, NULL);

	zassert_not_null(msg1);
	zassert_not_null(msg2);

	mp_bus_post(&fixture->bus, msg1);
	mp_bus_post(&fixture->bus, msg2);

	mp_bus_flush(&fixture->bus);

	zassert_is_null(mp_bus_peek(&fixture->bus), "Bus shall be empty after flush");
}

ZTEST_F(mp_bus_api, test_sync_listener)
{
	struct mp_object *src = (struct mp_object *)&fixture->elem;

	listener_call_count = 0;
	listener_last_type = MP_MESSAGE_UNKNOWN;
	listener_last_src = NULL;

	mp_bus_add_sync_listener(&fixture->bus, test_listener_cb, MP_MESSAGE_EOS, NULL);

	struct mp_message *msg = mp_message_new(MP_MESSAGE_EOS, src, NULL);

	zassert_not_null(msg);
	mp_bus_post(&fixture->bus, msg);

	zassert_equal(listener_call_count, 1, "Listener shall be called once for matching message");
	zassert_equal(listener_last_type, MP_MESSAGE_EOS,
		      "Listener shall receive correct message type");
	zassert_equal(listener_last_src, src, "Listener shall receive correct message source");
}

ZTEST_F(mp_bus_api, test_sync_listener_filters_type)
{
	listener_call_count = 0;

	mp_bus_add_sync_listener(&fixture->bus, test_listener_cb, MP_MESSAGE_ERROR, NULL);

	struct mp_message *msg = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);

	zassert_not_null(msg);
	mp_bus_post(&fixture->bus, msg);
	zassert_equal(listener_call_count, 0, "Listener shall not be called for non-matching type");
}
