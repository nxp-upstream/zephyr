/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "mp_value.h"

LOG_MODULE_REGISTER(mp_value, CONFIG_LIBMP_LOG_LEVEL);

#define DEFINE_COMPARE_SINGLE_VALUE_FUNC(typename, type)                                           \
	static int mp_value_compare_##typename(const MpValue *value1, const MpValue *value2)       \
	{                                                                                          \
		type val1 = ((const MpValueSimple *)value1)->data.v_##typename;                    \
		type val2 = ((const MpValueSimple *)value2)->data.v_##typename;                    \
		if (val1 > val2) {                                                                 \
			return MP_VALUE_GREATER_THAN;                                              \
		}                                                                                  \
                                                                                                   \
		if (val1 < val2) {                                                                 \
			return MP_VALUE_LESS_THAN;                                                 \
		}                                                                                  \
                                                                                                   \
		return MP_VALUE_EQUAL;                                                             \
	}

typedef struct {
	MpValue base;
	union {
		bool v_boolean;
		int32_t v_int;
		uint32_t v_uint;
		const char *v_cstring;
		MpObject *v_obj;
		void *v_ptr;
	} data;
} MpValueSimple;

typedef struct {
	MpValue base;
	sys_slist_t v_list;
} MpValueList;

typedef struct {
	MpValue base;
	union {
		int32_t v_int;
		uint32_t v_uint;
	} min, max, step;
} MpValueRange;

typedef struct {
	MpValue base;
	union {
		int32_t v_int;
		uint32_t v_uint;
	} num, denom;
} MpValueFraction;

typedef struct {
	MpValue base;
	MpValueFraction min, max, step;
} MpValueFractionRange;

typedef struct {
	MpValue *value;
	sys_snode_t node;
} MpValueNode;

static const size_t mp_value_type_sizes[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = sizeof(MpValueSimple),
	[MP_TYPE_BOOLEAN] = sizeof(MpValueSimple),
	[MP_TYPE_ENUM] = sizeof(MpValueSimple),
	[MP_TYPE_INT] = sizeof(MpValueSimple),
	[MP_TYPE_UINT] = sizeof(MpValueSimple),
	[MP_TYPE_FRACTION] = sizeof(MpValueFraction),
	[MP_TYPE_STRING] = sizeof(MpValueSimple),
	[MP_TYPE_INT_RANGE] = sizeof(MpValueRange),
	[MP_TYPE_FRACTION_RANGE] = sizeof(MpValueFractionRange),
	[MP_TYPE_LIST] = sizeof(MpValueList),
	[MP_TYPE_OBJECT] = sizeof(MpValueSimple),
	[MP_TYPE_PTR] = sizeof(MpValueSimple),
};

static const uint32_t mp_value_intersect_mask[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = 0,
	[MP_TYPE_BOOLEAN] = BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_LIST),
	[MP_TYPE_ENUM] = BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_LIST),
	[MP_TYPE_INT] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_INT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_UINT] = BIT(MP_TYPE_UINT) | BIT(MP_TYPE_LIST),
	[MP_TYPE_FRACTION] =
		BIT(MP_TYPE_FRACTION) | BIT(MP_TYPE_FRACTION_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_STRING] = BIT(MP_TYPE_STRING) | BIT(MP_TYPE_LIST),
	[MP_TYPE_INT_RANGE] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_INT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_FRACTION_RANGE] =
		BIT(MP_TYPE_FRACTION) | BIT(MP_TYPE_FRACTION_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_LIST] = BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_INT) |
			 BIT(MP_TYPE_UINT) | BIT(MP_TYPE_FRACTION) | BIT(MP_TYPE_STRING) |
			 BIT(MP_TYPE_INT_RANGE) | BIT(MP_TYPE_FRACTION_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_OBJECT] = 0,
	[MP_TYPE_PTR] = 0,
};

bool mp_value_is_primitive(const MpValue *value)
{
	if (value == NULL || !IN_RANGE(value->type, MP_TYPE_NONE + 1, MP_TYPE_COUNT - 1)) {
		return false;
	}

	return ((BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_INT) | BIT(MP_TYPE_UINT) |
		 BIT(MP_TYPE_FRACTION) | BIT(MP_TYPE_STRING)) &
		BIT(value->type)) != 0;
}

static void mp_value_set_fraction(MpValueFraction *frac, int num, int denom)
{
	frac->base.type = MP_TYPE_FRACTION;
	frac->num.v_int = num;
	frac->denom.v_int = denom;
}

static void mp_value_set_va_list(MpValue *value, int type, va_list *args)
{
	int num, denom;

	if (value == NULL) {
		return;
	}

	value->type = type;
	switch (value->type) {
	case MP_TYPE_BOOLEAN:
		((MpValueSimple *)value)->data.v_boolean = va_arg(*args, int);
		break;
	case MP_TYPE_INT:
		((MpValueSimple *)value)->data.v_int = va_arg(*args, int);
		break;
	case MP_TYPE_STRING:
		((MpValueSimple *)value)->data.v_cstring = va_arg(*args, const char *);
		break;
	case MP_TYPE_UINT:
		((MpValueSimple *)value)->data.v_uint = va_arg(*args, uint32_t);
		break;
	case MP_TYPE_PTR:
		((MpValueSimple *)value)->data.v_ptr = va_arg(*args, void *);
		break;
	case MP_TYPE_FRACTION:
		num = va_arg(*args, int);
		denom = va_arg(*args, int);
		mp_value_set_fraction(((MpValueFraction *)value), num, denom);
		break;
	case MP_TYPE_INT_RANGE:
		((MpValueRange *)value)->min.v_int = va_arg(*args, int);
		((MpValueRange *)value)->max.v_int = va_arg(*args, int);
		((MpValueRange *)value)->step.v_int = va_arg(*args, int);
		break;
	case MP_TYPE_FRACTION_RANGE:
		num = va_arg(*args, int);
		denom = va_arg(*args, int);
		mp_value_set_fraction(&((MpValueFractionRange *)value)->min, num, denom);
		num = va_arg(*args, int);
		denom = va_arg(*args, int);
		mp_value_set_fraction(&((MpValueFractionRange *)value)->max, num, denom);
		num = va_arg(*args, int);
		denom = va_arg(*args, int);
		mp_value_set_fraction(&((MpValueFractionRange *)value)->step, num, denom);
		break;
	case MP_TYPE_OBJECT:
		mp_object_replace(&((MpValueSimple *)value)->data.v_obj, va_arg(*args, MpObject *));
		break;
	case MP_TYPE_LIST:
		MpValue *list_item;

		while ((list_item = va_arg(*args, MpValue *)) != NULL) {
			mp_value_list_append(value, list_item);
		}
		break;
	default:
		break;
	}
}

void mp_value_set(MpValue *value, int type, ...)
{
	va_list args;

	va_start(args, type);
	mp_value_set_va_list(value, type, &args);
	va_end(args);
}

int mp_value_get_fraction_numerator(const MpValue *frac)
{
	return ((MpValueFraction *)frac)->num.v_int;
}

int mp_value_get_fraction_denominator(const MpValue *frac)
{
	return ((MpValueFraction *)frac)->denom.v_int;
}

const MpValue *mp_value_get_fraction_range_min(const MpValue *fraction_range)
{
	return (MpValue *)&((MpValueFractionRange *)fraction_range)->min;
}

const MpValue *mp_value_get_fraction_range_max(const MpValue *fraction_range)
{
	return (MpValue *)&((MpValueFractionRange *)fraction_range)->max;
}

const MpValue *mp_value_get_fraction_range_step(const MpValue *fraction_range)
{
	return (MpValue *)&((MpValueFractionRange *)fraction_range)->step;
}

const char *mp_value_get_string(const MpValue *value)
{
	return ((MpValueSimple *)value)->data.v_cstring;
}

int mp_value_get_int(const MpValue *value)
{
	return ((MpValueSimple *)value)->data.v_int;
}

uint32_t mp_value_get_uint(const MpValue *value)
{
	return ((MpValueSimple *)value)->data.v_uint;
}

void *mp_value_get_ptr(const MpValue *value)
{
	return value ? ((MpValueSimple *)value)->data.v_ptr : NULL;
}

bool mp_value_get_boolean(const MpValue *value)
{
	return ((MpValueSimple *)value)->data.v_boolean;
}

MpValue *mp_value_new_empty(MpValueType type)
{
	MpValue *value;

	if (type >= MP_TYPE_COUNT || type < MP_TYPE_NONE) {
		LOG_ERR("Invalid value type: %d", type);
		return NULL;
	}

	value = k_calloc(1, mp_value_type_sizes[type]);
	if (value == NULL) {
		LOG_ERR("Failed to allocate memory for MpValue type %d", type);
		return NULL;
	}

	value->type = type;
	if (value->type == MP_TYPE_LIST) {
		sys_slist_init(&((MpValueList *)value)->v_list);
	}

	return value;
}

void mp_value_destroy(MpValue *value)
{
	if (value->type == MP_TYPE_LIST) {
		while (!sys_slist_is_empty(&((MpValueList *)value)->v_list)) {
			MpValueNode *valueNode = CONTAINER_OF(
				sys_slist_get(&((MpValueList *)value)->v_list), MpValueNode, node);
			mp_value_destroy(valueNode->value);
			k_free(valueNode);
		}
	}

	if (value->type == MP_TYPE_OBJECT) {
		mp_object_unref(((MpValueSimple *)value)->data.v_obj);
	}

	k_free(value);
}

MpValue *mp_value_new(MpValueType type, ...)
{

	MpValue *value;
	va_list args;

	va_start(args, type);

	value = mp_value_new_va_list(type, &args);

	va_end(args);

	return value;
}

MpValue *mp_value_new_va_list(MpValueType type, va_list *args)
{
	MpValue *value = mp_value_new_empty(type);

	mp_value_set_va_list(value, type, args);

	return value;
}

MpValue *mp_value_duplicate(const MpValue *value)
{
	MpValue *dup_value = mp_value_new_empty(value->type);
	MpValueNode *v_node;

	if (dup_value == NULL) {
		return NULL;
	}

	if (value->type == MP_TYPE_LIST) {
		SYS_SLIST_FOR_EACH_CONTAINER(&((MpValueList *)value)->v_list, v_node, node) {
			mp_value_list_append(dup_value, mp_value_duplicate(v_node->value));
		}
	} else if (value->type == MP_TYPE_OBJECT) {
		((MpValueSimple *)dup_value)->data.v_obj =
			mp_object_ref(((MpValueSimple *)value)->data.v_obj);
	} else {
		memcpy(dup_value, value, mp_value_type_sizes[value->type]);
	}

	return dup_value;
}

void mp_value_list_append(MpValue *list, MpValue *append_value)
{
	MpValueNode *node;

	__ASSERT_NO_MSG(append_value != NULL && list != NULL);
	node = k_malloc(sizeof(MpValueNode));
	__ASSERT_NO_MSG(node != NULL);
	node->value = append_value;
	sys_slist_append(&((MpValueList *)list)->v_list, &node->node);
}

MpValue *mp_value_list_get(const MpValue *list, int index)
{
	sys_snode_t *node;
	MpValueNode *valueNode = NULL;
	int count = 0;

	SYS_SLIST_FOR_EACH_NODE(&((MpValueList *)list)->v_list, node) {
		if (count++ == index) {
			valueNode = CONTAINER_OF(node, MpValueNode, node);
			break;
		}
	}

	return valueNode ? valueNode->value : NULL;
}

bool mp_value_list_is_empty(const MpValue *list)
{
	return sys_slist_is_empty(&((MpValueList *)list)->v_list);
}

size_t mp_value_list_get_size(const MpValue *list)
{
	return sys_slist_len(&((MpValueList *)list)->v_list);
}

int mp_value_get_int_range_min(const MpValue *range)
{
	return ((MpValueRange *)range)->min.v_int;
}

int mp_value_get_int_range_max(const MpValue *range)
{
	return ((MpValueRange *)range)->max.v_int;
}

int mp_value_get_int_range_step(const MpValue *range)
{
	return ((MpValueRange *)range)->step.v_int;
}

MpObject *mp_value_get_object(MpValue *value)
{
	return value ? ((MpValueSimple *)value)->data.v_obj : NULL;
}

DEFINE_COMPARE_SINGLE_VALUE_FUNC(int, int);
DEFINE_COMPARE_SINGLE_VALUE_FUNC(uint, uint32_t);

int mp_value_compare_fraction(const MpValue *frac1, const MpValue *frac2)
{
	int n1 = mp_value_get_fraction_numerator(frac1);
	int d1 = mp_value_get_fraction_denominator(frac1);
	int n2 = mp_value_get_fraction_numerator(frac2);
	int d2 = mp_value_get_fraction_denominator(frac2);
	int gcd1 = gcd(n1, d1);
	int gcd2 = gcd(n2, d2);

	n1 /= gcd1;
	d1 /= gcd1;

	n2 /= gcd2;
	d2 /= gcd2;

	if (n1 * d2 > n2 * d1) {
		return MP_VALUE_GREATER_THAN;
	} else if (n1 * d2 < n2 * d1) {
		return MP_VALUE_LESS_THAN;
	}

	return MP_VALUE_EQUAL;
}

static int mp_value_list_compare(const MpValue *list1, const MpValue *list2);

int mp_value_compare(const MpValue *val1, const MpValue *val2)
{
	bool is_equal;

	if (val1->type != val2->type) {
		return MP_VALUE_COMPARE_FAILED;
	}

	switch (val1->type) {
	case MP_TYPE_BOOLEAN:
		if (mp_value_get_boolean(val1) == mp_value_get_boolean(val2)) {
			return MP_VALUE_EQUAL;
		}
		return MP_VALUE_UNORDERED;
	case MP_TYPE_ENUM:
	case MP_TYPE_INT:
		return mp_value_compare_int(val1, val2);
	case MP_TYPE_UINT:
		return mp_value_compare_uint(val1, val2);
	case MP_TYPE_FRACTION:
		return mp_value_compare_fraction(val1, val2);
	case MP_TYPE_STRING:
		return strcmp(mp_value_get_string(val1), mp_value_get_string(val2));
	case MP_TYPE_INT_RANGE:
		is_equal = (mp_value_get_int_range_min(val1) == mp_value_get_int_range_min(val2) &&
			    mp_value_get_int_range_max(val1) == mp_value_get_int_range_max(val2) &&
			    mp_value_get_int_range_step(val1) == mp_value_get_int_range_step(val2));

		return is_equal ? MP_VALUE_EQUAL : MP_VALUE_UNORDERED;
	case MP_TYPE_FRACTION_RANGE:
		is_equal = mp_value_compare_fraction(mp_value_get_fraction_range_min(val1),
						     mp_value_get_fraction_range_min(val2)) ==
				   MP_VALUE_EQUAL &&
			   mp_value_compare_fraction(mp_value_get_fraction_range_max(val1),
						     mp_value_get_fraction_range_max(val2)) ==
				   MP_VALUE_EQUAL &&
			   mp_value_compare_fraction(mp_value_get_fraction_range_step(val1),
						     mp_value_get_fraction_range_step(val2)) ==
				   MP_VALUE_EQUAL;
		return is_equal ? MP_VALUE_EQUAL : MP_VALUE_UNORDERED;
	case MP_TYPE_LIST:
		return mp_value_list_compare(val1, val2);
	default:
		return MP_VALUE_COMPARE_FAILED;
	}
}

static int mp_value_list_compare(const MpValue *list1, const MpValue *list2)
{
	int size1 = mp_value_list_get_size(list1);
	int size2 = mp_value_list_get_size(list1);
	int count_matched = 0;
	MpValueNode *v_node1, *v_node2;

	if (list1->type != MP_TYPE_LIST || list2->type != MP_TYPE_LIST) {
		return MP_VALUE_COMPARE_FAILED;
	}

	if (size1 != size2) {
		return MP_VALUE_UNORDERED;
	}

	SYS_SLIST_FOR_EACH_CONTAINER(&((MpValueList *)list1)->v_list, v_node1, node) {
		SYS_SLIST_FOR_EACH_CONTAINER(&((MpValueList *)list2)->v_list, v_node2, node) {
			if (mp_value_compare(v_node1->value, v_node2->value) == MP_VALUE_EQUAL) {
				count_matched++;
			}
		}
	}

	return count_matched == size1 ? MP_VALUE_EQUAL : MP_VALUE_UNORDERED;
}

bool mp_value_can_intersect(const MpValue *val1, const MpValue *val2)
{
	if (val1 == NULL || val2 == NULL ||
	    !IN_RANGE(val1->type, MP_TYPE_NONE, MP_TYPE_COUNT - 1)) {
		return false;
	}

	return (mp_value_intersect_mask[val1->type] & BIT(val2->type)) != 0;
}

MpValue *mp_value_intersect_int_range(const MpValue *ref_val, const MpValue *compare_val)
{
	int r1_min, r1_max, r2_min, r2_max, r1_step, r2_step;
	MpValue *intersect_value = NULL;

	r1_min = mp_value_get_int_range_min(ref_val);
	r1_max = mp_value_get_int_range_max(ref_val);
	r1_step = mp_value_get_int_range_step(ref_val);
	if (compare_val->type == MP_TYPE_INT_RANGE) {
		r2_min = mp_value_get_int_range_min(compare_val);
		r2_max = mp_value_get_int_range_max(compare_val);
		r2_step = mp_value_get_int_range_step(compare_val);
		if (r1_min > r2_max || r2_min > r1_max) {
			return NULL;
		}

		intersect_value = mp_value_new(MP_TYPE_INT_RANGE, MAX(r1_min, r2_min),
					       MIN(r1_max, r2_max), MIN(r1_step, r2_step), NULL);
	} else if (compare_val->type == MP_TYPE_INT) {
		if (IN_RANGE(mp_value_get_int(compare_val), r1_min, r1_max)) {
			intersect_value =
				mp_value_new(MP_TYPE_INT, mp_value_get_int(compare_val), NULL);
		}
	}

	return intersect_value;
}

/**
 * Find the min or max value between two primitive values
 * @param value1 the first value
 * @param value2 the second value
 * @param find_min true if find min, false if find max
 * @return the min or max value between two values
 */
static const MpValue *mp_value_min_max(const MpValue *value1, const MpValue *value2, bool find_min)
{
	switch (mp_value_compare(value1, value2)) {
	case MP_VALUE_LESS_THAN:
		return find_min ? value1 : value2;
	case MP_VALUE_EQUAL:
		return value1;
	case MP_VALUE_GREATER_THAN:
		return find_min ? value2 : value1;
	default:
		return NULL;
	}
}

MpValue *mp_value_intersect_fraction_range(const MpValue *ref_val, const MpValue *compare_val)
{
	const MpValue *frac_min1, *frac_max1, *frac_min2, *frac_max2;
	MpValue *intersect_value = NULL;

	frac_min1 = mp_value_get_fraction_range_min(ref_val);
	frac_max1 = mp_value_get_fraction_range_max(ref_val);
	if (compare_val->type == MP_TYPE_FRACTION_RANGE) {
		frac_min2 = mp_value_get_fraction_range_min(compare_val);
		frac_max2 = mp_value_get_fraction_range_max(compare_val);
		if (mp_value_compare_fraction(frac_min1, frac_max2) == MP_VALUE_GREATER_THAN ||
		    mp_value_compare_fraction(frac_max1, frac_min2) == MP_VALUE_LESS_THAN) {
			return NULL;
		}

		intersect_value = mp_value_new(MP_TYPE_FRACTION_RANGE, NULL);
		((MpValueFractionRange *)intersect_value)->min =
			*(MpValueFraction *)mp_value_min_max(frac_min1, frac_min2, false);
		((MpValueFractionRange *)intersect_value)->max =
			*(MpValueFraction *)mp_value_min_max(frac_max1, frac_max2, true);
		mp_value_set_fraction(&((MpValueFractionRange *)intersect_value)->step, 1, 1);
	} else if (compare_val->type == MP_TYPE_FRACTION &&
		   mp_value_compare_fraction(frac_min1, compare_val) == MP_VALUE_LESS_THAN &&
		   mp_value_compare_fraction(frac_max1, compare_val) == MP_VALUE_GREATER_THAN) {
		intersect_value =
			mp_value_new(MP_TYPE_FRACTION, mp_value_get_fraction_numerator(compare_val),
				     mp_value_get_fraction_denominator(compare_val), NULL);
	}

	return intersect_value;
}

MpValue *mp_value_intersect_list(const MpValue *list, const MpValue *compare_val)
{
	MpValue *intersect_value = NULL;
	MpValue *intersect_list = NULL;
	MpValueNode *v_node1, *v_node2;

	if (list == NULL || compare_val == NULL || compare_val->type == MP_TYPE_NONE) {
		return NULL;
	}

	SYS_SLIST_FOR_EACH_CONTAINER(&((MpValueList *)list)->v_list, v_node1, node) {
		intersect_value = NULL;
		switch (compare_val->type) {
		case MP_TYPE_BOOLEAN:
		case MP_TYPE_ENUM:
		case MP_TYPE_INT:
		case MP_TYPE_UINT:
		case MP_TYPE_FRACTION:
		case MP_TYPE_STRING:
			if (mp_value_compare(compare_val, v_node1->value) == MP_VALUE_EQUAL) {
				intersect_value = mp_value_duplicate(compare_val);
			}
			break;
		case MP_TYPE_INT_RANGE:
			intersect_value = mp_value_intersect_int_range(compare_val, v_node1->value);
			break;
		case MP_TYPE_FRACTION_RANGE:
			intersect_value =
				mp_value_intersect_fraction_range(compare_val, v_node1->value);
			break;
		case MP_TYPE_LIST:
			SYS_SLIST_FOR_EACH_CONTAINER(&((MpValueList *)compare_val)->v_list, v_node2,
						     node) {
				if (mp_value_compare(v_node1->value, v_node2->value) ==
				    MP_VALUE_EQUAL) {
					intersect_value = mp_value_duplicate(v_node2->value);
					break;
				}
			}
			break;
		default:
			break;
		}

		if (intersect_value != NULL) {
			if (intersect_list == NULL) {
				intersect_list = mp_value_new_empty(MP_TYPE_LIST);
			}
			mp_value_list_append(intersect_list, intersect_value);
		}
	}

	return intersect_list;
}

MpValue *mp_value_intersect(const MpValue *val1, const MpValue *val2)
{
	const MpValue *ref_val, *compare_val;
	MpValue *intersect_val = NULL;

	/* Check if intersect */
	if (!mp_value_can_intersect(val1, val2)) {
		return NULL;
	}

	/* When two values don't have the same type */
	if (val1->type >= val2->type) {
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
		switch (ref_val->type) {
		case MP_TYPE_INT_RANGE:
			intersect_val = mp_value_intersect_int_range(ref_val, compare_val);
			break;
		case MP_TYPE_FRACTION_RANGE:
			intersect_val = mp_value_intersect_fraction_range(ref_val, compare_val);
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

static inline void mp_value_print_int(const MpValue *value)
{
	printk("%d", mp_value_get_int(value));
}

static inline void mp_value_print_uint(const MpValue *value)
{
	printk("%u", mp_value_get_uint(value));
}

static inline void mp_value_print_string(const MpValue *value)
{
	printk("%s", mp_value_get_string(value));
}

static inline void mp_value_print_int_range(const MpValue *value)
{
	printk("[%d, %d, %d]", mp_value_get_int_range_min(value), mp_value_get_int_range_max(value),
	       mp_value_get_int_range_step(value));
}

static inline void mp_value_print_fraction(const MpValue *value)
{
	printk("%d/%d", mp_value_get_fraction_numerator(value),
	       mp_value_get_fraction_denominator(value));
}

static inline void mp_value_print_fraction_range(const MpValue *value)
{
	printk("[");
	mp_value_print_fraction(mp_value_get_fraction_range_min(value));
	printk(",");
	mp_value_print_fraction(mp_value_get_fraction_range_max(value));
	printk(",");
	mp_value_print_fraction(mp_value_get_fraction_range_step(value));
	printk("]");
}

static inline void mp_value_print_list(const MpValue *value)
{
	MpValueNode *valueNode;

	printk("{");
	SYS_SLIST_FOR_EACH_CONTAINER(&((MpValueList *)value)->v_list, valueNode, node) {
		mp_value_print(valueNode->value, false);
		if (sys_slist_peek_next(&valueNode->node) != NULL) {

			printk(", ");
		}
	}
	printk("}");
}

void mp_value_print(const MpValue *value, bool new_line)
{
	typedef void (*MpValuePrintFn)(const MpValue *value);
	static const MpValuePrintFn mp_value_print_table[MP_TYPE_COUNT] = {
		[MP_TYPE_NONE] = NULL,
		[MP_TYPE_BOOLEAN] = mp_value_print_int,
		[MP_TYPE_ENUM] = mp_value_print_int,
		[MP_TYPE_INT] = mp_value_print_int,
		[MP_TYPE_UINT] = mp_value_print_uint,
		[MP_TYPE_FRACTION] = mp_value_print_fraction,
		[MP_TYPE_INT_RANGE] = mp_value_print_int_range,
		[MP_TYPE_STRING] = mp_value_print_string,
		[MP_TYPE_LIST] = mp_value_print_list,
		[MP_TYPE_FRACTION_RANGE] = mp_value_print_fraction_range,
		[MP_TYPE_FRACTION_RANGE] = mp_value_print_fraction_range,
		[MP_TYPE_OBJECT] = NULL,
		[MP_TYPE_PTR] = NULL,
	};

	if (value == NULL || value->type >= ARRAY_SIZE(mp_value_print_table) ||
	    mp_value_print_table[value->type] == NULL) {
		LOG_ERR("Invalid MpValue to print");
		return;
	}

	MpValuePrintFn print_fn = mp_value_print_table[value->type];

	if (print_fn) {
		print_fn(value);
	}

	if (new_line) {
		printk("\n");
	}
}
