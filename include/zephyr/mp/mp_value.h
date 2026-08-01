/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_value.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_VALUE_H_
#define ZEPHYR_INCLUDE_MP_MP_VALUE_H_

/**
 * @defgroup mp_value Value Container
 * @ingroup mp_framework
 * @brief A generic container for values of different types
 *
 * Pointers passed to this API must not be NULL unless the parameter is
 * documented otherwise.
 *
 * @{
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Result of comparing two values
 *
 * The first three members order the two values. The last two report that no
 * ordering exists: the values are comparable but unordered, or the comparison
 * could not be made at all.
 */
enum mp_value_compare_result {
	/** First value is less than the second */
	MP_VALUE_LESS_THAN = -1,
	/** Values are equal */
	MP_VALUE_EQUAL = 0,
	/** First value is greater than the second */
	MP_VALUE_GREATER_THAN = 1,
	/** Values are of the same type but have no ordering */
	MP_VALUE_UNORDERED = 2,
	/** Comparison failed: a value is NULL or the two types differ */
	MP_VALUE_COMPARE_FAILED = 3,
};

/**
 * @brief Value types
 */
enum mp_value_type {
	MP_TYPE_NONE = 0,   /**< No type */
	MP_TYPE_BOOLEAN,    /**< Boolean value */
	MP_TYPE_INT,        /**< Signed integer value */
	MP_TYPE_UINT,       /**< Unsigned integer value */
	MP_TYPE_INT_RANGE,  /**< Integer range value */
	MP_TYPE_UINT_RANGE, /**< Unsigned integer range value */
	MP_TYPE_COUNT       /**< Number of types */
};

/**
 * @brief Value structure
 *
 * One fixed-size tagged union so that every value costs the same
 * and can be stored by value inside the structure that owns it.
 */
struct mp_value {
	/** Type of value, see @ref mp_value_type */
	enum mp_value_type type;
	union {
		/** MP_TYPE_BOOLEAN */
		bool v_boolean;
		/** MP_TYPE_INT */
		int32_t v_int;
		/** MP_TYPE_UINT */
		uint32_t v_uint;
		/** MP_TYPE_INT_RANGE, MP_TYPE_UINT_RANGE */
		struct {
			union {
				int32_t v_int;
				uint32_t v_uint;
			} min, max, step;
		} range;
	};
};

/**
 * @name Initializers for a statically defined value
 *
 * Each expands to a braced initializer for one @ref mp_value, so a capability
 * known at build time can be placed in .rodata rather than built at runtime.
 * They are what @ref MP_STRUCTURE_DEFINE takes for each of its fields. The
 * range forms take the bounds and the step between two consecutive values.
 * @{
 */
/** Initialize an MP_TYPE_BOOLEAN value holding @p v */
#define MP_VALUE_BOOLEAN(v)                {.type = MP_TYPE_BOOLEAN, .v_boolean = (v)}
/** Initialize an MP_TYPE_INT value holding @p v */
#define MP_VALUE_INT(v)                    {.type = MP_TYPE_INT, .v_int = (v)}
/** Initialize an MP_TYPE_UINT value holding @p v */
#define MP_VALUE_UINT(v)                   {.type = MP_TYPE_UINT, .v_uint = (v)}
/** @cond INTERNAL_HIDDEN */
#define MP_VALUE_RANGE_INIT(m, lo, hi, st) {.min.m = (lo), .max.m = (hi), .step.m = (st)}
/** @endcond */
/** Initialize an MP_TYPE_INT_RANGE value spanning @p lo to @p hi by @p st */
#define MP_VALUE_INT_RANGE(lo, hi, st)                                                             \
	{.type = MP_TYPE_INT_RANGE, .range = MP_VALUE_RANGE_INIT(v_int, lo, hi, st)}
/** Initialize an MP_TYPE_UINT_RANGE value spanning @p lo to @p hi by @p st */
#define MP_VALUE_UINT_RANGE(lo, hi, st)                                                            \
	{.type = MP_TYPE_UINT_RANGE, .range = MP_VALUE_RANGE_INIT(v_uint, lo, hi, st)}
/** @} */

/**
 * @brief Set a value's type and contents.
 *
 * BOOLEAN, INT and UINT take one argument; the range types take min, max
 * and step.
 *
 * @param value Pointer to the value to set.
 * @param type Type of the value, see @ref mp_value_type. It is an int and not
 *             the enum itself because va_start() starts reading right after
 *             this parameter, so the parameter cannot be of a type narrower
 *             than int, which an enum is allowed to be.
 * @param ... Arguments initializing the value, per the rules above.
 *
 * @return 0 on success, -EINVAL if @p value is NULL or @p type is invalid
 */
int mp_value_set(struct mp_value *value, int type, ...);

/**
 * @brief Set a value from a va_list.
 *
 * Same as @ref mp_value_set but accepts a va_list pointer instead of variadic
 * arguments, so a caller parsing its own list can build a value in place.
 *
 * @param value Pointer to the value to set.
 * @param type Type of the value, see @ref mp_value_type.
 * @param args Pointer to a va_list positioned at this value's arguments.
 *
 * @return 0 on success, -EINVAL if @p value is NULL or @p type is invalid
 */
int mp_value_set_va_list(struct mp_value *value, enum mp_value_type type, va_list *args);

/**
 * @brief Get boolean value of @ref MP_TYPE_BOOLEAN type
 *
 * The getters read the requested member without checking the type, so the
 * caller is responsible for passing a value of the matching type.
 *
 * @param value Pointer to the value.
 *
 * @return The boolean the value holds.
 */
bool mp_value_get_boolean(const struct mp_value *value);

/**
 * @brief Get int value of @ref MP_TYPE_INT type
 *
 * @param value Pointer to the value.
 *
 * @return The integer the value holds.
 */
int32_t mp_value_get_int(const struct mp_value *value);

/**
 * @brief Get uint value of @ref MP_TYPE_UINT type
 *
 * @param value Pointer to the value.
 *
 * @return The unsigned integer the value holds.
 */
uint32_t mp_value_get_uint(const struct mp_value *value);

/**
 * @brief Get minimum value of a @ref MP_TYPE_INT_RANGE type
 *
 * @param range Pointer to the range value.
 *
 * @return The lower bound of the range.
 */
int32_t mp_value_get_int_range_min(const struct mp_value *range);

/**
 * @brief Get maximum value of a @ref MP_TYPE_INT_RANGE type
 *
 * @param range Pointer to the range value.
 *
 * @return The upper bound of the range.
 */
int32_t mp_value_get_int_range_max(const struct mp_value *range);

/**
 * @brief Get step value of a @ref MP_TYPE_INT_RANGE type
 *
 * @param range Pointer to the range value.
 *
 * @return The step between two consecutive values of the range.
 */
int32_t mp_value_get_int_range_step(const struct mp_value *range);

/**
 * @brief Get minimum value of a @ref MP_TYPE_UINT_RANGE type
 *
 * @param range Pointer to the range value.
 *
 * @return The lower bound of the range.
 */
uint32_t mp_value_get_uint_range_min(const struct mp_value *range);

/**
 * @brief Get maximum value of a @ref MP_TYPE_UINT_RANGE type
 *
 * @param range Pointer to the range value.
 *
 * @return The upper bound of the range.
 */
uint32_t mp_value_get_uint_range_max(const struct mp_value *range);

/**
 * @brief Get step value of a @ref MP_TYPE_UINT_RANGE type
 *
 * @param range Pointer to the range value.
 *
 * @return The step between two consecutive values of the range.
 */
uint32_t mp_value_get_uint_range_step(const struct mp_value *range);

/**
 * @brief Compare two values
 *
 * @param val1 Pointer to the first value, may be NULL.
 * @param val2 Pointer to the second value, may be NULL.
 *
 * @return The comparison result, see @ref mp_value_compare_result.
 */
enum mp_value_compare_result mp_value_compare(const struct mp_value *val1,
					      const struct mp_value *val2);

/**
 * @brief Check whether two values actually have a non-empty intersection
 *
 * For primitive values, this means the two values are identical.
 * For range values, this means the ranges overlap.
 *
 * @param val1 Pointer to the first value, may be NULL.
 * @param val2 Pointer to the second value, may be NULL.
 *
 * @return true if the intersection is non-empty, false otherwise or if either
 *         value is NULL
 */
bool mp_value_can_intersect(const struct mp_value *val1, const struct mp_value *val2);

/**
 * @brief Intersect two values, put the result into caller-provided storage
 *
 * @param val1 Pointer to the first value, may be NULL.
 * @param val2 Pointer to the second value, may be NULL.
 * @param out Pointer to storage for the result, untouched unless 0 is returned.
 *
 * @retval 0 on success
 * @retval -ENOENT if the intersection is empty or either input value is NULL
 * @retval -EINVAL if @p out is NULL
 */
int mp_value_intersect(const struct mp_value *val1, const struct mp_value *val2,
               struct mp_value *out);

/**
 * @brief Check if a value is of a primitive type
 *
 * @param value Pointer to the value to check, may be NULL.
 *
 * @return true if the value is primitive, false otherwise or if @p value is NULL
 */
bool mp_value_is_primitive(const struct mp_value *value);

/**
 * @brief Print a value
 *
 * @param value Pointer to the value to print, may be NULL.
 * @param new_line Add newline after printing.
 */
void mp_value_print(const struct mp_value *value, bool new_line);

/** @} */

#endif /*ZEPHYR_INCLUDE_MP_MP_VALUE_H_*/
