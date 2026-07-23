/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTS_SUBSYS_MP_CORE_UNIT_SRC_MP_TEST_HELPERS_H_
#define TESTS_SUBSYS_MP_CORE_UNIT_SRC_MP_TEST_HELPERS_H_

#include <zephyr/mp/mp_value.h>
#include <zephyr/ztest.h>

#define validate_boolean_value(value, expected)                                                    \
	({                                                                                         \
		zassert_not_null(value);                                                           \
		zassert_equal(mp_value_get_type(value), MP_TYPE_BOOLEAN);                          \
		zassert_equal(mp_value_get_boolean(value), expected);                              \
	})

#define validate_int_value(value, expected)                                                        \
	({                                                                                         \
		zassert_not_null(value);                                                           \
		zassert_equal(mp_value_get_type(value), MP_TYPE_INT);                              \
		zassert_equal(mp_value_get_int(value), expected);                                  \
	})

#define validate_string_value(value, expected)                                                     \
	({                                                                                         \
		zassert_not_null(value);                                                           \
		zassert_equal(mp_value_get_type(value), MP_TYPE_STRING);                           \
		zassert_str_equal(mp_value_get_string(value), expected);                           \
	})

#define validate_range_value(value, min, max, step)                                                \
	({                                                                                         \
		zassert_not_null(value);                                                           \
		zassert_equal(mp_value_get_type(value), MP_TYPE_RANGE);                            \
		zassert_equal(mp_value_get_range_min(value), min);                                 \
		zassert_equal(mp_value_get_range_max(value), max);                                 \
		zassert_equal(mp_value_get_range_step(value), step);                               \
	})

#define validate_list_value_type_and_size(value, expected_size)                                    \
	({                                                                                         \
		zassert_not_null(value);                                                           \
		zassert_equal(mp_value_get_type(value), MP_TYPE_LIST);                             \
		zassert_equal(mp_value_list_get_size(value), expected_size);                       \
	})

#endif /* TESTS_SUBSYS_MP_CORE_UNIT_SRC_MP_TEST_HELPERS_H_ */
