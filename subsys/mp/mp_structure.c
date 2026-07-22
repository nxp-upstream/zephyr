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
	structure->media_type_id = media_type_id;
	structure->capacity = capacity;
	memset(&structure->fields, 0x00, capacity * sizeof(*structure->fields));

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

	return structure;
}

int mp_structure_clear(struct mp_structure *structure)
{
	if (structure == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0; i < structure->num_values; i++) {
		mp_value_destroy(structure->fields[i].value);
	}

	mp_structure_init(structure, 0, 0);

	return 0;
}

void mp_structure_destroy(struct mp_structure *structure)
{
	int ret;

	ret = mp_structure_clear(structure);
	if (ret < 0) {
		return;
	}

	k_free(structure);
}

int mp_structure_append(struct mp_structure *structure, uint8_t field_id, mp_value_t value)
{
	if (structure == NULL || value == NULL) {
		return -EINVAL;
	}

	if (structure->num_values >= structure->capacity) {
		/* User should allocate enough room ahead of time */
		return -ENOBUFS;
	}

	for (size_t i = 0; i < structure->num_values; i++) {
		if (structure->fields[i].id == field_id) {
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

bool mp_structure_can_intersect(struct mp_structure *struct1, struct mp_structure *struct2)
{
	mp_value_t intersect_value;
	bool can_intersect = false;

	if (struct1 == NULL || struct2 == NULL) {
		return false;
	}

	/* Check media type ID */
	if (struct1->media_type_id != struct2->media_type_id) {
		return false;
	}

	/* Check fields in struct1 against struct2 */
	for (size_t i = 0; i < struct1->num_values; i++) {
		uint8_t id1 = struct1->fields[i].id;
		mp_value_t value1 = struct1->fields[i].value;
		mp_value_t value2 = mp_structure_get_value(struct2, id1);
		if (value2 != NULL) {
			intersect_value = mp_value_intersect(value1, value2);
			if (intersect_value != NULL) {
				can_intersect = true;
				mp_value_destroy(intersect_value);
			} else {
				can_intersect = false;
				break;
			}
		}
	}

	return can_intersect;
}

struct mp_structure *mp_structure_intersect(struct mp_structure *struct1,
					    struct mp_structure *struct2)
{
	struct mp_structure *intersect_structure;

	if (!mp_structure_can_intersect(struct1, struct2)) {
		return NULL;
	}

	intersect_structure = mp_structure_new_empty(
		struct1->media_type_id, min(struct1->num_values, struct2->num_values));

	for (size_t i = 0; i < struct1->num_values; i++) {
		uint8_t id = struct1->fields[i].id;
		mp_value_t value1 = mp_structure_get_value(struct1, id);
		mp_value_t value2 = mp_structure_get_value(struct2, id);
		mp_structure_append(intersect_structure, id, mp_value_intersect(value1, value2));
	}

	return intersect_structure;
}

struct mp_structure *mp_structure_duplicate(struct mp_structure *src)
{
	int ret;
	struct mp_structure *dup;

	if (src == NULL) {
		return NULL;
	}

	dup = mp_structure_new_empty(src->media_type_id, src->num_values);
	for (size_t i = 0; i < src->num_values; i++) {
		ret = mp_structure_append(dup, src->fields[i].id,
					  mp_value_duplicate(src->fields[i].value));
		if (ret != 0) {
			goto error;
		}
	}

	return dup;
error:
	mp_structure_destroy(dup);
	return NULL;}

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
			fixated_value = mp_value_duplicate(mp_value_list_get(value, 0));
			break;
		default:
			fixated_value = mp_value_duplicate(value);
			break;
		}

		mp_structure_append(fixated_structure, field_id, fixated_value);
	}

	return fixated_structure;
}
