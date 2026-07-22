/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/minmax.h>

#include <zephyr/mp/mp_value.h>

LOG_MODULE_REGISTER(mp_value, CONFIG_MP_LOG_LEVEL);

#define MP_VALUE_SIMPLE(value)               ((struct mp_value_simple *)value)
#define MP_VALUE_RANGE(value)                ((struct mp_value_range *)value)
#define MP_VALUE_LIST(value)                 ((struct mp_value_list *)value)
#define MP_VALUE_CONST(value)                ((const mp_value_t)value)
#define MP_VALUE_SIMPLE_CONST(value)         ((const struct mp_value_simple *)value)
#define MP_VALUE_RANGE_CONST(value)          ((const struct mp_value_range *)value)
#define MP_VALUE_LIST_CONST(value)           ((const struct mp_value_list *)value)

/*
 * This is the value representation in binary for 32-bit systems, taking advantage
 * that all pointers involved are aligned on 32-bit:
 *
 * - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx00 - a pointer to a @c VALUE, 'x' is the pointer
 * - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx01 - a pointer to an @c OBJECT, 'x' is the pointer
 * - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx10 - other pointer type (@c PTR), 'x' is the pointer
 * - xxxxxxxxxxxxxxxxxxxxxxxxxxxxtt11 - an immediate value, 't' is type, 'x' is the value
 */

#define MP_VALUE_IS_VALUE_PTR(value)	(((uintptr_t)(value) & 0x3U) == 0x0)
#define MP_VALUE_IS_OBJECT_PTR(value)	(((uintptr_t)(value) & 0x3U) == 0x1)
#define MP_VALUE_IS_OTHER_PTR(value)	(((uintptr_t)(value) & 0x3U) == 0x2)
#define MP_VALUE_IS_IMMEDIATE(value)	(((uintptr_t)(value) & 0x3U) == 0x3)

#define MP_VALUE_IS_FITTING(i) (((uint64_t)i >> 28) == 0)
#define MP_VALUE_IS_NULL(value)                                                                    \
	(!MP_VALUE_IS_IMMEDIATE(value) && MP_VALUE_GET_PTR(value) == NULL)
#define MP_VALUE_IS_VALID(value)                                                                   \
	IN_RANGE(mp_value_get_type(value), MP_TYPE_NONE + 1, MP_TYPE_COUNT - 1)

#define MP_VALUE_GET_IMMEDIATE(value)	((uintptr_t)(value) >> 4)
#define MP_VALUE_GET_TYPE(value)	((((uintptr_t)(value) >> 2) & 0x3U) + 1)
#define MP_VALUE_GET_PTR(value)		((void *)((uintptr_t)(value) & ~0x3U))

#define MP_VALUE_SET_TYPE(value, type)                                                             \
	((mp_value_t)(((uintptr_t)(value) & ~0x0CU) | (((type) - 1) << 2)))

#define MP_VALUE_NEW_VALUE_PTR(uptr)	((mp_value_t)((uintptr_t)(uptr) | 0x0U))
#define MP_VALUE_NEW_OBJECT_PTR(uptr)	((mp_value_t)((uintptr_t)(uptr) | 0x1U))
#define MP_VALUE_NEW_OTHER_PTR(uptr)	((mp_value_t)((uintptr_t)(uptr) | 0x2U))
#define MP_VALUE_NEW_IMMEDIATE(type, uptr)                                                         \
	((mp_value_t)(((uintptr_t)(uptr) << 4) | (((type) - 1) << 2) | 0x3U))

/*
 * MP_TYPE_RANGE packing is aaaaaaaaaaaabbbbbbbbbbbbcccctt11 where:
 * - 'a' is (max - 1) up to (12-bit)
 * - 'b' is (min - 1) up to (12-bit)
 * - 'c' is log2(step) up to (4-bit)
 */
#define MP_VALUE_GET_RANGE_MIN(uptr)	((((uintptr_t)(uptr) >> 20) & 0xFFF) + 1)
#define MP_VALUE_GET_RANGE_MAX(uptr)	((((uintptr_t)(uptr) >> 8) & 0xFFF) + 1)
#define MP_VALUE_GET_RANGE_STEP(uptr)	(1U << (((uintptr_t)(uptr) >> 4) & 0xF))
#define MP_VALUE_PACK_RANGE(min, max, step)                                                        \
	(((((min) - 1) & 0xFFF) << 16) |                                                           \
	 ((((max) - 1) & 0xFFF) << 4) |                                                            \
	 (LOG2(MAX((step), 1)) & 0xF))

#define MP_COMPARE(a, b)                                                                           \
	({                                                                                         \
		__typeof__(a) _a = (a);                                                            \
		__typeof__(b) _b = (b);                                                            \
		(_a < _b) ? MP_VALUE_LESS_THAN                                                     \
			  : ((_a > _b) ? MP_VALUE_GREATER_THAN : MP_VALUE_EQUAL);                  \
	})

#define MP_VALUE_RANGES_OVERLAP(ref_val, cmp_val)                                                  \
	!((mp_value_get_range_min(ref_val) > mp_value_get_range_max(cmp_val)) |                    \
	  (mp_value_get_range_min(cmp_val) > mp_value_get_range_max(ref_val)))

#define MP_VALUE_NEW_INTERSECT_RANGE(ref_val, cmp_val)                                             \
	MP_VALUE_RANGES_OVERLAP(ref_val, cmp_val)                                                  \
	? mp_value_new_range(                                                                      \
		MAX(mp_value_get_range_min(ref_val), mp_value_get_range_min(cmp_val)),             \
		MIN(mp_value_get_range_max(ref_val), mp_value_get_range_max(cmp_val)),             \
		sys_gcd(mp_value_get_range_step(ref_val),                                          \
			mp_value_get_range_step(compare_val))                                      \
		)                                                                                  \
	: NULL

struct mp_value_simple {
	struct mp_value base;
	union {
		int64_t v_int;
		const char *v_cstring;
	};
};

struct mp_value_list {
	struct mp_value base;
	size_t size;
	mp_value_t v_list[];
};

struct mp_value_range {
	struct mp_value base;
	int64_t min;
	int64_t max;
	int64_t step;
};

static const size_t mp_value_type_sizes[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = sizeof(struct mp_value_simple),
	[MP_TYPE_BOOLEAN] = sizeof(struct mp_value_simple),
	[MP_TYPE_ENUM] = sizeof(struct mp_value_simple),
	[MP_TYPE_INT] = sizeof(struct mp_value_simple),
	[MP_TYPE_STRING] = sizeof(struct mp_value_simple),
	[MP_TYPE_RANGE] = sizeof(struct mp_value_range),
	[MP_TYPE_LIST] = sizeof(struct mp_value_list),
	[MP_TYPE_OBJECT] = 0,
	[MP_TYPE_PTR] = 0,
};

static const uint32_t mp_value_intersect_mask[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = 0,
	[MP_TYPE_BOOLEAN] = BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_LIST),
	[MP_TYPE_ENUM] = BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_LIST),
	[MP_TYPE_INT] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_STRING] = BIT(MP_TYPE_STRING) | BIT(MP_TYPE_LIST),
	[MP_TYPE_RANGE] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_LIST] = BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_INT) |
			 BIT(MP_TYPE_STRING) | BIT(MP_TYPE_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_OBJECT] = 0,
	[MP_TYPE_PTR] = 0,
};


enum mp_value_type mp_value_get_type(const mp_value_t value)
{
	uint32_t type;

	if (MP_VALUE_IS_IMMEDIATE(value)) {
		type = MP_VALUE_GET_TYPE(value);
	} else if (MP_VALUE_IS_OBJECT_PTR(value)) {
		type = MP_TYPE_OBJECT;
	} else if (MP_VALUE_IS_VALUE_PTR(value)) {
		type = value->_type;
	} else {
		type = MP_TYPE_NONE;
	}

	compiler_barrier();
	return type;
}

void mp_value_set_type(mp_value_t *value, enum mp_value_type type)
{
	if (MP_VALUE_IS_IMMEDIATE(*value)) {
		*value = MP_VALUE_SET_TYPE(*value, type);
	} else if (MP_VALUE_IS_VALUE_PTR(*value)) {
		(*value)->_type = type;
	} else {
		/* Other types are encoded directly in the pointers */
	}
}

bool mp_value_is_primitive(const mp_value_t value)
{
	enum mp_value_type type = mp_value_get_type(value);

	if (MP_VALUE_IS_NULL(value) || !MP_VALUE_IS_VALID(value)) {
		return false;
	}

	return ((BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_INT) |
		 BIT(MP_TYPE_STRING)) &
		BIT(type)) != 0;
}

bool mp_value_range_is_fitting(int64_t min, int64_t max, int64_t step)
{
	return false;
	if ((min < 1) || (max < 1) || (min - 1) >> 16 != 0 || (max - 1) >> 16 != 0) {
		return false;
	}
	if (min == max) {
		return true;
	}
	if (LOG2(step) >> 4 != 0 || step != 1U << LOG2(step)) {
		return false;
	}
	return true;
}

mp_value_t mp_value_new_range(int64_t min, int64_t max, int64_t step)
{
	mp_value_t value;

	if (mp_value_range_is_fitting(min, max, step)) {
		return MP_VALUE_NEW_IMMEDIATE(MP_TYPE_RANGE, MP_VALUE_PACK_RANGE(min, max, step));
	}

	value = k_calloc(1, mp_value_type_sizes[MP_TYPE_RANGE]);
	if (value == NULL) {
		LOG_ERR("Failed to allocate mp_value_t");
		return NULL;
	}

	mp_value_set_type(&value, MP_TYPE_RANGE);

	MP_VALUE_RANGE(value)->min = min;
	MP_VALUE_RANGE(value)->max = max;
	MP_VALUE_RANGE(value)->step = step;

	return value;
}

static mp_value_t mp_value_new_simple(enum mp_value_type type, int64_t i)
{
	mp_value_t value;

	if (MP_VALUE_IS_FITTING(i)) {
		return MP_VALUE_NEW_IMMEDIATE(type, i);
	}

	value = k_calloc(1, mp_value_type_sizes[MP_TYPE_INT]);
	if (value == NULL) {
		LOG_ERR("Failed to allocate mp_value_t");
		return NULL;
	}

	mp_value_set_type(&value, type);

	MP_VALUE_SIMPLE(value)->v_int = i;

	return value;
}

mp_value_t mp_value_new_int(int64_t i)
{
	return mp_value_new_simple(MP_TYPE_INT, i);
}

mp_value_t mp_value_new_enum(int e)
{
	return mp_value_new_simple(MP_TYPE_ENUM, e);
}

mp_value_t mp_value_new_boolean(bool b)
{
	return mp_value_new_simple(MP_TYPE_BOOLEAN, b);
}

mp_value_t mp_value_new_string(const char *s)
{
	mp_value_t value;

	value = k_calloc(1, mp_value_type_sizes[MP_TYPE_STRING]);
	if (value == NULL) {
		LOG_ERR("Failed to allocate string");
		return NULL;
	}

	mp_value_set_type(&value, MP_TYPE_STRING);

	MP_VALUE_SIMPLE(value)->v_cstring = s;

	return value;
}

mp_value_t mp_value_new_list(size_t size, mp_value_t *values)
{
	mp_value_t list;

	list = k_malloc(sizeof(struct mp_value_list) + size * sizeof(mp_value_t));
	if (list == NULL) {
		return NULL;
	}

	mp_value_set_type(&list, MP_TYPE_LIST);

	MP_VALUE_LIST(list)->size = size;

	if (values != NULL) {
		memcpy(MP_VALUE_LIST(list)->v_list, values, sizeof(mp_value_t) * size);
	} else {
		memset(MP_VALUE_LIST(list)->v_list, 0, sizeof(mp_value_t) * size);
	}

	return list;
}

const char *mp_value_get_string(const mp_value_t value)
{
	return MP_VALUE_SIMPLE_CONST(value)->v_cstring;
}

int64_t mp_value_get_int(const mp_value_t value)
{
	if (MP_VALUE_IS_IMMEDIATE(value)) {
		return MP_VALUE_GET_IMMEDIATE(value);
	}
	return MP_VALUE_SIMPLE_CONST(value)->v_int;
}

void *mp_value_get_ptr(const mp_value_t value)
{
	return MP_VALUE_GET_PTR(value);
}

bool mp_value_get_boolean(const mp_value_t value)
{
	return MP_VALUE_GET_IMMEDIATE(value);
}

int mp_value_destroy(mp_value_t value)
{
	int ret = 0;

	if (MP_VALUE_IS_NULL(value) || MP_VALUE_IS_IMMEDIATE(value)) {
		return 0;
	}

	if (mp_value_get_type(value) == MP_TYPE_LIST) {
		for (size_t i = 0; i < MP_VALUE_LIST(value)->size; i++) {
			mp_value_destroy(mp_value_list_get(value, i));
		}
	}

	if (MP_VALUE_IS_VALUE_PTR(value)) {
		k_free(MP_VALUE_GET_PTR(value));
	}

	if (MP_VALUE_IS_OBJECT_PTR(value)) {
		mp_object_unref(MP_VALUE_GET_PTR(value));
	}

	return ret;
}

mp_value_t mp_value_duplicate(const mp_value_t src)
{
	mp_value_t dst;

	if (MP_VALUE_IS_NULL(src)) {
		return NULL;
	}

	if (mp_value_get_type(src) == MP_TYPE_LIST) {
		dst = mp_value_new_list(MP_VALUE_LIST(src)->size, NULL);
		if (dst == NULL) {
			return NULL;
		}

		for (int i = 0; i < MP_VALUE_LIST(src)->size; i++) {
			mp_value_t dup = mp_value_duplicate(mp_value_list_get(src, i));
			if (dup == NULL) {
				mp_value_destroy(dst);
				return NULL;
			}

			mp_value_list_set(dst, i, dup);
		}

		return dst;
	}

	if (MP_VALUE_IS_OBJECT_PTR(src)) {
		dst = MP_VALUE_NEW_OBJECT_PTR(MP_VALUE_GET_PTR(src));
		mp_object_ref(MP_VALUE_GET_PTR(dst));
		return dst;
	}

	if (MP_VALUE_IS_VALUE_PTR(src)) {
		dst = k_calloc(1, mp_value_type_sizes[mp_value_get_type(src)]);
		if (dst == NULL) {
			return NULL;
		}
		memcpy(dst, src, mp_value_type_sizes[mp_value_get_type(src)]);
		return dst;
	}

	if (MP_VALUE_IS_OTHER_PTR(src)) {
		return MP_VALUE_NEW_OTHER_PTR(MP_VALUE_GET_PTR(src));
	}

	if (MP_VALUE_IS_IMMEDIATE(src)) {
		return MP_VALUE_NEW_IMMEDIATE(MP_VALUE_GET_TYPE(src), MP_VALUE_GET_IMMEDIATE(src));
	}

	return NULL;
}

void mp_value_list_set(mp_value_t list, int index, mp_value_t value)
{
	__ASSERT_NO_MSG(index < MP_VALUE_LIST(list)->size);
	MP_VALUE_LIST(list)->v_list[index] = value;
}

mp_value_t mp_value_list_get(const mp_value_t list, int index)
{
	if (index >= MP_VALUE_LIST(list)->size) {
		return NULL;
	}
	return MP_VALUE_LIST_CONST(list)->v_list[index];
}

bool mp_value_list_is_empty(const mp_value_t list)
{
	return MP_VALUE_LIST_CONST(list)->size == 0;
}

size_t mp_value_list_get_size(const mp_value_t list)
{
	return MP_VALUE_LIST_CONST(list)->size;
}

void mp_value_list_set_size(mp_value_t list, size_t size)
{
	if (size < MP_VALUE_LIST(list)->size) {
		MP_VALUE_LIST(list)->size = size;
	}
}

int64_t mp_value_get_range_min(const mp_value_t range)
{
	if (MP_VALUE_IS_IMMEDIATE(range)) {
		return MP_VALUE_GET_RANGE_MIN(range);
	}
	return MP_VALUE_RANGE_CONST(range)->min;
}

int64_t mp_value_get_range_max(const mp_value_t range)
{
	if (MP_VALUE_IS_IMMEDIATE(range)) {
		return MP_VALUE_GET_RANGE_MAX(range);
	}
	return MP_VALUE_RANGE_CONST(range)->max;
}

int64_t mp_value_get_range_step(const mp_value_t range)
{
	if (MP_VALUE_IS_IMMEDIATE(range)) {
		return MP_VALUE_GET_RANGE_STEP(range);
	}
	return MP_VALUE_RANGE_CONST(range)->step;
}

struct mp_object *mp_value_get_object(mp_value_t value)
{
	return MP_VALUE_GET_PTR(value);
}

static int mp_value_list_compare(const mp_value_t list1, const mp_value_t list2);

int mp_value_compare(const mp_value_t val1, const mp_value_t val2)
{
	if (MP_VALUE_IS_NULL(val1) || MP_VALUE_IS_NULL(val2)) {
		return MP_VALUE_COMPARE_FAILED;
	}

	if (mp_value_get_type(val1) != mp_value_get_type(val2)) {
		return MP_VALUE_COMPARE_FAILED;
	}

	switch (mp_value_get_type(val1)) {
	case MP_TYPE_BOOLEAN:
	case MP_TYPE_ENUM:
		return mp_value_get_int(val1) == mp_value_get_int(val2)
			       ? MP_VALUE_EQUAL
			       : MP_VALUE_UNORDERED;
	case MP_TYPE_INT:
		return MP_COMPARE(mp_value_get_int(val1), mp_value_get_int(val2));
	case MP_TYPE_STRING:
		return strcmp(MP_VALUE_SIMPLE_CONST(val1)->v_cstring,
			      MP_VALUE_SIMPLE_CONST(val2)->v_cstring) == 0
			       ? MP_VALUE_EQUAL
			       : MP_VALUE_UNORDERED;
	case MP_TYPE_RANGE:
		return (mp_value_get_range_min(val1) == mp_value_get_range_min(val2) &&
			mp_value_get_range_max(val1) == mp_value_get_range_max(val2) &&
			mp_value_get_range_step(val1) == mp_value_get_range_step(val2))
				? MP_VALUE_EQUAL
				: MP_VALUE_UNORDERED;
	case MP_TYPE_LIST:
		return mp_value_list_compare(val1, val2);
	default:
		return MP_VALUE_COMPARE_FAILED;
	}
}

static int mp_value_list_compare(const mp_value_t list1, const mp_value_t list2)
{
	int size1 = mp_value_list_get_size(list1);
	int size2 = mp_value_list_get_size(list2);
	int count_matched = 0;

	if (mp_value_get_type(list1) != MP_TYPE_LIST || mp_value_get_type(list2) != MP_TYPE_LIST) {
		return MP_VALUE_COMPARE_FAILED;
	}

	if (size1 != size2) {
		return MP_VALUE_UNORDERED;
	}

	for (size_t i1 = 0; i1 < MP_VALUE_LIST(list1)->size; i1++) {
		for (size_t i2 = 0; i2 < MP_VALUE_LIST(list2)->size; i2++) {
			if (mp_value_compare(MP_VALUE_LIST(list1)->v_list[i1],
					     MP_VALUE_LIST(list2)->v_list[i2]) == MP_VALUE_EQUAL) {
				count_matched++;
			}
		}
	}

	return count_matched == size1 ? MP_VALUE_EQUAL : MP_VALUE_UNORDERED;
}

bool mp_value_can_intersect(const mp_value_t val1, const mp_value_t val2)
{
	if (MP_VALUE_IS_NULL(val1) || MP_VALUE_IS_NULL(val2) ||
	    !MP_VALUE_IS_VALID(val1) || !MP_VALUE_IS_VALID(val2)) {
		return false;
	}
	return (mp_value_intersect_mask[mp_value_get_type(val1)] &
		BIT(mp_value_get_type(val2))) != 0;
}

mp_value_t mp_value_intersect_range(const mp_value_t ref_val,
				    const mp_value_t compare_val)
{
	if (mp_value_get_type(compare_val) == MP_TYPE_RANGE &&
	    mp_value_get_type(ref_val) == MP_TYPE_RANGE) {
		return MP_VALUE_NEW_INTERSECT_RANGE(ref_val, compare_val);
	}

	if (mp_value_get_type(ref_val) == MP_TYPE_RANGE &&
	    mp_value_get_type(compare_val) == MP_TYPE_INT &&
	    IN_RANGE(mp_value_get_int(compare_val),
		     mp_value_get_range_min(ref_val),
		     mp_value_get_range_max(ref_val))) {
		return mp_value_new_int(mp_value_get_int(compare_val));
	}

	return NULL;
}

mp_value_t mp_value_intersect_list(const mp_value_t list,
					 const mp_value_t compare_val)
{
	mp_value_t intersect_value = NULL;
	mp_value_t intersect_list = NULL;
	mp_value_t value1, value2;

	if (MP_VALUE_IS_NULL(list) || MP_VALUE_IS_NULL(compare_val) ||
	    !MP_VALUE_IS_VALID(list) || !MP_VALUE_IS_VALID(compare_val)) {
		return NULL;
	}

	intersect_list = mp_value_new_list(min(mp_value_list_get_size(list),
					       mp_value_list_get_size(compare_val)), NULL);
	if (intersect_list == NULL) {
		LOG_ERR("Failed to allocate result list");
		return NULL;
	}

	size_t o = 0;
	for (size_t i1 = 0; i1 < MP_VALUE_LIST(list)->size; i1++) {
		intersect_value = NULL;
		value1 = MP_VALUE_LIST(list)->v_list[i1];

		switch (mp_value_get_type(compare_val)) {
		case MP_TYPE_BOOLEAN:
		case MP_TYPE_ENUM:
		case MP_TYPE_INT:
		case MP_TYPE_STRING:
			if (mp_value_compare(compare_val, value1) == MP_VALUE_EQUAL) {
				intersect_value = mp_value_duplicate(compare_val);
			}
			break;
		case MP_TYPE_RANGE:
			intersect_value = mp_value_intersect_range(compare_val, value1);
			break;
		case MP_TYPE_LIST:
			for (size_t i2 = 0; i2 < MP_VALUE_LIST(compare_val)->size; i2++) {
				value2 = MP_VALUE_LIST(compare_val)->v_list[i2];

				if (mp_value_compare(value1, value2) == MP_VALUE_EQUAL) {
					mp_value_t dup = mp_value_duplicate(value2);
					if (dup == NULL) {
						LOG_ERR("Failed to allocate intersection value");
						goto error;
					}

					mp_value_list_set(intersect_list, o++, dup);
					break;
				}
			}
			break;
		default:
			break;
		}

		if (intersect_value != NULL) {
			mp_value_list_set(intersect_list, o++, intersect_value);
		}
	}

	if (o == 0) {
		LOG_WRN("No intersection between %p and %p", list, compare_val);
		goto error;
	}

	mp_value_list_set_size(intersect_list, o);

	return intersect_list;

error:
	mp_value_destroy(intersect_list);
	return NULL;
}

mp_value_t mp_value_intersect(const mp_value_t val1, const mp_value_t val2)
{
	mp_value_t ref_val, compare_val;
	mp_value_t intersect_val = NULL;

	/* Missing values are matching anything */
	if (val1 == NULL && val2 != NULL) {
		return mp_value_ref(val2);
	}
	if (val1 != NULL && val2 == NULL) {
		return mp_value_ref(val1);
	}

	/* Check if intersect */
	if (!mp_value_can_intersect(val1, val2)) {
		return NULL;
	}

	/* When two values don't have the same type */
	if (mp_value_get_type(val1) >= mp_value_get_type(val2)) {
		ref_val = val1;
		compare_val = val2;
	} else {
		ref_val = val2;
		compare_val = val1;
	}

	if (mp_value_is_primitive(ref_val)) {
		if (mp_value_compare(val1, val2) == MP_VALUE_EQUAL) {
			intersect_val = mp_value_duplicate(val1);
		}
	} else {
		switch (mp_value_get_type(ref_val)) {
		case MP_TYPE_RANGE:
			intersect_val = mp_value_intersect_range(ref_val, compare_val);
			break;
		case MP_TYPE_LIST:
			intersect_val = mp_value_intersect_list(ref_val, compare_val);
			break;
		default:
			break;
		}
	}

	return intersect_val;
}

static inline void mp_value_print_int(const mp_value_t value)
{
	printk("%lld", mp_value_get_int(value));
}

static inline void mp_value_print_string(const mp_value_t value)
{
	printk("%s", mp_value_get_string(value));
}

static inline void mp_value_print_range(const mp_value_t value)
{
	printk("[%lld, %lld, %lld]",
	       mp_value_get_range_min(value),
	       mp_value_get_range_max(value),
	       mp_value_get_range_step(value));
}

void mp_value_print(const mp_value_t value, bool new_line);

static inline void mp_value_print_list(const mp_value_t value)
{
	printk("{");
	for (size_t i = 0; i < MP_VALUE_LIST_CONST(value)->size; i++) {
		if (i > 0) {
			printk(", ");
		}
		mp_value_print(MP_VALUE_LIST_CONST(value)->v_list[i], false);
	}
	printk("}");
}

void mp_value_print(const mp_value_t value, bool new_line)
{
	typedef void (*mp_value_print_fn)(const mp_value_t);
	static const mp_value_print_fn mp_value_print_table[MP_TYPE_COUNT] = {
		[MP_TYPE_NONE] = NULL,
		[MP_TYPE_BOOLEAN] = mp_value_print_int,
		[MP_TYPE_ENUM] = mp_value_print_int,
		[MP_TYPE_INT] = mp_value_print_int,
		[MP_TYPE_RANGE] = mp_value_print_range,
		[MP_TYPE_STRING] = mp_value_print_string,
		[MP_TYPE_LIST] = mp_value_print_list,
		[MP_TYPE_OBJECT] = NULL,
		[MP_TYPE_PTR] = NULL,
	};

	if (MP_VALUE_IS_NULL(value) || !MP_VALUE_IS_VALID(value) ||
	    mp_value_print_table[mp_value_get_type(value)] == NULL) {
		LOG_ERR("Invalid mp_value %p to print, type %u", value, mp_value_get_type(value));
		return;
	}

	mp_value_print_fn print_fn = mp_value_print_table[mp_value_get_type(value)];

	if (print_fn != NULL) {
		print_fn(value);
	}

	if (new_line) {
		printk("\n");
	}
}
