/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mp/mp_value.h>

LOG_MODULE_REGISTER(mp_value, CONFIG_MP_LOG_LEVEL);

#define MP_VALUE(value)                      ((struct mp_value *)value)
#define MP_VALUE_SIMPLE(value)               ((struct mp_value_simple *)value)
#define MP_VALUE_RANGE(value)                ((struct mp_value_range *)value)
#define MP_VALUE_LIST(value)                 ((struct mp_value_list *)value)
#define MP_VALUE_CONST(value)                ((const struct mp_value *)value)
#define MP_VALUE_SIMPLE_CONST(value)         ((const struct mp_value_simple *)value)
#define MP_VALUE_RANGE_CONST(value)          ((const struct mp_value_range *)value)
#define MP_VALUE_LIST_CONST(value)           ((const struct mp_value_list *)value)

#define mp_compare(a, b)                                                                           \
	({                                                                                         \
		__typeof__(a) _a = (a);                                                            \
		__typeof__(b) _b = (b);                                                            \
		(_a < _b) ? MP_VALUE_LESS_THAN                                                     \
			  : ((_a > _b) ? MP_VALUE_GREATER_THAN : MP_VALUE_EQUAL);                  \
	})

#define MP_VALUE_RANGES_OVERLAP(ref_val, cmp_val, vtype)                                           \
	!(MP_VALUE_RANGE(ref_val)->min.vtype > MP_VALUE_RANGE(cmp_val)->max.vtype ||               \
	  MP_VALUE_RANGE(cmp_val)->min.vtype > MP_VALUE_RANGE(ref_val)->max.vtype)

#define MP_VALUE_CREATE_INTERSECT_RANGE(ref_val, cmp_val, vtype, type_enum)                        \
	MP_VALUE_RANGES_OVERLAP(ref_val, cmp_val, vtype)                                           \
	? mp_value_new(                                                                            \
		  type_enum,                                                                       \
		  MAX(MP_VALUE_RANGE(ref_val)->min.vtype, MP_VALUE_RANGE(cmp_val)->min.vtype),     \
		  MIN(MP_VALUE_RANGE(ref_val)->max.vtype, MP_VALUE_RANGE(cmp_val)->max.vtype),     \
		  sys_gcd(MP_VALUE_RANGE(ref_val)->step.vtype,                                     \
			  MP_VALUE_RANGE(compare_val)->step.vtype),                                \
		  NULL)                                                                            \
	: NULL

#define MP_SINGLE_VALUE_IN_RANGE(ref_val, cmp_val, vtype)                                          \
	(IN_RANGE(MP_VALUE_SIMPLE(cmp_val)->vtype, MP_VALUE_RANGE(ref_val)->min.vtype,             \
		  MP_VALUE_RANGE(ref_val)->max.vtype))

struct mp_value_simple {
	struct mp_value base;
	union {
		bool v_boolean;
		int32_t v_int;
		uint32_t v_uint;
		const char *v_cstring;
		struct mp_object *v_obj;
		void *v_ptr;
	};
};

struct mp_value_list {
	struct mp_value base;
	sys_slist_t v_list;
};

struct mp_value_range {
	struct mp_value base;
	union {
		int32_t v_int;
		uint32_t v_uint;
	} min, max, step;
};

struct mp_value_node {
	struct mp_value *value;
	sys_snode_t node;
};

static const size_t mp_value_type_sizes[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = sizeof(struct mp_value_simple),
	[MP_TYPE_BOOLEAN] = sizeof(struct mp_value_simple),
	[MP_TYPE_ENUM] = sizeof(struct mp_value_simple),
	[MP_TYPE_INT] = sizeof(struct mp_value_simple),
	[MP_TYPE_UINT] = sizeof(struct mp_value_simple),
	[MP_TYPE_STRING] = sizeof(struct mp_value_simple),
	[MP_TYPE_INT_RANGE] = sizeof(struct mp_value_range),
	[MP_TYPE_UINT_RANGE] = sizeof(struct mp_value_range),
	[MP_TYPE_LIST] = sizeof(struct mp_value_list),
	[MP_TYPE_OBJECT] = sizeof(struct mp_value_simple),
	[MP_TYPE_PTR] = sizeof(struct mp_value_simple),
};

static const uint32_t mp_value_intersect_mask[MP_TYPE_COUNT] = {
	[MP_TYPE_NONE] = 0,
	[MP_TYPE_BOOLEAN] = BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_LIST),
	[MP_TYPE_ENUM] = BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_LIST),
	[MP_TYPE_INT] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_INT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_UINT] = BIT(MP_TYPE_UINT) | BIT(MP_TYPE_UINT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_STRING] = BIT(MP_TYPE_STRING) | BIT(MP_TYPE_LIST),
	[MP_TYPE_INT_RANGE] = BIT(MP_TYPE_INT) | BIT(MP_TYPE_INT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_UINT_RANGE] = BIT(MP_TYPE_UINT) | BIT(MP_TYPE_UINT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_LIST] = BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_INT) |
			 BIT(MP_TYPE_UINT) | BIT(MP_TYPE_STRING) | BIT(MP_TYPE_INT_RANGE) |
			 BIT(MP_TYPE_UINT_RANGE) | BIT(MP_TYPE_LIST),
	[MP_TYPE_OBJECT] = 0,
	[MP_TYPE_PTR] = 0,
};

bool mp_value_is_primitive(const struct mp_value *value)
{
	if (value == NULL || !IN_RANGE(value->type, MP_TYPE_NONE + 1, MP_TYPE_COUNT - 1)) {
		return false;
	}

	return ((BIT(MP_TYPE_BOOLEAN) | BIT(MP_TYPE_ENUM) | BIT(MP_TYPE_INT) | BIT(MP_TYPE_UINT) |
		 BIT(MP_TYPE_STRING)) &
		BIT(value->type)) != 0;
}

static int mp_value_set_range(struct mp_value *value, int type, va_list *args)
{
	if (value == NULL) {
		return -EINVAL;
	}

	value->type = type;
	MP_VALUE_RANGE(value)->min.v_uint = va_arg(*args, uint32_t);
	MP_VALUE_RANGE(value)->max.v_uint = va_arg(*args, uint32_t);
	MP_VALUE_RANGE(value)->step.v_uint = va_arg(*args, uint32_t);

	return 0;
}

static int mp_value_set_list(struct mp_value *value, va_list *args)
{
	struct mp_value *list_item;
	int ret;

	while ((list_item = va_arg(*args, struct mp_value *)) != NULL) {
		ret = mp_value_list_append(value, list_item);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int mp_value_set_va_list(struct mp_value *value, int type, va_list *args)
{
	if (value == NULL) {
		return -EINVAL;
	}

	value->type = type;
	switch (value->type) {
	case MP_TYPE_BOOLEAN:
	case MP_TYPE_ENUM:
	case MP_TYPE_INT:
		MP_VALUE_SIMPLE(value)->v_int = va_arg(*args, int);
		break;
	case MP_TYPE_STRING:
		MP_VALUE_SIMPLE(value)->v_cstring = va_arg(*args, const char *);
		break;
	case MP_TYPE_UINT:
		MP_VALUE_SIMPLE(value)->v_uint = va_arg(*args, uint32_t);
		break;
	case MP_TYPE_OBJECT:
		mp_object_replace(&MP_VALUE_SIMPLE(value)->v_obj,
				  va_arg(*args, struct mp_object *));
		break;
	case MP_TYPE_PTR:
		MP_VALUE_SIMPLE(value)->v_ptr = va_arg(*args, void *);
		break;
	case MP_TYPE_INT_RANGE:
	case MP_TYPE_UINT_RANGE:
		return mp_value_set_range(value, type, args);
	case MP_TYPE_LIST:
		return mp_value_set_list(value, args);
	default:
		LOG_ERR("Unknown mp_value type: %d", type);
		return -EINVAL;
	}

	return 0;
}

int mp_value_set(struct mp_value *value, int type, ...)
{
	va_list args;
	int ret;

	va_start(args, type);
	ret = mp_value_set_va_list(value, type, &args);
	va_end(args);

	return ret;
}

const char *mp_value_get_string(const struct mp_value *value)
{
	return MP_VALUE_SIMPLE_CONST(value)->v_cstring;
}

int mp_value_get_int(const struct mp_value *value)
{
	return MP_VALUE_SIMPLE_CONST(value)->v_int;
}

uint32_t mp_value_get_uint(const struct mp_value *value)
{
	return MP_VALUE_SIMPLE_CONST(value)->v_uint;
}

void *mp_value_get_ptr(const struct mp_value *value)
{
	return (value != NULL) ? MP_VALUE_SIMPLE_CONST(value)->v_ptr : NULL;
}

bool mp_value_get_boolean(const struct mp_value *value)
{
	return MP_VALUE_SIMPLE_CONST(value)->v_boolean;
}

struct mp_value *mp_value_new_empty(enum mp_value_type type)
{
	struct mp_value *value;

	if (type >= MP_TYPE_COUNT || type < MP_TYPE_NONE) {
		LOG_ERR("Invalid value type: %d", type);
		return NULL;
	}

	value = k_calloc(1, mp_value_type_sizes[type]);
	if (value == NULL) {
		LOG_ERR("Failed to allocate memory for mp_value type %d", type);
		return NULL;
	}

	value->type = type;
	if (value->type == MP_TYPE_LIST) {
		sys_slist_init(&MP_VALUE_LIST(value)->v_list);
	}

	return value;
}

int mp_value_destroy(struct mp_value *value)
{
	struct mp_value_node *value_node;
	sys_snode_t *node;

	if (value == NULL) {
		return -EINVAL;
	}

	if (value->type == MP_TYPE_LIST) {
		while (!sys_slist_is_empty(&MP_VALUE_LIST(value)->v_list)) {
			node = sys_slist_get(&MP_VALUE_LIST(value)->v_list);
			if (node == NULL) {
				k_free(value);
				return -EIO;
			}

			value_node = CONTAINER_OF(node, struct mp_value_node, node);
			mp_value_destroy(value_node->value);
			k_free(value_node);
		}
	}

	if (value->type == MP_TYPE_OBJECT) {
		mp_object_unref(MP_VALUE_SIMPLE(value)->v_obj);
	}

	k_free(value);

	return 0;
}

struct mp_value *mp_value_new(enum mp_value_type type, ...)
{

	struct mp_value *value;
	va_list args;

	va_start(args, type);

	value = mp_value_new_va_list(type, &args);

	va_end(args);

	return value;
}

struct mp_value *mp_value_new_va_list(enum mp_value_type type, va_list *args)
{
	int ret;
	struct mp_value *value = mp_value_new_empty(type);

	if (value == NULL) {
		return NULL;
	}

	ret = mp_value_set_va_list(value, type, args);
	if (ret < 0) {
		LOG_ERR("Failed to set mp_value type %d: %d", type, ret);
		mp_value_destroy(value);
		return NULL;
	}

	return value;
}

static int mp_value_copy(struct mp_value *dst, const struct mp_value *src)
{
	int ret;

	if (dst == NULL || src == NULL) {
		return -EINVAL;
	}

	if (src->type == MP_TYPE_LIST) {
		struct mp_value_node *v_node;

		SYS_SLIST_FOR_EACH_CONTAINER(&MP_VALUE_LIST(src)->v_list, v_node, node) {
			struct mp_value *dup = mp_value_duplicate(v_node->value);

			if (dup == NULL) {
				return -ENOMEM;
			}

			ret = mp_value_list_append(dst, dup);
			if (ret < 0) {
				mp_value_destroy(dup);
				return ret;
			}
		}
	} else if (src->type == MP_TYPE_OBJECT) {
		MP_VALUE_SIMPLE(dst)->v_obj = MP_VALUE_SIMPLE(src)->v_obj;
		mp_object_ref(MP_VALUE_SIMPLE(dst)->v_obj);
	} else {
		memcpy(dst, src, mp_value_type_sizes[src->type]);
	}

	return 0;
}

struct mp_value *mp_value_duplicate(const struct mp_value *value)
{
	struct mp_value *dup_value;
	int ret;

	if (value == NULL) {
		return NULL;
	}

	dup_value = mp_value_new_empty(value->type);
	if (dup_value == NULL) {
		return NULL;
	}

	ret = mp_value_copy(dup_value, value);
	if (ret < 0) {
		LOG_ERR("Failed to copy mp_value: %d", ret);
		mp_value_destroy(dup_value);
		return NULL;
	}

	return dup_value;
}

int mp_value_list_append(struct mp_value *list, struct mp_value *append_value)
{
	struct mp_value_node *node;

	if (list == NULL || append_value == NULL) {
		return -EINVAL;
	}

	node = k_malloc(sizeof(struct mp_value_node));
	if (node == NULL) {
		return -ENOMEM;
	}

	node->value = append_value;
	sys_slist_append(&MP_VALUE_LIST(list)->v_list, &node->node);

	return 0;
}

struct mp_value *mp_value_list_get(const struct mp_value *list, int index)
{
	sys_snode_t *node;
	struct mp_value_node *value_node = NULL;
	int count = 0;

	SYS_SLIST_FOR_EACH_NODE((sys_slist_t *)&MP_VALUE_LIST(list)->v_list, node) {
		if (count++ == index) {
			value_node = CONTAINER_OF(node, struct mp_value_node, node);
			break;
		}
	}

	return (value_node != NULL) ? value_node->value : NULL;
}

bool mp_value_list_is_empty(const struct mp_value *list)
{
	return sys_slist_is_empty(&MP_VALUE_LIST_CONST(list)->v_list);
}

size_t mp_value_list_get_size(const struct mp_value *list)
{
	return sys_slist_len(&MP_VALUE_LIST_CONST(list)->v_list);
}

int mp_value_get_int_range_min(const struct mp_value *range)
{
	return MP_VALUE_RANGE_CONST(range)->min.v_int;
}

int mp_value_get_int_range_max(const struct mp_value *range)
{
	return MP_VALUE_RANGE_CONST(range)->max.v_int;
}

int mp_value_get_int_range_step(const struct mp_value *range)
{
	return MP_VALUE_RANGE_CONST(range)->step.v_int;
}

uint32_t mp_value_get_uint_range_min(const struct mp_value *range)
{
	return MP_VALUE_RANGE_CONST(range)->min.v_uint;
}

uint32_t mp_value_get_uint_range_max(const struct mp_value *range)
{
	return MP_VALUE_RANGE_CONST(range)->max.v_uint;
}

uint32_t mp_value_get_uint_range_step(const struct mp_value *range)
{
	return MP_VALUE_RANGE_CONST(range)->step.v_uint;
}

struct mp_object *mp_value_get_object(struct mp_value *value)
{
	return (value != NULL) ? MP_VALUE_SIMPLE_CONST(value)->v_obj : NULL;
}

static int mp_value_list_compare(const struct mp_value *list1, const struct mp_value *list2);

int mp_value_compare(const struct mp_value *val1, const struct mp_value *val2)
{
	bool is_equal;

	if (val1 == NULL || val2 == NULL) {
		return MP_VALUE_COMPARE_FAILED;
	}

	if (val1->type != val2->type) {
		return MP_VALUE_COMPARE_FAILED;
	}

	switch (val1->type) {
	case MP_TYPE_BOOLEAN:
	case MP_TYPE_ENUM:
		return MP_VALUE_SIMPLE_CONST(val1)->v_uint == MP_VALUE_SIMPLE_CONST(val2)->v_uint
			       ? MP_VALUE_EQUAL
			       : MP_VALUE_UNORDERED;
	case MP_TYPE_INT:
		return mp_compare(MP_VALUE_SIMPLE_CONST(val1)->v_int,
				  MP_VALUE_SIMPLE_CONST(val2)->v_int);
	case MP_TYPE_UINT:
		return mp_compare(MP_VALUE_SIMPLE_CONST(val1)->v_uint,
				  MP_VALUE_SIMPLE_CONST(val2)->v_uint);
	case MP_TYPE_STRING:
		return strcmp(MP_VALUE_SIMPLE_CONST(val1)->v_cstring,
			      MP_VALUE_SIMPLE_CONST(val2)->v_cstring) == 0
			       ? MP_VALUE_EQUAL
			       : MP_VALUE_UNORDERED;
	case MP_TYPE_UINT_RANGE:
	case MP_TYPE_INT_RANGE:
		is_equal = (MP_VALUE_RANGE_CONST(val1)->min.v_uint ==
				    MP_VALUE_RANGE_CONST(val2)->min.v_uint &&
			    MP_VALUE_RANGE_CONST(val1)->max.v_uint ==
				    MP_VALUE_RANGE_CONST(val2)->max.v_uint &&
			    MP_VALUE_RANGE_CONST(val1)->step.v_uint ==
				    MP_VALUE_RANGE_CONST(val2)->step.v_uint);

		return is_equal ? MP_VALUE_EQUAL : MP_VALUE_UNORDERED;
	case MP_TYPE_LIST:
		return mp_value_list_compare(val1, val2);
	default:
		return MP_VALUE_COMPARE_FAILED;
	}
}

static int mp_value_list_compare(const struct mp_value *list1, const struct mp_value *list2)
{
	int size1 = mp_value_list_get_size(list1);
	int size2 = mp_value_list_get_size(list2);
	int count_matched = 0;
	struct mp_value_node *v_node1, *v_node2;

	if (list1->type != MP_TYPE_LIST || list2->type != MP_TYPE_LIST) {
		return MP_VALUE_COMPARE_FAILED;
	}

	if (size1 != size2) {
		return MP_VALUE_UNORDERED;
	}

	SYS_SLIST_FOR_EACH_CONTAINER((sys_slist_t *)&MP_VALUE_LIST(list1)->v_list, v_node1, node) {
		SYS_SLIST_FOR_EACH_CONTAINER((sys_slist_t *)&MP_VALUE_LIST(list2)->v_list, v_node2,
					     node) {
			if (mp_value_compare(v_node1->value, v_node2->value) == MP_VALUE_EQUAL) {
				count_matched++;
			}
		}
	}

	return count_matched == size1 ? MP_VALUE_EQUAL : MP_VALUE_UNORDERED;
}

struct mp_value *mp_value_intersect_int_range(const struct mp_value *ref_val,
					      const struct mp_value *compare_val)
{
	struct mp_value *intersect_value;

	if (compare_val->type == MP_TYPE_INT_RANGE && ref_val->type == MP_TYPE_INT_RANGE) {
		intersect_value = MP_VALUE_CREATE_INTERSECT_RANGE(ref_val, compare_val, v_int,
								  MP_TYPE_INT_RANGE);
	} else if (compare_val->type == MP_TYPE_UINT_RANGE && ref_val->type == MP_TYPE_UINT_RANGE) {
		intersect_value = MP_VALUE_CREATE_INTERSECT_RANGE(ref_val, compare_val, v_uint,
								  MP_TYPE_UINT_RANGE);
	} else if ((ref_val->type == MP_TYPE_INT_RANGE && compare_val->type == MP_TYPE_INT &&
		    MP_SINGLE_VALUE_IN_RANGE(ref_val, compare_val, v_int)) ||
		   (ref_val->type == MP_TYPE_UINT_RANGE && compare_val->type == MP_TYPE_UINT &&
		    MP_SINGLE_VALUE_IN_RANGE(ref_val, compare_val, v_uint))) {
		intersect_value =
			mp_value_new(compare_val->type, MP_VALUE_SIMPLE(compare_val)->v_uint, NULL);
	} else {
		intersect_value = NULL;
	}

	return intersect_value;
}

struct mp_value *mp_value_intersect_list(const struct mp_value *list,
					 const struct mp_value *compare_val)
{
	struct mp_value *intersect_value = NULL;
	struct mp_value *intersect_list = NULL;
	struct mp_value_node *v_node1, *v_node2;

	if (list == NULL || compare_val == NULL || compare_val->type == MP_TYPE_NONE) {
		return NULL;
	}

	SYS_SLIST_FOR_EACH_CONTAINER((sys_slist_t *)&MP_VALUE_LIST(list)->v_list, v_node1, node) {
		intersect_value = NULL;
		switch (compare_val->type) {
		case MP_TYPE_BOOLEAN:
		case MP_TYPE_ENUM:
		case MP_TYPE_INT:
		case MP_TYPE_UINT:
		case MP_TYPE_STRING:
			if (mp_value_compare(compare_val, v_node1->value) == MP_VALUE_EQUAL) {
				intersect_value = mp_value_duplicate(compare_val);
			}
			break;
		case MP_TYPE_INT_RANGE:
		case MP_TYPE_UINT_RANGE:
			intersect_value = mp_value_intersect_int_range(compare_val, v_node1->value);
			break;
		case MP_TYPE_LIST:
			SYS_SLIST_FOR_EACH_CONTAINER(
				(sys_slist_t *)&MP_VALUE_LIST(compare_val)->v_list, v_node2, node) {
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

static bool mp_value_int_range_can_intersect(const struct mp_value *ref_val,
					  const struct mp_value *compare_val)
{
	if (ref_val->type == MP_TYPE_INT_RANGE && compare_val->type == MP_TYPE_INT_RANGE) {
		return MP_VALUE_RANGES_OVERLAP(ref_val, compare_val, v_int);
	}

	if (ref_val->type == MP_TYPE_UINT_RANGE && compare_val->type == MP_TYPE_UINT_RANGE) {
		return MP_VALUE_RANGES_OVERLAP(ref_val, compare_val, v_uint);
	}

	if (ref_val->type == MP_TYPE_INT_RANGE && compare_val->type == MP_TYPE_INT) {
		return MP_SINGLE_VALUE_IN_RANGE(ref_val, compare_val, v_int);
	}

	if (ref_val->type == MP_TYPE_UINT_RANGE && compare_val->type == MP_TYPE_UINT) {
		return MP_SINGLE_VALUE_IN_RANGE(ref_val, compare_val, v_uint);
	}

	return false;
}

static bool mp_value_list_can_intersect(const struct mp_value *list,
				     const struct mp_value *compare_val)
{
	struct mp_value_node *v_node1, *v_node2;

	SYS_SLIST_FOR_EACH_CONTAINER((sys_slist_t *)&MP_VALUE_LIST(list)->v_list, v_node1, node) {
		if (compare_val->type != MP_TYPE_LIST) {
			if (mp_value_can_intersect(v_node1->value, compare_val)) {
				return true;
			}
			continue;
		}

		SYS_SLIST_FOR_EACH_CONTAINER((sys_slist_t *)&MP_VALUE_LIST(compare_val)->v_list,
					     v_node2, node) {
			if (mp_value_compare(v_node1->value, v_node2->value) == MP_VALUE_EQUAL) {
				return true;
			}
		}
	}

	return false;
}

bool mp_value_can_intersect(const struct mp_value *val1, const struct mp_value *val2)
{
	const struct mp_value *ref_val, *compare_val;

	if (val1 == NULL || val2 == NULL ||
	    !IN_RANGE(val1->type, MP_TYPE_NONE, MP_TYPE_COUNT - 1) ||
	    (mp_value_intersect_mask[val1->type] & BIT(val2->type)) == 0) {
		return false;
	}

	/*
	 * Same ordering rule as mp_value_intersect(): a container type always has a
	 * higher ordinal than the scalar type it can contain, so the greater of the
	 * two is the one to dispatch on.
	 */
	if (val1->type >= val2->type) {
		ref_val = val1;
		compare_val = val2;
	} else {
		ref_val = val2;
		compare_val = val1;
	}

	if (mp_value_is_primitive(ref_val)) {
		return mp_value_compare(val1, val2) == MP_VALUE_EQUAL;
	}

	switch (ref_val->type) {
	case MP_TYPE_INT_RANGE:
	case MP_TYPE_UINT_RANGE:
		return mp_value_int_range_can_intersect(ref_val, compare_val);
	case MP_TYPE_LIST:
		return mp_value_list_can_intersect(ref_val, compare_val);
	default:
		return false;
	}
}

struct mp_value *mp_value_intersect(const struct mp_value *val1, const struct mp_value *val2)
{
	const struct mp_value *ref_val, *compare_val;
	struct mp_value *intersect_val = NULL;

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
		case MP_TYPE_UINT_RANGE:
			intersect_val = mp_value_intersect_int_range(ref_val, compare_val);
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

static inline void mp_value_print_int(const struct mp_value *value)
{
	printk("%d", MP_VALUE_SIMPLE_CONST(value)->v_int);
}

static inline void mp_value_print_uint(const struct mp_value *value)
{
	printk("%u", MP_VALUE_SIMPLE_CONST(value)->v_uint);
}

static inline void mp_value_print_string(const struct mp_value *value)
{
	printk("%s", MP_VALUE_SIMPLE_CONST(value)->v_cstring);
}

static inline void mp_value_print_int_range(const struct mp_value *value)
{
	printk("[%d, %d, %d]", MP_VALUE_RANGE_CONST(value)->min.v_int,
	       MP_VALUE_RANGE_CONST(value)->max.v_int, MP_VALUE_RANGE_CONST(value)->step.v_int);
}

static inline void mp_value_print_uint_range(const struct mp_value *value)
{
	printk("[%u, %u, %u]", MP_VALUE_RANGE_CONST(value)->min.v_uint,
	       MP_VALUE_RANGE_CONST(value)->max.v_uint, MP_VALUE_RANGE_CONST(value)->step.v_uint);
}

static inline void mp_value_print_list(const struct mp_value *value)
{
	struct mp_value_node *value_node;

	printk("{");
	SYS_SLIST_FOR_EACH_CONTAINER((sys_slist_t *)&MP_VALUE_LIST(value)->v_list, value_node,
				     node) {
		mp_value_print(value_node->value, false);
		if (sys_slist_peek_next(&value_node->node) != NULL) {

			printk(", ");
		}
	}
	printk("}");
}

void mp_value_print(const struct mp_value *value, bool new_line)
{
	typedef void (*mp_value_print_fn)(const struct mp_value *);
	static const mp_value_print_fn mp_value_print_table[MP_TYPE_COUNT] = {
		[MP_TYPE_NONE] = NULL,
		[MP_TYPE_BOOLEAN] = mp_value_print_int,
		[MP_TYPE_ENUM] = mp_value_print_int,
		[MP_TYPE_INT] = mp_value_print_int,
		[MP_TYPE_UINT] = mp_value_print_uint,
		[MP_TYPE_INT_RANGE] = mp_value_print_int_range,
		[MP_TYPE_UINT_RANGE] = mp_value_print_uint_range,
		[MP_TYPE_STRING] = mp_value_print_string,
		[MP_TYPE_LIST] = mp_value_print_list,
		[MP_TYPE_OBJECT] = NULL,
		[MP_TYPE_PTR] = NULL,
	};

	if (value == NULL || value->type >= ARRAY_SIZE(mp_value_print_table) ||
	    mp_value_print_table[value->type] == NULL) {
		LOG_ERR("Invalid mp_value to print");
		return;
	}

	mp_value_print_fn print_fn = mp_value_print_table[value->type];

	if (print_fn != NULL) {
		print_fn(value);
	}

	if (new_line) {
		printk("\n");
	}
}
