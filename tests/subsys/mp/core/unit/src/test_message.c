/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/core/mp_caps.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_messages.h>
#include <zephyr/mp/core/mp_structure.h>

extern struct k_heap _system_heap;

struct mp_message_api_fixture {
	struct sys_memory_stats mem_before;
};

static void *message_suite_setup(void)
{
	static struct mp_message_api_fixture fixture;

	return &fixture;
}

static void message_before(void *f)
{
	struct mp_message_api_fixture *fix = f;

	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);
}

static void message_after(void *f)
{
	struct mp_message_api_fixture *fix = f;
	struct sys_memory_stats mem_after;

	sys_heap_runtime_stats_get(&_system_heap.heap, &mem_after);
	zassert_equal(fix->mem_before.allocated_bytes, mem_after.allocated_bytes,
		      "Memory leak detected: before=%zu after=%zu", fix->mem_before.allocated_bytes,
		      mem_after.allocated_bytes);
}

ZTEST_SUITE(mp_message_api, NULL, message_suite_setup, message_before, message_after, NULL);

ZTEST(mp_message_api, test_new_messages)
{
	struct mp_element elem;

	memset(&elem, 0, sizeof(elem));
	mp_element_init(&elem, 7);

	struct mp_message *eos = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);

	zassert_not_null(eos, "mp_message_new(EOS) returned NULL");
	zassert_equal(eos->type, MP_MESSAGE_EOS, "type != EOS");
	zassert_is_null(eos->src, "src != NULL");
	zassert_is_null(eos->data, "data != NULL");
	mp_message_destroy(eos);

	struct mp_message *err = mp_message_new(MP_MESSAGE_ERROR, (struct mp_object *)&elem, NULL);

	zassert_not_null(err, "mp_message_new(ERROR) returned NULL");
	zassert_equal(err->type, MP_MESSAGE_ERROR, "type != ERROR");
	zassert_equal(err->src, (struct mp_object *)&elem, "src mismatch");
	mp_message_destroy(err);

	struct mp_structure *data = mp_structure_new(MP_MEDIA_AUDIO_PCM, MP_STRUCTURE_END);

	zassert_not_null(data);

	struct mp_message *with_data = mp_message_new(MP_MESSAGE_EOS, NULL, data);

	zassert_not_null(with_data);
	zassert_equal(with_data->data, data, "data mismatch");
	mp_message_destroy(with_data);

	struct mp_message *m1 = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);
	struct mp_message *m2 = mp_message_new(MP_MESSAGE_EOS, NULL, NULL);

	zassert_not_null(m1);
	zassert_not_null(m2);
	zassert_true(m2->seq_id >= m1->seq_id, "seq_id not monotonically increasing");
	mp_message_destroy(m1);
	mp_message_destroy(m2);
}

ZTEST(mp_message_api, test_sanity)
{
	struct mp_message *msg = mp_message_new(MP_MESSAGE_UNKNOWN, NULL, NULL);

	if (msg != NULL) {
		zassert_equal(msg->type, MP_MESSAGE_UNKNOWN, "type != UNKNOWN");
		mp_message_destroy(msg);
	}
}
