/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>

#include <zephyr/mp/mp_value.h>

LOG_MODULE_REGISTER(mp_value, CONFIG_MP_LOG_LEVEL);

#define MP_VALUE_RANGES_OVERLAP(ref_val, cmp_val, vtype)                                           \
	!((ref_val)->range.min.vtype > (cmp_val)->range.max.vtype ||                               \
	  (cmp_val)->range.min.vtype > (ref_val)->range.max.vtype)

#define MP_SINGLE_VALUE_IN_RANGE(ref_val, cmp_val, vtype)                                          \
	(IN_RANGE((cmp_val)->vtype, (ref_val)->range.min.vtype, (ref_val)->range.max.vtype))

#define MP_VALUE_PRIMITIVE_MASK (BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_INT) | BIT(MP_TYPE_UINT))

static const uint32_t mp_value_intersect_mask[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = 0,
	[MP_TYPE_BOOLEAN] = BIT(MP_TYPE_BOOLEAN),
	[MP_TYPE_INT] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_INT_RANGE),
	[MP_TYPE_UINT] = BIT(MP_TYPE_UINT) | BIT(MP_TYPE_UINT_RANGE),
	[MP_TYPE_INT_RANGE] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_INT_RANGE),
	[MP_TYPE_UINT_RANGE] = BIT(MP_TYPE_UINT) | BIT(MP_TYPE_UINT_RANGE),
};

bool mp_value_is_primitive(const struct mp_value *value)
{
	if (value == NULL || !IN_RANGE(value->type, MP_TYPE_NONE + 1, MP_TYPE_COUNT - 1)) {
		return false;
	}

	return (MP_VALUE_PRIMITIVE_MASK & BIT(value->type)) != 0;
}

static void mp_value_set_range(struct mp_value *value, enum mp_value_type type, va_list *args)
{
	/*
	 * Reading an argument as a type it was not passed as is undefined, so
	 * the two range types are read apart even though the bounds share a
	 * union: a signed range is passed as int, an unsigned one as uint32_t.
	 */
	if (type == MP_TYPE_INT_RANGE) {
		value->range.min.v_int = va_arg(*args, int);
		value->range.max.v_int = va_arg(*args, int);
		value->range.step.v_int = va_arg(*args, int);
	} else {
		value->range.min.v_uint = va_arg(*args, uint32_t);
		value->range.max.v_uint = va_arg(*args, uint32_t);
		value->range.step.v_uint = va_arg(*args, uint32_t);
	}
}

int mp_value_set_va_list(struct mp_value *value, enum mp_value_type type, va_list *args)
{
	__ASSERT_NO_MSG(args != NULL);

	if (value == NULL) {
		return -EINVAL;
	}

	/* Any integer type narrower than int arrives as int through the variadic argument list */
	switch (type) {
	case MP_TYPE_BOOLEAN:
		/* A bool was promoted to int so it has to be narrowed here */
		value->v_boolean = (va_arg(*args, int) != 0);
		break;
	case MP_TYPE_INT:
		value->v_int = va_arg(*args, int);
		break;
	case MP_TYPE_UINT:
		value->v_uint = va_arg(*args, uint32_t);
		break;
	case MP_TYPE_INT_RANGE:
	case MP_TYPE_UINT_RANGE:
		mp_value_set_range(value, type, args);
		break;
	default:
		LOG_ERR("Unknown mp_value type: %d", type);
		return -EINVAL;
	}

	/* Stamp the type last so a rejected one leaves the value untouched */
	value->type = type;

	return 0;
}

int mp_value_set(struct mp_value *value, int type, ...)
{
	int ret;
	va_list args;

	va_start(args, type);
	ret = mp_value_set_va_list(value, type, &args);
	va_end(args);

	return ret;
}

int32_t mp_value_get_int(const struct mp_value *value)
{
	__ASSERT_NO_MSG(value != NULL);

	return value->v_int;
}

uint32_t mp_value_get_uint(const struct mp_value *value)
{
	__ASSERT_NO_MSG(value != NULL);

	return value->v_uint;
}

bool mp_value_get_boolean(const struct mp_value *value)
{
	__ASSERT_NO_MSG(value != NULL);

	return value->v_boolean;
}

int32_t mp_value_get_int_range_min(const struct mp_value *range)
{
	__ASSERT_NO_MSG(range != NULL);

	return range->range.min.v_int;
}

int32_t mp_value_get_int_range_max(const struct mp_value *range)
{
	__ASSERT_NO_MSG(range != NULL);

	return range->range.max.v_int;
}

int32_t mp_value_get_int_range_step(const struct mp_value *range)
{
	__ASSERT_NO_MSG(range != NULL);

	return range->range.step.v_int;
}

uint32_t mp_value_get_uint_range_min(const struct mp_value *range)
{
	__ASSERT_NO_MSG(range != NULL);

	return range->range.min.v_uint;
}

uint32_t mp_value_get_uint_range_max(const struct mp_value *range)
{
	__ASSERT_NO_MSG(range != NULL);

	return range->range.max.v_uint;
}

uint32_t mp_value_get_uint_range_step(const struct mp_value *range)
{
	__ASSERT_NO_MSG(range != NULL);

	return range->range.step.v_uint;
}

static bool mp_value_primitive_equal(const struct mp_value *val1, const struct mp_value *val2)
{
	switch (val1->type) {
	case MP_TYPE_BOOLEAN:
		return val1->v_boolean == val2->v_boolean;
	case MP_TYPE_INT:
		return val1->v_int == val2->v_int;
	case MP_TYPE_UINT:
		return val1->v_uint == val2->v_uint;
	default:
		return false;
	}
}

bool mp_value_can_intersect(const struct mp_value *val1, const struct mp_value *val2)
{
	struct mp_value scratch;

	/*
	 * Code saving by reusing mp_value_intersect(). Although it takes more time to
	 * compute, it only runs at pad-link time, so it is not a performance concern.
	 */
	return mp_value_intersect(val1, val2, &scratch) == 0;
}

static int mp_value_intersect_range(const struct mp_value *ref_val,
				    const struct mp_value *compare_val, struct mp_value *out)
{
	if (ref_val->type == MP_TYPE_INT_RANGE && compare_val->type == MP_TYPE_INT_RANGE) {
		if (!MP_VALUE_RANGES_OVERLAP(ref_val, compare_val, v_int)) {
			return -ENOENT;
		}

		out->type = MP_TYPE_INT_RANGE;
		out->range.min.v_int = MAX(ref_val->range.min.v_int, compare_val->range.min.v_int);
		out->range.max.v_int = MIN(ref_val->range.max.v_int, compare_val->range.max.v_int);
		out->range.step.v_int =
			(int32_t)sys_gcd(ref_val->range.step.v_int, compare_val->range.step.v_int);

		return 0;
	}

	if (ref_val->type == MP_TYPE_UINT_RANGE && compare_val->type == MP_TYPE_UINT_RANGE) {
		if (!MP_VALUE_RANGES_OVERLAP(ref_val, compare_val, v_uint)) {
			return -ENOENT;
		}

		out->type = MP_TYPE_UINT_RANGE;
		out->range.min.v_uint =
			MAX(ref_val->range.min.v_uint, compare_val->range.min.v_uint);
		out->range.max.v_uint =
			MIN(ref_val->range.max.v_uint, compare_val->range.max.v_uint);
		out->range.step.v_uint =
			sys_gcd(ref_val->range.step.v_uint, compare_val->range.step.v_uint);

		return 0;
	}

	if ((ref_val->type == MP_TYPE_INT_RANGE && compare_val->type == MP_TYPE_INT &&
	     MP_SINGLE_VALUE_IN_RANGE(ref_val, compare_val, v_int)) ||
	    (ref_val->type == MP_TYPE_UINT_RANGE && compare_val->type == MP_TYPE_UINT &&
	     MP_SINGLE_VALUE_IN_RANGE(ref_val, compare_val, v_uint))) {
		*out = *compare_val;
		return 0;
	}

	return -ENOENT;
}

int mp_value_intersect(const struct mp_value *val1, const struct mp_value *val2,
		       struct mp_value *out)
{
	const struct mp_value *ref_val, *compare_val;

	if (out == NULL) {
		return -EINVAL;
	}

	/* Only a pair of types the mask allows can have a common value */
	if (val1 == NULL || val2 == NULL ||
	    !IN_RANGE(val1->type, MP_TYPE_NONE, MP_TYPE_COUNT - 1) ||
	    !IN_RANGE(val2->type, MP_TYPE_NONE, MP_TYPE_COUNT - 1) ||
	    (mp_value_intersect_mask[val1->type] & BIT(val2->type)) == 0) {
		return -ENOENT;
	}

	/*
	 * A container type always has a higher ordinal than the scalar type
	 * it can contain, so the greater of the two is the one to dispatch on.
	 */
	if (val1->type >= val2->type) {
		ref_val = val1;
		compare_val = val2;
	} else {
		ref_val = val2;
		compare_val = val1;
	}

	if (mp_value_is_primitive(ref_val)) {
		if (!mp_value_primitive_equal(val1, val2)) {
			return -ENOENT;
		}

		*out = *val1;

		return 0;
	}

	switch (ref_val->type) {
	case MP_TYPE_INT_RANGE:
	case MP_TYPE_UINT_RANGE:
		return mp_value_intersect_range(ref_val, compare_val, out);
	default:
		return -ENOENT;
	}
}

static inline void mp_value_print_boolean(const struct mp_value *value)
{
	printk("%s", value->v_boolean ? "true" : "false");
}

static inline void mp_value_print_int(const struct mp_value *value)
{
	printk("%d", value->v_int);
}

static inline void mp_value_print_uint(const struct mp_value *value)
{
	printk("%u", value->v_uint);
}

static inline void mp_value_print_int_range(const struct mp_value *value)
{
	printk("[%d, %d, %d]", value->range.min.v_int, value->range.max.v_int,
	       value->range.step.v_int);
}

static inline void mp_value_print_uint_range(const struct mp_value *value)
{
	printk("[%u, %u, %u]", value->range.min.v_uint, value->range.max.v_uint,
	       value->range.step.v_uint);
}

typedef void (*mp_value_print_fn)(const struct mp_value *);

static const mp_value_print_fn mp_value_print_table[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = NULL,
	[MP_TYPE_BOOLEAN] = mp_value_print_boolean,
	[MP_TYPE_INT] = mp_value_print_int,
	[MP_TYPE_UINT] = mp_value_print_uint,
	[MP_TYPE_INT_RANGE] = mp_value_print_int_range,
	[MP_TYPE_UINT_RANGE] = mp_value_print_uint_range,
};

void mp_value_print(const struct mp_value *value, bool new_line)
{
	if (value == NULL || value->type >= ARRAY_SIZE(mp_value_print_table) ||
	    mp_value_print_table[value->type] == NULL) {
		LOG_ERR("Invalid mp_value to print");
		return;
	}

	mp_value_print_table[value->type](value);

	if (new_line) {
		printk("\n");
	}
}
