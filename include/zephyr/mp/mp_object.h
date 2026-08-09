/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_object.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_OBJECT_H_
#define ZEPHYR_INCLUDE_MP_MP_OBJECT_H_

/**
 * @defgroup mp_object Objects
 * @brief Base object APIs.
 * @ingroup mp_framework
 * @{
 */

#include <stdint.h>

#include <zephyr/sys/dlist.h>
#include <zephyr/sys/util_macro.h>

/** Base flag of the object */
#define MP_OBJECT_FLAG_BASE BIT(0)
/**
 * @brief The object is a @ref mp_bin and holds children of its own.
 *
 * Set by mp_bin_init(). This is to tell a walker that an element can be
 * descended into.
 */
#define MP_OBJECT_FLAG_BIN BIT(1)

/** Sentinel value to mark the end of property lists */
#define MP_PROP_LIST_END -1

/**
 * @brief Base object structure
 *
 * This structure defines the common fields and interface for all objects
 * in the MP object system: identity, list membership, and property access.
 */
struct mp_object {
	/** Parent element that contains this object */
	struct mp_object *container;
	/** Unique ID given to an object instance. The max value (UINT8_MAX) is reserved and
	 * should not be used
	 */
	uint8_t id;
	/** Flags of the object, bitfield inheritable */
	uint32_t flags;
	/** Object node to be used in a linked list */
	sys_dnode_t node;
	/** Function to set property */
	int (*set_property)(struct mp_object *self, uint32_t key, const void *val);
	/** Function to get property */
	int (*get_property)(struct mp_object *self, uint32_t key, void *val);
};

/**
 * @brief Initialize all fields of mp_object to zero.
 *
 * Must be called first when initializing any structure that embeds mp_object.
 *
 * @param obj Pointer to the mp_object to initialize.
 */
void mp_object_init(struct mp_object *obj);

/**
 * @brief Set multiple properties of an mp_object.
 *
 * This function sets one or more properties on the given object.
 * The arguments must be provided as a sequence of {key, value} pairs, terminated by
 * MP_PROP_LIST_END.
 *
 * Example usage:
 *
 * @code
 * mp_object_set_properties(obj, "key1", val1, "key2", val2, MP_PROP_LIST_END);
 * @endcode
 *
 * @param obj Pointer to a @ref mp_object.
 * @param ... A variable list of {uint32_t key, const void *val} pairs, terminated by
 * MP_PROP_LIST_END.
 *
 */
int mp_object_set_properties(struct mp_object *obj, ...);

/**
 * @brief Get multiple properties' values of an mp_object.
 *
 * This function gets one or more properties' values of the given object.
 * The arguments must be provided as a sequence of {key, value} pairs, terminated by
 * MP_PROP_LIST_END.
 *
 * Example usage:
 *
 * @code
 * mp_object_get_properties(obj, "key1", val1, "key2", val2, MP_PROP_LIST_END);
 * @endcode
 *
 * @param obj Pointer to a @ref mp_object.
 * @param ... A variable list of {uint32_t key, void *val} pairs, terminated by MP_PROP_LIST_END.
 *
 */
int mp_object_get_properties(struct mp_object *obj, ...);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_MP_OBJECT_H_ */
