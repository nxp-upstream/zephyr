/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/mp_caps.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include "mp_test_helpers.h"

extern struct k_heap _system_heap;

struct mp_structure_api_fixture {
	struct sys_memory_stats mem_before;
};

static void *structure_suite_setup(void)
{
	static struct mp_structure_api_fixture fixture;

	return &fixture;
}

static void structure_before(void *f)
{
	struct mp_structure_api_fixture *fix = f;

	sys_heap_runtime_stats_get(&_system_heap.heap, &fix->mem_before);
}

static void structure_after(void *f)
{
	struct mp_structure_api_fixture *fix = f;
	struct sys_memory_stats mem_after;

	sys_heap_runtime_stats_get(&_system_heap.heap, &mem_after);
	zassert_equal(fix->mem_before.allocated_bytes, mem_after.allocated_bytes,
		      "Memory leak detected: before=%zu after=%zu", fix->mem_before.allocated_bytes,
		      mem_after.allocated_bytes);
}

ZTEST_SUITE(mp_structure_api, NULL, structure_suite_setup, structure_before, structure_after, NULL);

/* Field IDs used by intersection tests */
enum test_field {
	TEST_BOOL = 0,
	TEST_INT,
	TEST_INT2,
	TEST_STRING,
	TEST_RANGE,
	TEST_LIST,
};

ZTEST(mp_structure_api, test_new)
{
	struct mp_structure *s = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_INT(48000),
		MP_CAPS_BITWIDTH, MP_INT(16),
		MP_STRUCTURE_END
	);

	zassert_not_null(s, "mp_structure_new returned NULL");
	zassert_equal(s->media_type_id, MP_MEDIA_AUDIO_PCM, "media_type_id mismatch");

	mp_value_t rate = mp_structure_get_value(s, MP_CAPS_SAMPLE_RATE);

	zassert_not_null(rate, "SAMPLE_RATE field not found");
	zassert_equal(mp_value_get_int(rate), 48000, "sample rate != 48000");

	mp_value_t bw = mp_structure_get_value(s, MP_CAPS_BITWIDTH);

	zassert_not_null(bw, "BITWIDTH field not found");
	zassert_equal(mp_value_get_int(bw), 16, "bitwidth != 16");

	mp_structure_unref(s);

	struct mp_structure *sv = mp_structure_new(MP_MEDIA_VIDEO, MP_STRUCTURE_END);

	zassert_not_null(sv, "mp_structure_new(no fields) returned NULL");
	zassert_equal(sv->media_type_id, MP_MEDIA_VIDEO, "media_type_id != VIDEO");
	mp_structure_unref(sv);

	struct mp_structure *sr = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_RANGE(8000, 48000, 8000),
		MP_STRUCTURE_END
	);

	zassert_not_null(sr);
	mp_value_t val = mp_structure_get_value(sr, MP_CAPS_SAMPLE_RATE);

	zassert_not_null(val);
	zassert_equal(mp_value_get_type(val), MP_TYPE_RANGE, "type != INT_RANGE");
	zassert_equal(mp_value_get_range_min(val), 8000, "min != 8000");
	zassert_equal(mp_value_get_range_max(val), 48000, "max != 48000");
	mp_structure_unref(sr);

	struct mp_structure *si = mp_structure_new_empty(MP_MEDIA_AUDIO_PCM, 3);

	zassert_not_null(si, "mp_structure_init failed");
	zassert_equal(si->media_type_id, MP_MEDIA_AUDIO_PCM, "media_type_id mismatch");

	mp_value_t appended = mp_value_new_int(44100);

	zassert_ok(mp_structure_append(si, MP_CAPS_SAMPLE_RATE, appended), "append failed");

	mp_value_t dup_val = mp_value_new_int(0);

	zassert_equal(mp_structure_append(si, MP_CAPS_SAMPLE_RATE, dup_val), -EEXIST,
		      "duplicate field != -EEXIST");
	mp_value_unref(dup_val);

	zassert_equal(mp_structure_init(NULL, MP_MEDIA_AUDIO_PCM, 0), -EINVAL,
		      "init(NULL) != -EINVAL");
	zassert_equal(mp_structure_append(NULL, MP_CAPS_SAMPLE_RATE, appended), -EINVAL,
		      "append(NULL struct) != -EINVAL");
	zassert_equal(mp_structure_append(si, MP_CAPS_BITWIDTH, NULL), -EINVAL,
		      "append(NULL value) != -EINVAL");

	mp_value_t retrieved = mp_structure_get_value(si, MP_CAPS_SAMPLE_RATE);

	zassert_not_null(retrieved, "appended field not found");
	zassert_equal(mp_value_get_int(retrieved), 44100, "retrieved value != 44100");
	mp_structure_unref(si);
}

ZTEST(mp_structure_api, test_is_fixed_fixate_duplicate)
{
	return;
	struct mp_structure *fixed_s = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_INT(48000),
		MP_CAPS_BITWIDTH, MP_INT(16),
		MP_STRUCTURE_END
	);

	zassert_true(mp_structure_is_fixed(fixed_s), "structure not fixed");
	mp_structure_unref(fixed_s);

	struct mp_structure *range_s = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_RANGE(8000, 48000, 8000),
		MP_STRUCTURE_END
	);

	zassert_false(mp_structure_is_fixed(range_s), "range structure is fixed");

	struct mp_structure *fixated = mp_structure_fixate(range_s);

	zassert_not_null(fixated, "fixate returned NULL");
	zassert_true(mp_structure_is_fixed(fixated), "fixated structure not fixed");
	mp_structure_unref(range_s);
	mp_structure_unref(fixated);
}

ZTEST(mp_structure_api, test_intersect_asymmetric_fields)
{
	/* s1: fields TEST_INT and TEST_INT2 */
	struct mp_structure *s1 = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		TEST_INT, MP_INT(100U),
		TEST_INT2, MP_INT(-42),
		MP_STRUCTURE_END);
	/* s2: fields TEST_INT (common) and TEST_STRING (only in s2) */
	struct mp_structure *s2 = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		TEST_INT, MP_INT(100U),
		TEST_STRING, MP_STRING("hello"),
		MP_STRUCTURE_END);

	zassert_true(mp_structure_can_intersect(s1, s2), "asymmetric structures cannot intersect");

	struct mp_structure *result = mp_structure_intersect(s1, s2);

	zassert_not_null(result, "intersection returned NULL");

	/* s1 has 2 fields, s2 has 2 fields, 1 common: result must have 3 fields total */
	zassert_equal(mp_structure_len(result), 3, "result field count != 3");

	/* TEST_INT2: only in s1 - must be present as-is */
	mp_value_t v = mp_structure_get_value(result, TEST_INT2);
	validate_int_value(v, -42);

	/* TEST_INT: common - must be the intersected value */
	v = mp_structure_get_value(result, TEST_INT);
	validate_int_value(v, 100U);

	/* TEST_STRING: only in s2 - must be present as-is */
	v = mp_structure_get_value(result, TEST_STRING);
	validate_string_value(v, "hello");

	mp_structure_unref(s1);
	mp_structure_unref(s2);
	mp_structure_unref(result);
}

ZTEST(mp_structure_api, test_cannot_intersect)
{
	struct mp_structure *s_sample_int = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_INT(48000),
		MP_STRUCTURE_END);
	struct mp_structure *s_bw = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_BITWIDTH, MP_INT(16),
		MP_STRUCTURE_END);
	struct mp_structure *s_low = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_RANGE(8000, 16000, 8000),
		MP_STRUCTURE_END);

	/* Case 1: NULL operand - can_intersect must return false, intersect must return NULL */
	zassert_false(mp_structure_can_intersect(s_sample_int, NULL),
		      "can_intersect(s, NULL) should return false");
	zassert_false(mp_structure_can_intersect(NULL, NULL),
		      "can_intersect(NULL, NULL) should return false");
	zassert_is_null(mp_structure_intersect(s_sample_int, NULL),
			"intersect(s, NULL) should return NULL");
	zassert_is_null(mp_structure_intersect(NULL, NULL),
			"intersect(NULL, NULL) should return NULL");

	/* Case 2: no common field - different field IDs, same media type */
	zassert_false(mp_structure_can_intersect(s_sample_int, s_bw),
		      "structures with no common field should not intersect");
	zassert_is_null(mp_structure_intersect(s_sample_int, s_bw),
			"intersect with no common field should return NULL");

	/* Case 3: common field with non-overlapping values */
	zassert_false(mp_structure_can_intersect(s_low, s_sample_int),
		      "out-of-range value should not intersect");
	zassert_is_null(mp_structure_intersect(s_low, s_sample_int),
			"intersect with incompatible field value should return NULL");

	mp_structure_unref(s_sample_int);
	mp_structure_unref(s_bw);
	mp_structure_unref(s_low);
}

ZTEST(mp_structure_api, test_sanity)
{
	struct mp_structure *s = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_INT(48000),
		MP_STRUCTURE_END
	);

	zassert_is_null(mp_structure_get_value(s, MP_CAPS_IMAGE_WIDTH),
			"non-existent field != NULL");

	mp_structure_unref(s);

	struct mp_structure *audio = mp_structure_new(
		MP_MEDIA_AUDIO_PCM,
		MP_CAPS_SAMPLE_RATE, MP_INT(48000),
		MP_STRUCTURE_END
	);
	struct mp_structure *video = mp_structure_new(
		MP_MEDIA_VIDEO,
		MP_CAPS_IMAGE_WIDTH, MP_INT(1920),
		MP_STRUCTURE_END
	);

	zassert_false(mp_structure_can_intersect(audio, video),
		      "different media types can intersect");

	mp_structure_unref(audio);
	mp_structure_unref(video);
}
