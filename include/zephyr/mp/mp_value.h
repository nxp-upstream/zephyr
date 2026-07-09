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
 * @brief A generic container for values for different @ref mp_value_type
 *
 * @{
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/mp/mp_object.h>

/** @brief Value comparison result: first value is less than second */
#define MP_VALUE_LESS_THAN      -1
/** @brief Value comparison result: values are equal */
#define MP_VALUE_EQUAL          0
/** @brief Value comparison result: first value is greater than second */
#define MP_VALUE_GREATER_THAN   1
/** @brief Value comparison result: values cannot be ordered */
#define MP_VALUE_UNORDERED      2
/** @brief Value comparison failed due to error */
#define MP_VALUE_COMPARE_FAILED 3

/**
 * @brief value encoded as either an immediate value, or a pointer to a value structure.
 */
typedef struct mp_value *mp_value_t;

/**
 * @brief mp_value type enumeration
 */
enum mp_value_type {
	MP_TYPE_NONE = 0,            /**< No type */
	MP_TYPE_BOOLEAN,             /**< Boolean value */
	MP_TYPE_ENUM,                /**< Enumeration value */
	MP_TYPE_INT,                 /**< Signed integer value */
	MP_TYPE_RANGE,               /**< Integer range value */
	MP_TYPE_STRING,              /**< String value */
	MP_TYPE_LIST,                /**< List of values */
	MP_TYPE_OBJECT,              /**< Object reference */
	MP_TYPE_PTR,                 /**< Pointer type */
	MP_TYPE_COUNT                /**< Number of types */
};

/** @brief Helper for passing a boolean to va_arg functions */
#define MP_BOOLEAN(value)	MP_TYPE_BOOLEAN, (int64_t)(value)

/** @brief Helper for passing an enum to va_arg functions */
#define MP_ENUM(value)		MP_TYPE_ENUM, (int64_t)(value)

/** @brief Helper for passing an integer to va_arg functions */
#define MP_INT(value)		MP_TYPE_INT, (int64_t)(value)

/** @biref Helper for passing an FPS number to va_arg functions */
#define MP_FPS(value)		MP_INT(NSEC_PER_SEC / (value))

/** @brief Helper for passing a string to va_arg functions */
#define MP_STRING(value)	MP_TYPE_STRING, (char *)(value)

/** @brief Helper for passing a range to va_arg functions */
#define MP_RANGE(min, max, step) MP_TYPE_RANGE, (int64_t)(min), (int64_t)(max), (int64_t)(step)

/** @brief Helper for passing a list to va_arg functions */
#define MP_LIST(...)		MP_TYPE_LIST, __VA_ARGS__, MP_TYPE_NONE

/** @brief Helper for passing an mp_object to va_arg functions */
#define MP_OBJECT(value)	MP_TYPE_OBJECT, (struct mp_object *)(value)

/** @brief Helper for passing a pointer to va_arg functions */
#define MP_PTR(value)		MP_TYPE_PTR, (void *)(value)

/**
 * @brief Base mp_value structure
 */
struct mp_value {
	/** For internal use, see @ref mp_value_get_type */
	enum mp_value_type _type;
};

/**
 * @brief Create a new mp_value with the specified types and initialization arguments.
 *
 * This function creates a new mp_value instance based on the provided type.
 *
 * The number and type of variadic arguments depend on the specified enum mp_value_type:
 *
 * The first argument is expected to be the type, and the ones following to be the values.
 *
 * - MP_TYPE_BOOLEAN, MP_TYPE_ENUM, MP_TYPE_INT, MP_TYPE_STRING, MP_TYPE_OBJECT, MP_TYPE_PTR:
 *   Require one initialization value.
 *
 * - MP_TYPE_RANGE: Requires three integer values (min, max, and step).
 *
 * - MP_TYPE_LIST: Requires a sequence of mp_value elements, terminated with NULL
 *   to indicate the end of the list.
 *
 * @param type The type of the value to create.
 * @param argp Pointer to a va_list containing the initialization arguments.
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed or on invalid arguments
 */
mp_value_t mp_value_new_va(va_list *argp);

/**
 * @brief Create a new range value
 *
 * @param min Minimum value of the range
 * @param max Maximum value of the range
 * @param step Step between two values of the range
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed
 */
mp_value_t mp_value_new_range(int64_t min, int64_t max, int64_t step);

/**
 * @brief Create a new integer value
 *
 * @param i Integer to use as value
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed
 */
mp_value_t mp_value_new_int(int64_t i);

/**
 * @brief Create a new enum value
 *
 * @param e Enum to use as value
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed
 */
mp_value_t mp_value_new_enum(int e);

/**
 * @brief Create a new boolean value
 *
 * @param b Boolean to use as value
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed
 */
mp_value_t mp_value_new_boolean(bool b);

/**
 * @brief Create a new string value
 *
 * @param s String to use as value
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed
 */
mp_value_t mp_value_new_string(const char *s);

/**
 * @brief Create a new mp_value with list type of fixed length.
 *
 * The length cannot be modified. A new list needs to be created and elements transferred.
 *
 * @param size Number of elements the list contains
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed or on invalid arguments
 */
mp_value_t mp_value_new_list(size_t size);

/**
 * @brief Create a new list from a va_list.
 *
 * Accepts a va_list pointer that will be iterated until NULL is encountered,
 * marking the end of the list.
 *
 * @param argp Pointer to a va_list containing the elements of the list.
 *
 * @retval The newly created value
 * @retval NULL if memory allocation failed or on invalid arguments
 */
mp_value_t mp_value_new_list_va(va_list *argp);

/**
 * @brief Destroy a value and release its resources.
 *
 * @param value value to destroy
 *
 * @return 0 on success, -EINVAL if value is NULL
 */
int mp_value_destroy(mp_value_t value);

/**
 * @brief Get list size.
 *
 * @param list list of values
 *
 * @return size of list
 */
size_t mp_value_list_get_size(const mp_value_t list);

/**
 * @brief Set list size.
 *
 * This can only be set to a lower value than the previous one as to reduce the size of a list,
 * otherwise the size is ignored.
 *
 * @param list list of values
 * @param size new size
 *
 * @return size of list
 */
void mp_value_list_set_size(mp_value_t list, size_t size);

/**
 * @brief Return true if list is empty.
 *
 * @param list list of values
 *
 * @return true if list is empty, false otherwise
 */
bool mp_value_list_is_empty(const mp_value_t list);

/**
 * @brief Get the type of a value
 *
 * @param value the value to query
 *
 * @return type of value
 */
enum mp_value_type mp_value_get_type(const mp_value_t value);

/**
 * @brief Set the type of a value
 *
 * @param value pointer the value
 * @param value type to set
 */
void mp_value_set_type(mp_value_t *value, enum mp_value_type type);

/**
 * @brief Set value at index in list.
 *
 * @param list list of value
 * @param index index of value to get from list
 * @param value index of value to get from list
 */
void mp_value_list_set(const mp_value_t list, int index, mp_value_t value);

/**
 * @brief Get value at index in list.
 *
 * @param list list of value
 * @param index index of value to get from list
 *
 * @return value at given index in list, or NULL if not found
 */
mp_value_t mp_value_list_get(const mp_value_t list, int index);

/** Get boolean value of MP_TYPE_BOOLEAN */
bool mp_value_get_boolean(const mp_value_t value);

/** Get int value of MP_TYPE_INT */
int64_t mp_value_get_int(const mp_value_t value);

/** Get string value of MP_TYPE_STRING */
const char *mp_value_get_string(const mp_value_t value);

/** Get pointer value of MP_TYPE_PTR */
void *mp_value_get_ptr(const mp_value_t value);


/** Get minimum value of @ref mp_value with MP_TYPE_RANGE */
int64_t mp_value_get_range_min(const mp_value_t range);

/* Get maximum value of @ref mp_value with MP_TYPE_RANGE */
int64_t mp_value_get_range_max(const mp_value_t range);

/* Get step value of @ref mp_value with MP_TYPE_RANGE */
int64_t mp_value_get_range_step(const mp_value_t range);

/** Get the object reference of a mp_value with MP_TYPE_OBJECT */
struct mp_object *mp_value_get_object(mp_value_t value);

/**
 * Comparison between two primitive values
 *
 * @param val1 first value
 * @param val2 second value
 * @return MP_VALUE_GREATER_THAN if val1 > val2
 *	MP_VALUE_LESS_THAN if val1 < val2
 *	MP_VALUE_EQUAL if val1 == val2
 *	MP_VALUE_UNORDERED if val1 and val2 are not comparable
 *	MP_VALUE_COMPARE_FAILED if val1 and val2 are not same type
 */
int mp_value_compare(const mp_value_t val1, const mp_value_t val2);

/**
 * Intersect between two values
 *
 * @param val1 reference value to compare with
 * @param val2 value to compare with
 * @return NULL if intersect is empty
 */
mp_value_t mp_value_intersect(const mp_value_t val1, const mp_value_t val2);

/**
 * Intersect between value and range
 *
 * @param ref_val reference value to compare with
 * @param compare_val value to compare with
 * @return NULL if intersect is empty
 */
mp_value_t mp_value_intersect_range(const mp_value_t ref_val,
				    const mp_value_t compare_val);

/**
 * Intersect between list with value, range or list
 *
 * @param list reference value to compare with
 * @param compare_val value to compare with
 * @return NULL if intersect is empty
 */
mp_value_t mp_value_intersect_list(const mp_value_t list,
					 const mp_value_t compare_val);

/**
 * Check if two values can intersect
 *
 * @param val1 first value
 * @param val2 second value
 * @return true if two values can intersect
 */
bool mp_value_can_intersect(const mp_value_t val1, const mp_value_t val2);

/**
 * Duplicate value
 *
 * @param value value to duplicate
 * @return new value with same type and data as original value, or NULL on failure
 * @note For string only pointer is copied, not string itself.
 */
mp_value_t mp_value_duplicate(const mp_value_t value);

/**
 * @brief Check if a value is a primitive type
 *
 * @param value Value to check, must not be NULL
 *
 * @return true if value is primitive, false otherwise
 */
bool mp_value_is_primitive(const mp_value_t value);

/**
 * @brief Print a value
 *
 * @param value Value to print, may be NULL
 * @param new_line Add newline after printing
 */
void mp_value_print(const mp_value_t value, bool new_line);

/** @} */

#endif /*ZEPHYR_INCLUDE_MP_MP_VALUE_H_*/
