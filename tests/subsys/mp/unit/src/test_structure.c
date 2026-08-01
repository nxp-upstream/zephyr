/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include "mp_test_helpers.h"

ZTEST_SUITE(mp_structure_api, NULL, NULL, NULL, NULL, NULL);

/* Field IDs used by intersection tests */
enum test_field {
	TEST_BOOL = 0,
	TEST_INT,
	TEST_UINT,
	TEST_RANGE_INT,
	TEST_RANGE_UINT,
};

ZTEST(mp_structure_api, test_new)
{
	struct mp_structure s;

	zassert_ok(mp_structure_init_fields(&s, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_INT, 48000, MP_CAPS_BITWIDTH, MP_TYPE_INT, 16,
					    MP_CAPS_END),
		   "init &s failed");

	zassert_equal(s.media_type_id, MP_MEDIA_AUDIO_PCM, "media_type_id mismatch");

	zassert_ok(mp_structure_remove_field(&s, MP_CAPS_SAMPLE_RATE), "remove_field failed");
	zassert_is_null(mp_structure_get_value(&s, MP_CAPS_SAMPLE_RATE),
			"removed field still found");
	zassert_not_null(mp_structure_get_value(&s, MP_CAPS_BITWIDTH), "non-removed field missing");

	mp_structure_clear(&s);
	zassert_is_null(mp_structure_get_value(&s, MP_CAPS_BITWIDTH), "field found after clear");

	struct mp_structure si;

	zassert_ok(mp_structure_init(&si, MP_MEDIA_AUDIO_PCM));

	struct mp_value appended;

	mp_value_set(&appended, MP_TYPE_INT, 44100);

	zassert_ok(mp_structure_append_value(&si, MP_CAPS_SAMPLE_RATE, &appended), "append failed");

	const struct mp_value *retrieved = mp_structure_get_value(&si, MP_CAPS_SAMPLE_RATE);

	zassert_not_null(retrieved, "appended field not found");
	zassert_equal(mp_value_get_int(retrieved), 44100, "retrieved value != 44100");

	struct mp_value dup_val;

	mp_value_set(&dup_val, MP_TYPE_INT, 0);

	zassert_equal(mp_structure_append_value(&si, MP_CAPS_SAMPLE_RATE, &dup_val), -EEXIST,
		      "duplicate field != -EEXIST");

	zassert_equal(mp_structure_init(NULL, MP_MEDIA_AUDIO_PCM), -EINVAL,
		      "init(NULL) != -EINVAL");
	zassert_equal(mp_structure_append_value(NULL, MP_CAPS_SAMPLE_RATE, &appended), -EINVAL,
		      "append(NULL struct) != -EINVAL");
	zassert_equal(mp_structure_append_value(&si, MP_CAPS_BITWIDTH, NULL), -EINVAL,
		      "append(NULL value) != -EINVAL");
}

ZTEST(mp_structure_api, test_is_fixed_fixate_duplicate)
{
	struct mp_structure fixed_s;

	zassert_ok(mp_structure_init_fields(&fixed_s, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_INT, 48000, MP_CAPS_BITWIDTH, MP_TYPE_INT, 16,
					    MP_CAPS_END),
		   "init &fixed_s failed");

	zassert_true(mp_structure_is_fixed(&fixed_s), "structure not fixed");

	struct mp_structure dup;

	zassert_ok(mp_structure_duplicate(&fixed_s, &dup), "duplicate failed");

	zassert_equal(dup.media_type_id, fixed_s.media_type_id, "media_type_id mismatch");
	zassert_equal(mp_value_get_int(mp_structure_get_value(&dup, MP_CAPS_SAMPLE_RATE)), 48000,
		      "duplicated value != 48000");

	struct mp_structure range_s;

	zassert_ok(mp_structure_init_fields(&range_s, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_INT_RANGE, 8000, 48000, 8000, MP_CAPS_END),
		   "init &range_s failed");

	zassert_false(mp_structure_is_fixed(&range_s), "range structure is fixed");

	struct mp_structure fixated;

	zassert_ok(mp_structure_fixate(&range_s, &fixated), "fixate failed");

	zassert_true(mp_structure_is_fixed(&fixated), "&fixated structure not fixed");
}

/* Intersect two structures where all fields are primitive (fixed) values. */
ZTEST(mp_structure_api, test_intersect_primitive)
{
	struct mp_structure s1;

	zassert_ok(mp_structure_init_fields(&s1, MP_MEDIA_AUDIO_PCM, TEST_BOOL, MP_TYPE_BOOLEAN,
					    true, TEST_INT, MP_TYPE_INT, -123, TEST_UINT,
					    MP_TYPE_UINT, 123, MP_CAPS_END),
		   "init &s1 failed");
	struct mp_structure s2;

	zassert_ok(mp_structure_init_fields(&s2, MP_MEDIA_AUDIO_PCM, TEST_BOOL, MP_TYPE_BOOLEAN,
					    true, TEST_INT, MP_TYPE_INT, -123, TEST_UINT,
					    MP_TYPE_UINT, 123, MP_CAPS_END),
		   "init &s2 failed");

	zassert_true(mp_structure_can_intersect(&s1, &s2), "identical structures cannot intersect");

	struct mp_structure result;

	zassert_ok(mp_structure_intersect(&s1, &s2, &result), "intersect failed");

	const struct mp_value *v = mp_structure_get_value(&result, TEST_BOOL);

	validate_boolean_value(v, true);

	v = mp_structure_get_value(&result, TEST_INT);
	validate_int_value(v, -123);

	v = mp_structure_get_value(&result, TEST_UINT);
	validate_uint_value(v, 123);
}

/* Intersect a structure with an INT_RANGE field against a structure with a fixed INT value. */
ZTEST(mp_structure_api, test_intersect_int_range)
{
	struct {
		int value;
		int expected;
	} test_cases[] = {
		{INT_MIN, INT_MIN},
		{INT_MAX, INT_MAX},
		{(INT_MIN + INT_MAX) / 2, (INT_MIN + INT_MAX) / 2},
	};

	struct mp_structure s_range;

	zassert_ok(mp_structure_init_fields(&s_range, MP_MEDIA_AUDIO_PCM, TEST_RANGE_INT,
					    MP_TYPE_INT_RANGE, INT_MIN, INT_MAX, 1, MP_CAPS_END),
		   "init &s_range failed");

	for (int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct mp_structure s_val;

		zassert_ok(mp_structure_init_fields(&s_val, MP_MEDIA_AUDIO_PCM, TEST_RANGE_INT,
						    MP_TYPE_INT, test_cases[i].value, MP_CAPS_END),
			   "init &s_val failed");

		struct mp_structure result;

		zassert_ok(mp_structure_intersect(&s_range, &s_val, &result), "intersect failed");

		const struct mp_value *v = mp_structure_get_value(&result, TEST_RANGE_INT);

		validate_int_value(v, test_cases[i].expected);
	}
}

/* Intersect a structure with a UINT_RANGE field against a structure with a fixed UINT value. */
ZTEST(mp_structure_api, test_intersect_uint_range)
{
	struct {
		unsigned int expected;
		const char *description;
	} test_cases[] = {
		{0, "Zero value"},
		{UINT32_MAX, "Maximum value"},
		{UINT32_MAX / 2, "Mid-range value"},
	};

	struct mp_structure s_range;

	zassert_ok(mp_structure_init_fields(&s_range, MP_MEDIA_AUDIO_PCM, TEST_RANGE_UINT,
					    MP_TYPE_UINT_RANGE, 0, UINT32_MAX, 1, MP_CAPS_END),
		   "init &s_range failed");

	for (int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct mp_structure s_val;

		zassert_ok(mp_structure_init_fields(&s_val, MP_MEDIA_AUDIO_PCM, TEST_RANGE_UINT,
						    MP_TYPE_UINT, test_cases[i].expected,
						    MP_CAPS_END),
			   "init &s_val failed");

		struct mp_structure result;

		zassert_ok(mp_structure_intersect(&s_range, &s_val, &result), "intersect failed");

		const struct mp_value *v = mp_structure_get_value(&result, TEST_RANGE_UINT);

		validate_uint_value(v, test_cases[i].expected);
	}
}

/* Intersect structures whose field sets only partly overlap. */
ZTEST(mp_structure_api, test_intersect_asymmetric_fields)
{
	struct mp_structure s1;

	zassert_ok(mp_structure_init_fields(&s1, MP_MEDIA_AUDIO_PCM, TEST_INT, MP_TYPE_INT, -42,
					    TEST_UINT, MP_TYPE_UINT, 100U, MP_CAPS_END),
		   "init &s1 failed");
	struct mp_structure s2;

	zassert_ok(mp_structure_init_fields(&s2, MP_MEDIA_AUDIO_PCM, TEST_UINT, MP_TYPE_UINT, 100U,
					    TEST_BOOL, MP_TYPE_BOOLEAN, true, MP_CAPS_END),
		   "init &s2 failed");

	zassert_true(mp_structure_can_intersect(&s1, &s2),
		     "asymmetric structures cannot intersect");

	struct mp_structure result;

	zassert_ok(mp_structure_intersect(&s1, &s2, &result), "intersect failed");

	zassert_equal(result.num_fields, 3, "&result field count != 3");
	const struct mp_value *v = mp_structure_get_value(&result, TEST_INT);

	validate_int_value(v, -42);

	v = mp_structure_get_value(&result, TEST_UINT);
	validate_uint_value(v, 100U);

	v = mp_structure_get_value(&result, TEST_BOOL);
	validate_boolean_value(v, true);
}

/* A structure that constrains nothing intersects with anything. */
ZTEST(mp_structure_api, test_any_structure)
{
	struct mp_structure any;
	struct mp_structure result;

	zassert_ok(mp_structure_init_any(&any));
	zassert_true(mp_structure_is_any(&any), "init_any did not mark the structure");
	zassert_false(mp_structure_is_fixed(&any), "an ANY structure must not be fixed");

	/* Nothing to choose, so there is nothing to fixate */
	zassert_equal(mp_structure_fixate(&any, &result), -ENOENT, "fixate(ANY) != -ENOENT");

	struct mp_structure concrete;

	zassert_ok(mp_structure_init_fields(&concrete, MP_MEDIA_AUDIO_PCM, TEST_UINT, MP_TYPE_UINT,
					    100U, TEST_RANGE_UINT, MP_TYPE_UINT_RANGE, 0, 50, 1,
					    MP_CAPS_END),
		   "init &concrete failed");

	/* ANY on either side yields the other side unchanged */
	zassert_ok(mp_structure_intersect(&any, &concrete, &result), "ANY x C failed");
	zassert_equal(result.num_fields, 2, "ANY x C lost a field");
	validate_uint_value(mp_structure_get_value(&result, TEST_UINT), 100U);
	validate_uint_range_value(mp_structure_get_value(&result, TEST_RANGE_UINT), 0, 50, 1);

	zassert_ok(mp_structure_intersect(&concrete, &any, &result), "C x ANY failed");
	zassert_equal(result.num_fields, 2, "C x ANY lost a field");

	/* An empty structure is not an ANY structure: it intersects with nothing */
	struct mp_structure empty;

	zassert_ok(mp_structure_init(&empty, MP_MEDIA_AUDIO_PCM));
	zassert_false(mp_structure_is_any(&empty), "an empty structure must not be ANY");
	zassert_equal(mp_structure_intersect(&empty, &concrete, &result), -ENOENT,
		      "empty x C != -ENOENT");

	/*
	 * An empty structure constrains nothing just as an ANY one does, so both
	 * report the same: nothing has settled, and there is nothing to fixate.
	 */
	zassert_false(mp_structure_is_fixed(&empty), "an empty structure must not be fixed");
	zassert_equal(mp_structure_fixate(&empty, &result), -ENOENT, "fixate(empty) != -ENOENT");
}

ZTEST(mp_structure_api, test_cannot_intersect)
{
	struct mp_structure result;
	struct mp_structure s_sample_int;

	zassert_ok(mp_structure_init_fields(&s_sample_int, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_INT, 48000, MP_CAPS_END),
		   "init &s_sample_int failed");
	struct mp_structure s_bw;

	zassert_ok(mp_structure_init_fields(&s_bw, MP_MEDIA_AUDIO_PCM, MP_CAPS_BITWIDTH,
					    MP_TYPE_INT, 16, MP_CAPS_END),
		   "init &s_bw failed");
	struct mp_structure s_low;

	zassert_ok(mp_structure_init_fields(&s_low, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_INT_RANGE, 8000, 16000, 8000, MP_CAPS_END),
		   "init &s_low failed");

	zassert_false(mp_structure_can_intersect(&s_sample_int, NULL),
		      "can_intersect(&s, NULL) should return false");
	zassert_false(mp_structure_can_intersect(NULL, NULL),
		      "can_intersect(NULL, NULL) should return false");
	zassert_not_equal(mp_structure_intersect(&s_sample_int, NULL, &result), 0,
			  "intersect(&s, NULL) should fail");
	zassert_not_equal(mp_structure_intersect(NULL, NULL, &result), 0,
			  "intersect(NULL, NULL) should fail");

	zassert_false(mp_structure_can_intersect(&s_sample_int, &s_bw),
		      "structures with no common field should not intersect");
	zassert_not_equal(mp_structure_intersect(&s_sample_int, &s_bw, &result), 0,
			  "intersect with no common field should fail");

	zassert_false(mp_structure_can_intersect(&s_low, &s_sample_int),
		      "out-of-range value should not intersect");
	zassert_not_equal(mp_structure_intersect(&s_low, &s_sample_int, &result), 0,
			  "intersect with incompatible field value should fail");
}

ZTEST(mp_structure_api, test_sanity)
{
	struct mp_structure s;

	zassert_ok(mp_structure_init_fields(&s, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_INT, 48000, MP_CAPS_END),
		   "init &s failed");

	zassert_is_null(mp_structure_get_value(&s, MP_CAPS_IMAGE_WIDTH),
			"non-existent field != NULL");
	zassert_true(mp_structure_remove_field(&s, MP_CAPS_IMAGE_WIDTH) < 0,
		     "remove non-existent field did not fail");
}

/* A structure holds CONFIG_MP_STRUCTURE_MAX_FIELDS fields; the next one is refused. */
ZTEST(mp_structure_api, test_append_out_of_slots)
{
	struct mp_structure s;
	struct mp_value v;

	/* Filling every slot needs one more field identifier than there are slots */
	if (CONFIG_MP_STRUCTURE_MAX_FIELDS >= MP_CAPS_END) {
		ztest_test_skip();
	}

	zassert_ok(mp_structure_init(&s, MP_MEDIA_AUDIO_PCM), "init &s failed");
	zassert_ok(mp_value_set(&v, MP_TYPE_UINT, 1U), "set &v failed");

	for (uint8_t id = 0; id < CONFIG_MP_STRUCTURE_MAX_FIELDS; id++) {
		zassert_ok(mp_structure_append_value(&s, id, &v), "append of field %u failed", id);
	}

	zassert_equal(s.num_fields, CONFIG_MP_STRUCTURE_MAX_FIELDS, "structure did not fill up");
	zassert_equal(mp_structure_append_value(&s, CONFIG_MP_STRUCTURE_MAX_FIELDS, &v), -ENOSPC,
		      "append past the last slot did not report -ENOSPC");

	/* Freeing a slot makes room again */
	zassert_ok(mp_structure_remove_field(&s, 0), "remove_field failed");
	zassert_ok(mp_structure_append_value(&s, CONFIG_MP_STRUCTURE_MAX_FIELDS, &v),
		   "append after freeing a slot failed");
}
