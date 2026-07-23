/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/mp_value.h>

extern struct k_heap _system_heap;

struct mp_value_api_fixture {
	struct sys_memory_stats mem_before;
};

static void *value_suite_setup(void)
{
	static struct mp_value_api_fixture fixture;

	return &fixture;
}

static void value_before(void *f)
{
	struct mp_value_api_fixture *fix = f;

	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);
}

static void value_after(void *f)
{
	struct mp_value_api_fixture *fix = f;
	struct sys_memory_stats mem_after;

	sys_heap_runtime_stats_get(&_system_heap.heap, &mem_after);
	zassert_equal(fix->mem_before.allocated_bytes, mem_after.allocated_bytes,
		      "Memory leak detected: before=%zu after=%zu", fix->mem_before.allocated_bytes,
		      mem_after.allocated_bytes);
}

ZTEST_SUITE(mp_value_api, NULL, value_suite_setup, value_before, value_after, NULL);

ZTEST(mp_value_api, test_new_values)
{
	mp_value_t bt = mp_value_new_boolean(true);

	zassert_not_null(bt, "mp_value_new(BOOLEAN, true) returned NULL");
	zassert_equal(mp_value_get_type(bt), MP_TYPE_BOOLEAN, "type != BOOLEAN");
	zassert_true(mp_value_get_boolean(bt), "value != true");
	mp_value_unref(bt);

	mp_value_t bf = mp_value_new_boolean(false);

	zassert_not_null(bf);
	zassert_false(mp_value_get_boolean(bf), "value != false");
	mp_value_unref(bf);

	mp_value_t iv = mp_value_new_int(-42LL);

	zassert_not_null(iv, "mp_value_new(INT) returned NULL");
	zassert_equal(mp_value_get_type(iv), MP_TYPE_INT, "type != INT");
	zassert_equal(mp_value_get_int(iv), -42, "value != -42");
	mp_value_unref(iv);

	mp_value_t uv = mp_value_new_int(123U);

	zassert_not_null(uv);
	zassert_equal(mp_value_get_type(uv), MP_TYPE_INT, "type != INT");
	zassert_equal(mp_value_get_int(uv), 123U, "value != 123");
	mp_value_unref(uv);

	mp_value_t sv = mp_value_new_string("hello");

	zassert_not_null(sv);
	zassert_equal(mp_value_get_type(sv), MP_TYPE_STRING, "type != STRING");
	zassert_str_equal(mp_value_get_string(sv), "hello", "string mismatch");
	mp_value_unref(sv);

	mp_value_t rv = mp_value_new_range(8000, 48000, 8000);

	zassert_not_null(rv);
	zassert_equal(mp_value_get_type(rv), MP_TYPE_RANGE, "type != INT_RANGE");
	zassert_equal(mp_value_get_range_min(rv), 8000, "min != 8000");
	zassert_equal(mp_value_get_range_max(rv), 48000, "max != 48000");
	zassert_equal(mp_value_get_range_step(rv), 8000, "step != 8000");
	mp_value_unref(rv);

	mp_value_t ez = mp_value_new_int(0);

	zassert_not_null(ez);
	zassert_equal(mp_value_get_int(ez), 0, "value != 0");
	mp_value_unref(ez);

	mp_value_t el = mp_value_new_list(0, NULL);

	zassert_not_null(el, "mp_value_new(LIST, NULL) returned NULL");
	zassert_equal(mp_value_get_type(el), MP_TYPE_LIST, "type != LIST");
	zassert_equal(mp_value_list_get_size(el), 0, "list size != 0");
	zassert_true(mp_value_list_is_empty(el), "list not empty");
	mp_value_unref(el);
}

ZTEST(mp_value_api, test_list)
{
	mp_value_t list = mp_value_new_list(2, NULL);
	mp_value_t item1 = mp_value_new_int(10);
	mp_value_t item2 = mp_value_new_int(20);

	zassert_not_null(list, "list allocation failed");
	mp_value_list_set(list, 0, item1); /* should not trigger assertion error */
	mp_value_list_set(list, 1, item2); /* should not trigger assertion error */

	zassert_equal(mp_value_list_get_size(list), 2, "list size != 2");
	zassert_false(mp_value_list_is_empty(list), "list is empty");

	mp_value_t got = mp_value_list_get(list, 0);

	zassert_not_null(got, "get(0) returned NULL");
	zassert_equal(mp_value_get_int(got), 10, "item[0] != 10");

	got = mp_value_list_get(list, 1);
	zassert_equal(mp_value_get_int(got), 20, "item[1] != 20");

	zassert_is_null(mp_value_list_get(list, 5), "get(out of range) != NULL");

	mp_value_unref(list);
}

ZTEST(mp_value_api, test_compare)
{
	mp_value_t a10 = mp_value_new_int(10);
	mp_value_t b10 = mp_value_new_int(10);
	mp_value_t b20 = mp_value_new_int(20);
	mp_value_t b100 = mp_value_new_int(100);
	mp_value_t a100 = mp_value_new_int(100);

	zassert_equal(mp_value_compare(a10, b10), MP_VALUE_EQUAL, "10 == 10 failed");
	zassert_equal(mp_value_compare(a10, b20), MP_VALUE_LESS_THAN, "10 < 20 failed");
	zassert_equal(mp_value_compare(a100, b10), MP_VALUE_GREATER_THAN, "100 > 10 failed");

	mp_value_unref(a10);
	mp_value_unref(b10);
	mp_value_unref(b20);
	mp_value_unref(b100);
	mp_value_unref(a100);
}

ZTEST(mp_value_api, test_intersect)
{
	mp_value_t a = mp_value_new_int(48000);
	mp_value_t b = mp_value_new_int(48000);
	mp_value_t result = mp_value_intersect(a, b);

	zassert_not_null(result, "intersect(equal values) returned NULL");
	zassert_equal(mp_value_get_int(result), 48000, "result != 48000");
	mp_value_unref(result);
	mp_value_unref(a);
	mp_value_unref(b);

	mp_value_t range = mp_value_new_range(8000, 48000, 8000);
	mp_value_t val = mp_value_new_int(16000);

	result = mp_value_intersect(range, val);
	zassert_not_null(result, "intersect(value in range) returned NULL");
	mp_value_unref(result);
	mp_value_unref(range);
	mp_value_unref(val);
}

ZTEST(mp_value_api, test_is_primitive)
{
	mp_value_t iv = mp_value_new_int(1);

	zassert_true(mp_value_is_primitive(iv), "INT not primitive");
	mp_value_unref(iv);

	mp_value_t bv = mp_value_new_boolean(true);

	zassert_true(mp_value_is_primitive(bv), "BOOLEAN not primitive");
	mp_value_unref(bv);

	mp_value_t lv = mp_value_new_list(0, NULL);

	zassert_false(mp_value_is_primitive(lv), "LIST is primitive");
	mp_value_unref(lv);

	mp_value_t rv = mp_value_new_range(0, 10, 1);

	zassert_false(mp_value_is_primitive(rv), "INT_RANGE is primitive");
	mp_value_unref(rv);

	mp_value_t ci_a = mp_value_new_int(10);
	mp_value_t ci_b = mp_value_new_int(10);

	zassert_true(mp_value_can_intersect(ci_a, ci_b), "same-type values cannot intersect");
	mp_value_unref(ci_a);
	mp_value_unref(ci_b);

	mp_value_t ci_range = mp_value_new_range(0, 100, 1);
	mp_value_t ci_val = mp_value_new_int(50);

	zassert_true(mp_value_can_intersect(ci_range, ci_val), "range and value cannot intersect");
	mp_value_unref(ci_range);
	mp_value_unref(ci_val);
}

ZTEST(mp_value_api, test_sanity)
{
	mp_value_t int_val = mp_value_new_int(42);
	mp_value_t str_val = mp_value_new_string("hello");

	zassert_equal(mp_value_compare(int_val, str_val), MP_VALUE_COMPARE_FAILED,
		      "different types compare != COMPARE_FAILED");
	mp_value_unref(int_val);
	mp_value_unref(str_val);

	mp_value_t a = mp_value_new_int(100);
	mp_value_t b = mp_value_new_int(200);
	mp_value_t result = mp_value_intersect(a, b);

	zassert_is_null(result, "disjoint values intersect != NULL");
	mp_value_unref(a);
	mp_value_unref(b);
}
