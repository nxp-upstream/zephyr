/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include <zephyr/mp/mp_caps.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>
#include <zephyr/sys/minmax.h>

int mp_structure_init(struct mp_structure *structure, uint8_t media_type_id, size_t capacity)
{
	if (structure == NULL) {
		return -EINVAL;
	}

	memset(structure, 0x00, sizeof(*structure));
	memset(&structure->fields, 0x00, capacity * sizeof(*structure->fields));
	structure->media_type_id = media_type_id;
	structure->capacity = capacity;
	structure->ref = ATOMIC_INIT(0);

	return 0;
}

struct mp_structure *mp_structure_new_empty(uint8_t media_type_id, size_t capacity)
{
	struct mp_structure *structure;

	structure = k_malloc(sizeof(struct mp_structure) + sizeof(*structure->fields) * capacity);
	if (structure == NULL) {
		return NULL;
	}

	mp_structure_init(structure, media_type_id, capacity);

	return mp_structure_ref(structure);
}

static void mp_structure_destroy(struct mp_structure *structure)
{
	if (structure == NULL) {
		return;
	}

	for (size_t i = 0; i < structure->num_values; i++) {
		mp_value_unref(structure->fields[i].value);
	}

	k_free(structure);
}

int mp_structure_append(struct mp_structure *structure, uint8_t field_id, mp_value_t value)
{
	if (structure == NULL || value == NULL) {
		return -EINVAL;
	}

	if (structure->num_values >= structure->capacity) {
		mp_value_unref(value);
		/* User should allocate enough room ahead of time */
		return -ENOBUFS;
	}

	for (size_t i = 0; i < structure->num_values; i++) {
		if (structure->fields[i].id == field_id) {
			mp_value_unref(value);
			return -EEXIST;
		}
	}

	structure->fields[structure->num_values].id = field_id;
	structure->fields[structure->num_values].value = value;
	structure->num_values++;

	return 0;
}

struct mp_structure *mp_structure_new_empty_va(uint8_t media_type_id, va_list args)
{
	va_list tmp;

	va_copy(tmp, args);

	size_t num_values = 0;
	while (va_arg(tmp, uint32_t) != MP_CAPS_END) {
		va_arg(tmp, mp_value_t);
		num_values++;
	}

	return mp_structure_new_empty(media_type_id, num_values);
}

struct mp_structure *mp_structure_new_va(uint8_t media_type_id, va_list *argp)
{
	struct mp_structure *structure;

	if (media_type_id == MP_MEDIA_END) {
		return NULL;
	}

	structure = mp_structure_new_empty_va(media_type_id, *argp);
	if (structure == NULL) {
		return NULL;
	}

	for (size_t i = 0; i < structure->capacity; i++) {
		uint32_t field_id = va_arg(*argp, uint32_t);
		mp_value_t value = va_arg(*argp, mp_value_t);
		mp_structure_append(structure, field_id, value);
	}

	return structure;
}

struct mp_structure *mp_structure_new(uint8_t media_type_id, ...)
{
	va_list args;
	struct mp_structure *structure;

	va_start(args, media_type_id);
	structure = mp_structure_new_va(media_type_id, &args);
	va_end(args);

	return structure;
}

void mp_structure_print(struct mp_structure *structure)
{
	printk("\n");
	printk("Media Type ID: %u\n", structure->media_type_id);
	for (size_t i = 0; i < structure->num_values; i++) {
		printk("Field ID %u (type %u): ",
		       structure->fields[i].id, mp_value_get_type(structure->fields[i].value));
		mp_value_print(structure->fields[i].value, true);
	}
}

mp_value_t mp_structure_get_value(struct mp_structure *structure, uint8_t field_id)
{
	if (structure == NULL) {
		return NULL;
	}

	for (size_t i = 0; i < structure->num_values; i++) {
		if (structure->fields[i].id == field_id) {
			return structure->fields[i].value;
		}
	}

	return NULL;
}

int mp_structure_len(struct mp_structure *structure)
{
	return structure->num_values;
}

struct mp_structure *mp_structure_intersect(struct mp_structure *struct1,
					    struct mp_structure *struct2)
{
	struct mp_structure *intersect_structure;
	bool has_common_field = false;

	if (struct1 == NULL || struct2 == NULL) {
		return NULL;
	}

	if (struct1->media_type_id != struct2->media_type_id) {
		return NULL;
	}

	for (size_t i = 0; i < struct1->num_values; i++) {
		uint8_t id = struct1->fields[i].id;
		if (mp_structure_get_value(struct2, id) != NULL) {
			has_common_field = true;
			break;
		}
	}

	if (!has_common_field) {
		return NULL;
	}

	intersect_structure = mp_structure_new_empty(
		struct1->media_type_id, struct1->num_values + struct2->num_values);
	if (intersect_structure == NULL) {
		return NULL;
	}

	/*
	 * Intersect every field of struct1. For a field also present in struct2
	 * (a common field), mp_value_intersect() returns the intersected value or
	 * NULL when the two constraints are incompatible. A field present only in
	 * struct1 intersects against NULL, which "matches anything" and yields a
	 * reference to struct1's value. An empty intersection on a common field
	 * makes the whole structure incompatible, so the intersection fails.
	 */
	for (size_t i = 0; i < struct1->num_values; i++) {
		uint8_t id = struct1->fields[i].id;
		mp_value_t value1 = struct1->fields[i].value;
		mp_value_t value2 = mp_structure_get_value(struct2, id);
		mp_value_t intersect_value = mp_value_intersect(value1, value2);

		if (intersect_value == NULL) {
			mp_structure_unref(intersect_structure);
			return NULL;
		}

		mp_structure_append(intersect_structure, id, intersect_value);
	}

	/* Add the fields that only exist in struct2 */
	for (size_t i = 0; i < struct2->num_values; i++) {
		uint8_t id = struct2->fields[i].id;

		if (mp_structure_get_value(struct1, id) != NULL) {
			/* Common field, already intersected in the loop above */
			continue;
		}

		mp_structure_append(intersect_structure, id,
				    mp_value_ref(struct2->fields[i].value));
	}

	return intersect_structure;
}


bool mp_structure_can_intersect(struct mp_structure *struct1, struct mp_structure *struct2)
{
	bool has_common_field = false;

	if (struct1 == NULL || struct2 == NULL) {
		return false;
	}

	if (struct1->media_type_id != struct2->media_type_id) {
		return false;
	}

	/*
	 * Standalone predicate: check whether the two structures can intersect
	 * without allocating a result. Every field common to both must have
	 * intersectable values, and at least one common field must exist.
	 */
	for (size_t i = 0; i < struct1->num_values; i++) {
		mp_value_t value2 =
			mp_structure_get_value(struct2, struct1->fields[i].id);

		if (value2 == NULL) {
			continue;
		}

		has_common_field = true;

		if (!mp_value_can_intersect(struct1->fields[i].value, value2)) {
			return false;
		}
	}

	return has_common_field;
}


void mp_structure_unref(struct mp_structure *structure)
{
	if (structure == NULL) {
		return;
	}

	__ASSERT_NO_MSG(atomic_get(&structure->ref) > 0);
	if (atomic_dec(&structure->ref) == 1) {
		mp_structure_destroy(structure);
	}
}

struct mp_structure *mp_structure_ref(struct mp_structure *structure)
{
	if (structure == NULL) {
		return NULL;
	}

	atomic_inc(&structure->ref);
	return structure;
}

bool mp_structure_is_fixed(struct mp_structure *structure)
{
	for (size_t i = 0; i < structure->num_values; i++) {
		if (!mp_value_is_primitive(structure->fields[i].value)) {
			return false;
		}
	}

	return true;
}

struct mp_structure *mp_structure_fixate(struct mp_structure *src)
{
	struct mp_structure *fixated_structure;
	mp_value_t fixated_value;

	if (src == NULL) {
		return NULL;
	}

	fixated_structure = mp_structure_new_empty(src->media_type_id, src->num_values);

	for (size_t i = 0; i < src->num_values; i++) {
		uint8_t field_id = src->fields[i].id;
		mp_value_t value = src->fields[i].value;

		switch (mp_value_get_type(src->fields[i].value)) {
		case MP_TYPE_RANGE:
			fixated_value = mp_value_new_int(mp_value_get_range_min(value));
			break;
		case MP_TYPE_LIST:
			fixated_value = mp_value_ref(mp_value_list_get(value, 0));
			break;
		default:
			fixated_value = mp_value_ref(value);
			break;
		}

		mp_structure_append(fixated_structure, field_id, fixated_value);
	}

	return fixated_structure;
}
