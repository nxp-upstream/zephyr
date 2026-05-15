/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/core/mp_value.h>

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
	/* Boolean true */
	struct mp_value *bt = mp_value_new(MP_TYPE_BOOLEAN, true);

	zassert_not_null(bt, "mp_value_new(BOOLEAN, true) shall return non-NULL");
	zassert_equal(bt->type, MP_TYPE_BOOLEAN, "Type shall be BOOLEAN");
	zassert_true(mp_value_get_boolean(bt), "Boolean value shall be true");
	mp_value_destroy(bt);

	/* Boolean false */
	struct mp_value *bf = mp_value_new(MP_TYPE_BOOLEAN, false);

	zassert_not_null(bf);
	zassert_false(mp_value_get_boolean(bf), "Boolean value shall be false");
	mp_value_destroy(bf);

	/* INT */
	struct mp_value *iv = mp_value_new(MP_TYPE_INT, -42);

	zassert_not_null(iv, "mp_value_new(INT) shall return non-NULL");
	zassert_equal(iv->type, MP_TYPE_INT, "Type shall be INT");
	zassert_equal(mp_value_get_int(iv), -42, "INT value shall be -42");
	mp_value_destroy(iv);

	/* UINT */
	struct mp_value *uv = mp_value_new(MP_TYPE_UINT, 123U);

	zassert_not_null(uv);
	zassert_equal(uv->type, MP_TYPE_UINT, "Type shall be UINT");
	zassert_equal(mp_value_get_uint(uv), 123U, "UINT value shall be 123");
	mp_value_destroy(uv);

	/* STRING */
	struct mp_value *sv = mp_value_new(MP_TYPE_STRING, "hello");

	zassert_not_null(sv);
	zassert_equal(sv->type, MP_TYPE_STRING, "Type shall be STRING");
	zassert_str_equal(mp_value_get_string(sv), "hello", "String value shall match");
	mp_value_destroy(sv);

	/* INT_RANGE */
	struct mp_value *rv = mp_value_new(MP_TYPE_INT_RANGE, 8000, 48000, 8000);

	zassert_not_null(rv);
	zassert_equal(rv->type, MP_TYPE_INT_RANGE, "Type shall be INT_RANGE");
	zassert_equal(mp_value_get_int_range_min(rv), 8000, "Range min shall be 8000");
	zassert_equal(mp_value_get_int_range_max(rv), 48000, "Range max shall be 48000");
	zassert_equal(mp_value_get_int_range_step(rv), 8000, "Range step shall be 8000");
	mp_value_destroy(rv);

	/* INT_FRACTION */
	struct mp_value *fv = mp_value_new(MP_TYPE_INT_FRACTION, 30, 1);

	zassert_not_null(fv);
	zassert_equal(fv->type, MP_TYPE_INT_FRACTION, "Type shall be INT_FRACTION");
	zassert_equal(mp_value_get_fraction_numerator(fv), 30, "Numerator shall be 30");
	zassert_equal(mp_value_get_fraction_denominator(fv), 1, "Denominator shall be 1");
	mp_value_destroy(fv);

	/* Empty INT (default zero) */
	struct mp_value *ez = mp_value_new(MP_TYPE_INT, 0);

	zassert_not_null(ez);
	zassert_equal(mp_value_get_int(ez), 0, "Empty INT value shall be 0");
	mp_value_destroy(ez);

	/* Empty list */
	struct mp_value *el = mp_value_new(MP_TYPE_LIST, NULL);

	zassert_not_null(el, "mp_value_new(LIST, NULL) shall return non-NULL");
	zassert_equal(el->type, MP_TYPE_LIST, "Type shall be LIST");
	zassert_equal(mp_value_list_get_size(el), 0, "Empty list size shall be 0");
	zassert_true(mp_value_list_is_empty(el), "Empty list shall be empty");
	mp_value_destroy(el);
}

ZTEST(mp_value_api, test_list)
{
	struct mp_value *list = mp_value_new(MP_TYPE_LIST, NULL);
	struct mp_value *item1 = mp_value_new(MP_TYPE_INT, 10);
	struct mp_value *item2 = mp_value_new(MP_TYPE_INT, 20);

	zassert_ok(mp_value_list_append(list, item1), "Appending item1 shall succeed");
	zassert_ok(mp_value_list_append(list, item2), "Appending item2 shall succeed");

	zassert_equal(mp_value_list_get_size(list), 2, "List size shall be 2");
	zassert_false(mp_value_list_is_empty(list), "List shall not be empty");

	struct mp_value *got = mp_value_list_get(list, 0);

	zassert_not_null(got, "Get at index 0 shall return non-NULL");
	zassert_equal(mp_value_get_int(got), 10, "First item shall be 10");

	got = mp_value_list_get(list, 1);
	zassert_equal(mp_value_get_int(got), 20, "Second item shall be 20");

	zassert_is_null(mp_value_list_get(list, 5),
			"Get with out-of-range index shall return NULL");

	zassert_equal(mp_value_list_append(NULL, item1), -EINVAL, "NULL list shall return -EINVAL");
	zassert_equal(mp_value_list_append(list, NULL), -EINVAL, "NULL value shall return -EINVAL");

	mp_value_destroy(list);
}

ZTEST(mp_value_api, test_compare)
{
	struct mp_value *a10 = mp_value_new(MP_TYPE_INT, 10);
	struct mp_value *b10 = mp_value_new(MP_TYPE_INT, 10);
	struct mp_value *b20 = mp_value_new(MP_TYPE_INT, 20);
	struct mp_value *b100 = mp_value_new(MP_TYPE_INT, 100);
	struct mp_value *a100 = mp_value_new(MP_TYPE_INT, 100);

	zassert_equal(mp_value_compare(a10, b10), MP_VALUE_EQUAL, "Same int values shall be EQUAL");
	zassert_equal(mp_value_compare(a10, b20), MP_VALUE_LESS_THAN,
		      "10 < 20 shall return LESS_THAN");
	zassert_equal(mp_value_compare(a100, b10), MP_VALUE_GREATER_THAN,
		      "100 > 10 shall return GREATER_THAN");

	mp_value_destroy(a10);
	mp_value_destroy(b10);
	mp_value_destroy(b20);
	mp_value_destroy(b100);
	mp_value_destroy(a100);
}

ZTEST(mp_value_api, test_intersect)
{
	struct mp_value *a = mp_value_new(MP_TYPE_INT, 48000);
	struct mp_value *b = mp_value_new(MP_TYPE_INT, 48000);
	struct mp_value *result = mp_value_intersect(a, b);

	zassert_not_null(result, "Intersecting equal values shall succeed");
	zassert_equal(mp_value_get_int(result), 48000, "Result shall be 48000");
	mp_value_destroy(result);
	mp_value_destroy(a);
	mp_value_destroy(b);

	struct mp_value *range = mp_value_new(MP_TYPE_INT_RANGE, 8000, 48000, 8000);
	struct mp_value *val = mp_value_new(MP_TYPE_INT, 16000);

	result = mp_value_intersect(range, val);
	zassert_not_null(result, "Value within range shall intersect");
	mp_value_destroy(result);
	mp_value_destroy(range);
	mp_value_destroy(val);
}

ZTEST(mp_value_api, test_duplicate_and_is_primitive)
{
	struct mp_value *original = mp_value_new(MP_TYPE_INT, 999);
	struct mp_value *copy = mp_value_duplicate(original);

	zassert_not_null(copy, "Duplicate shall return non-NULL");
	zassert_true(copy != original, "Duplicate shall be different pointer");
	zassert_equal(mp_value_get_int(copy), 999, "Duplicated value shall match");
	mp_value_destroy(original);
	mp_value_destroy(copy);

	struct mp_value *rorig = mp_value_new(MP_TYPE_INT_RANGE, 1, 100, 1);
	struct mp_value *rcopy = mp_value_duplicate(rorig);

	zassert_not_null(rcopy);
	zassert_equal(mp_value_get_int_range_min(rcopy), 1, "Min shall match");
	zassert_equal(mp_value_get_int_range_max(rcopy), 100, "Max shall match");
	zassert_equal(mp_value_get_int_range_step(rcopy), 1, "Step shall match");
	mp_value_destroy(rorig);
	mp_value_destroy(rcopy);

	struct mp_value *iv = mp_value_new(MP_TYPE_INT, 1);

	zassert_true(mp_value_is_primitive(iv), "INT shall be primitive");
	mp_value_destroy(iv);

	struct mp_value *bv = mp_value_new(MP_TYPE_BOOLEAN, true);

	zassert_true(mp_value_is_primitive(bv), "BOOLEAN shall be primitive");
	mp_value_destroy(bv);

	struct mp_value *lv = mp_value_new(MP_TYPE_LIST, NULL);

	zassert_false(mp_value_is_primitive(lv), "LIST shall not be primitive");
	mp_value_destroy(lv);

	struct mp_value *rv = mp_value_new(MP_TYPE_INT_RANGE, 0, 10, 1);

	zassert_false(mp_value_is_primitive(rv), "INT_RANGE shall not be primitive");
	mp_value_destroy(rv);

	struct mp_value *ci_a = mp_value_new(MP_TYPE_INT, 10);
	struct mp_value *ci_b = mp_value_new(MP_TYPE_INT, 10);

	zassert_true(mp_value_can_intersect(ci_a, ci_b),
		     "Same-type values shall be able to intersect");
	mp_value_destroy(ci_a);
	mp_value_destroy(ci_b);

	struct mp_value *ci_range = mp_value_new(MP_TYPE_INT_RANGE, 0, 100, 1);
	struct mp_value *ci_val = mp_value_new(MP_TYPE_INT, 50);

	zassert_true(mp_value_can_intersect(ci_range, ci_val),
		     "Range and value shall be able to intersect");
	mp_value_destroy(ci_range);
	mp_value_destroy(ci_val);
}

ZTEST(mp_value_api, test_set_updates_value)
{
	struct mp_value *val = mp_value_new(MP_TYPE_INT, 10);

	mp_value_set(val, MP_TYPE_INT, 99);

	zassert_equal(mp_value_get_int(val), 99, "Value shall be updated to 99");

	mp_value_destroy(val);
}

ZTEST(mp_value_api, test_sanity)
{
	struct mp_value *int_val = mp_value_new(MP_TYPE_INT, 42);
	struct mp_value *str_val = mp_value_new(MP_TYPE_STRING, "hello");

	zassert_equal(mp_value_compare(int_val, str_val), MP_VALUE_COMPARE_FAILED,
		      "Comparing different types shall return COMPARE_FAILED");
	mp_value_destroy(int_val);
	mp_value_destroy(str_val);

	struct mp_value *a = mp_value_new(MP_TYPE_INT, 100);
	struct mp_value *b = mp_value_new(MP_TYPE_INT, 200);
	struct mp_value *result = mp_value_intersect(a, b);

	zassert_is_null(result, "Disjoint values shall return NULL");
	mp_value_destroy(a);
	mp_value_destroy(b);
}
