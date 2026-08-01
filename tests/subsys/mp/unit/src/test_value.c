/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/mp_value.h>

ZTEST_SUITE(mp_value_api, NULL, NULL, NULL, NULL, NULL);

ZTEST(mp_value_api, test_new_values)
{
	struct mp_value bt;

	mp_value_set(&bt, MP_TYPE_BOOLEAN, true);

	zassert_equal(bt.type, MP_TYPE_BOOLEAN, "type != BOOLEAN");
	zassert_true(mp_value_get_boolean(&bt), "value != true");

	struct mp_value bf;

	mp_value_set(&bf, MP_TYPE_BOOLEAN, false);

	zassert_false(mp_value_get_boolean(&bf), "value != false");

	struct mp_value iv;

	mp_value_set(&iv, MP_TYPE_INT, -42);

	zassert_equal(iv.type, MP_TYPE_INT, "type != INT");
	zassert_equal(mp_value_get_int(&iv), -42, "value != -42");

	struct mp_value uv;

	mp_value_set(&uv, MP_TYPE_UINT, 123U);

	zassert_equal(uv.type, MP_TYPE_UINT, "type != UINT");
	zassert_equal(mp_value_get_uint(&uv), 123U, "value != 123");

	struct mp_value rv;

	mp_value_set(&rv, MP_TYPE_INT_RANGE, 8000, 48000, 8000);

	zassert_equal(rv.type, MP_TYPE_INT_RANGE, "type != INT_RANGE");
	zassert_equal(mp_value_get_int_range_min(&rv), 8000, "min != 8000");
	zassert_equal(mp_value_get_int_range_max(&rv), 48000, "max != 48000");
	zassert_equal(mp_value_get_int_range_step(&rv), 8000, "step != 8000");

	struct mp_value ez;

	mp_value_set(&ez, MP_TYPE_INT, 0);

	zassert_equal(mp_value_get_int(&ez), 0, "value != 0");
}

ZTEST(mp_value_api, test_compare)
{
	struct mp_value a10;

	mp_value_set(&a10, MP_TYPE_INT, 10);
	struct mp_value b10;

	mp_value_set(&b10, MP_TYPE_INT, 10);
	struct mp_value b20;

	mp_value_set(&b20, MP_TYPE_INT, 20);
	struct mp_value b100;

	mp_value_set(&b100, MP_TYPE_INT, 100);
	struct mp_value a100;

	mp_value_set(&a100, MP_TYPE_INT, 100);

	zassert_equal(mp_value_compare(&a10, &b10), MP_VALUE_EQUAL, "10 == 10 failed");
	zassert_equal(mp_value_compare(&a10, &b20), MP_VALUE_LESS_THAN, "10 < 20 failed");
	zassert_equal(mp_value_compare(&a100, &b10), MP_VALUE_GREATER_THAN, "100 > 10 failed");
}

ZTEST(mp_value_api, test_intersect)
{
	struct mp_value a;

	mp_value_set(&a, MP_TYPE_INT, 48000);
	struct mp_value b;

	mp_value_set(&b, MP_TYPE_INT, 48000);
	struct mp_value result;

	zassert_ok(mp_value_intersect(&a, &b, &result), "intersect(equal values) failed");
	zassert_equal(mp_value_get_int(&result), 48000, "result != 48000");

	struct mp_value range;

	mp_value_set(&range, MP_TYPE_INT_RANGE, 8000, 48000, 8000);
	struct mp_value val;

	mp_value_set(&val, MP_TYPE_INT, 16000);

	zassert_ok(mp_value_intersect(&range, &val, &result), "intersect(value in &range) failed");
	zassert_equal(mp_value_get_int(&result), 16000, "result != 16000");
}

ZTEST(mp_value_api, test_copy_and_is_primitive)
{
	struct mp_value original;

	struct mp_value copy;

	/* A value owns nothing, so copying one is a plain assignment */
	mp_value_set(&original, MP_TYPE_INT, 999);
	copy = original;
	zassert_equal(mp_value_get_int(&copy), 999, "copied value != 999");

	struct mp_value rorig;
	struct mp_value rcopy;

	mp_value_set(&rorig, MP_TYPE_INT_RANGE, 1, 100, 1);
	rcopy = rorig;
	zassert_equal(mp_value_get_int_range_min(&rcopy), 1, "min != 1");
	zassert_equal(mp_value_get_int_range_max(&rcopy), 100, "max != 100");
	zassert_equal(mp_value_get_int_range_step(&rcopy), 1, "step != 1");

	struct mp_value iv;

	mp_value_set(&iv, MP_TYPE_INT, 1);

	zassert_true(mp_value_is_primitive(&iv), "INT not primitive");

	struct mp_value bv;

	mp_value_set(&bv, MP_TYPE_BOOLEAN, true);

	zassert_true(mp_value_is_primitive(&bv), "BOOLEAN not primitive");

	struct mp_value rv;

	mp_value_set(&rv, MP_TYPE_INT_RANGE, 0, 10, 1);

	zassert_false(mp_value_is_primitive(&rv), "INT_RANGE is primitive");

	struct mp_value ci_a;

	mp_value_set(&ci_a, MP_TYPE_INT, 10);
	struct mp_value ci_b;

	mp_value_set(&ci_b, MP_TYPE_INT, 10);

	zassert_true(mp_value_can_intersect(&ci_a, &ci_b), "same-type values cannot intersect");

	struct mp_value ci_range;

	mp_value_set(&ci_range, MP_TYPE_INT_RANGE, 0, 100, 1);
	struct mp_value ci_val;

	mp_value_set(&ci_val, MP_TYPE_INT, 50);

	zassert_true(mp_value_can_intersect(&ci_range, &ci_val),
		     "&range and value cannot intersect");
}

ZTEST(mp_value_api, test_set_updates_value)
{
	struct mp_value val;

	mp_value_set(&val, MP_TYPE_INT, 10);

	mp_value_set(&val, MP_TYPE_INT, 99);
	zassert_equal(mp_value_get_int(&val), 99, "value != 99 after set");
}

ZTEST(mp_value_api, test_sanity)
{
	struct mp_value int_val;

	mp_value_set(&int_val, MP_TYPE_INT, 42);
	struct mp_value uint_val;

	mp_value_set(&uint_val, MP_TYPE_UINT, 42U);

	zassert_equal(mp_value_compare(&int_val, &uint_val), MP_VALUE_COMPARE_FAILED,
		      "different types compare != COMPARE_FAILED");

	struct mp_value a;

	mp_value_set(&a, MP_TYPE_INT, 100);
	struct mp_value b;

	mp_value_set(&b, MP_TYPE_INT, 200);
	struct mp_value result;

	zassert_equal(mp_value_intersect(&a, &b, &result), -ENOENT,
		      "disjoint values intersect != -ENOENT");
}
