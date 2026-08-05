/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

LOG_MODULE_REGISTER(mp_structure, CONFIG_MP_LOG_LEVEL);

int mp_structure_init(struct mp_structure *structure, uint8_t media_type_id)
{
	if (structure == NULL || media_type_id >= MP_MEDIA_END) {
		return -EINVAL;
	}

	structure->media_type_id = media_type_id;
	structure->flags = 0;
	structure->num_fields = 0;

	return 0;
}

int mp_structure_init_any(struct mp_structure *structure)
{
	int ret = mp_structure_init(structure, MP_MEDIA_UNKNOWN);

	if (ret != 0) {
		return ret;
	}

	structure->flags = MP_STRUCTURE_FLAG_ANY;

	return 0;
}

bool mp_structure_is_any(const struct mp_structure *structure)
{
	return structure != NULL && (structure->flags & MP_STRUCTURE_FLAG_ANY) != 0;
}

bool mp_structure_is_empty(const struct mp_structure *structure)
{
	return structure != NULL && !mp_structure_is_any(structure) && structure->num_fields == 0;
}

int mp_structure_clear(struct mp_structure *structure)
{
	if (structure == NULL) {
		return -EINVAL;
	}

	/*
	 * Resetting num_fields to empty the structure. Every read of ids and values is
	 * bounded by num_fields, so the slots past it are already unreachable. The media
	 * type and the flags are left alone so the structure can be filled in again as it is.
	 */
	structure->num_fields = 0;

	return 0;
}

int mp_structure_append_value(struct mp_structure *structure, uint8_t field_id,
			      const struct mp_value *value)
{
	if (structure == NULL || value == NULL || field_id >= MP_CAPS_END) {
		return -EINVAL;
	}

	if (mp_structure_get_value(structure, field_id) != NULL) {
		return -EEXIST;
	}

	if (structure->num_fields >= CONFIG_MP_STRUCTURE_MAX_FIELDS) {
		LOG_ERR("No slot left for field %u, raise CONFIG_MP_STRUCTURE_MAX_FIELDS",
			field_id);
		return -ENOSPC;
	}

	/* A value owns nothing, so filling the slot is a plain copy */
	structure->ids[structure->num_fields] = field_id;
	structure->values[structure->num_fields] = *value;
	structure->num_fields++;

	return 0;
}

int mp_structure_copy_field(const struct mp_structure *src, struct mp_structure *dst,
			    uint8_t field_id)
{
	const struct mp_value *value = mp_structure_get_value(src, field_id);

	if (value == NULL) {
		return -ENOENT;
	}

	return mp_structure_append_value(dst, field_id, value);
}

int mp_structure_init_fields(struct mp_structure *structure, uint8_t media_type_id, ...)
{
	va_list args;
	struct mp_value value;
	enum mp_value_type type;
	uint8_t field_id;
	int ret;

	ret = mp_structure_init(structure, media_type_id);
	if (ret != 0) {
		return ret;
	}

	va_start(args, media_type_id);
	while (1) {
		field_id = (uint8_t)va_arg(args, uint32_t);
		if (field_id == MP_CAPS_END) {
			break;
		}

		type = va_arg(args, int);
		ret = mp_value_set_va_list(&value, type, &args);
		if (ret != 0) {
			break;
		}

		ret = mp_structure_append_value(structure, field_id, &value);
		if (ret != 0) {
			break;
		}
	}
	va_end(args);

	if (ret != 0) {
		mp_structure_clear(structure);
	}

	return ret;
}

void mp_structure_print(const struct mp_structure *structure)
{
	__ASSERT_NO_MSG(structure != NULL);

	printk("\n");
	printk("Media Type ID: %u\n", structure->media_type_id);
	for (uint8_t i = 0; i < structure->num_fields; i++) {
		printk("Field ID: %u\n", structure->ids[i]);
		mp_value_print(&structure->values[i], true);
	}
}

const struct mp_value *mp_structure_get_value(const struct mp_structure *structure,
					      uint8_t field_id)
{
	if (structure == NULL) {
		return NULL;
	}

	for (uint8_t i = 0; i < structure->num_fields; i++) {
		if (structure->ids[i] == field_id) {
			return &structure->values[i];
		}
	}

	return NULL;
}

int mp_structure_remove_field(struct mp_structure *structure, uint8_t field_id)
{
	if (structure == NULL) {
		return -EINVAL;
	}

	for (uint8_t i = 0; i < structure->num_fields; i++) {
		if (structure->ids[i] != field_id) {
			continue;
		}

		/* Slots carry no order, so the last one can fill the hole */
		structure->num_fields--;
		structure->ids[i] = structure->ids[structure->num_fields];
		structure->values[i] = structure->values[structure->num_fields];

		return 0;
	}

	return -ENOENT;
}

bool mp_structure_can_intersect(const struct mp_structure *struct1,
				const struct mp_structure *struct2)
{
	struct mp_structure scratch;

	/*
	 * Code saving by reusing mp_structure_intersect(). Although it takes more time
	 * to compute, it only runs at pad-link time, so it is not a performance concern.
	 */
	return mp_structure_intersect(struct1, struct2, &scratch) == 0;
}

int mp_structure_intersect(const struct mp_structure *struct1, const struct mp_structure *struct2,
			   struct mp_structure *out)
{
	int ret;
	struct mp_value intersect_value;
	bool common = false;

	/*
	 * The result is built into out field by field, so an out that is also
	 * an input would be read after it has been reset. Nothing needs it, so
	 * it is refused rather than paid for with a scratch structure.
	 */
	if (struct1 == NULL || struct2 == NULL || out == NULL || out == struct1 || out == struct2) {
		return -EINVAL;
	}

	/* A structure that constrains nothing leaves the other side unchanged */
	if (mp_structure_is_any(struct1)) {
		return mp_structure_duplicate(struct2, out);
	}

	if (mp_structure_is_any(struct2)) {
		return mp_structure_duplicate(struct1, out);
	}

	if (struct1->media_type_id != struct2->media_type_id) {
		return -EINVAL;
	}

	ret = mp_structure_init(out, struct1->media_type_id);
	if (ret != 0) {
		return ret;
	}

	for (uint8_t i = 0; i < struct1->num_fields; i++) {
		const struct mp_value *other = mp_structure_get_value(struct2, struct1->ids[i]);

		if (other != NULL) {
			if (mp_value_intersect(&struct1->values[i], other, &intersect_value) != 0) {
				mp_structure_clear(out);
				return -ENOENT;
			}

			common = true;
		} else {
			/* A field only one side constrains is carried through unchanged */
			intersect_value = struct1->values[i];
		}

		ret = mp_structure_append_value(out, struct1->ids[i], &intersect_value);
		if (ret != 0) {
			mp_structure_clear(out);
			return ret;
		}
	}

	/* Structures with nothing in common do not intersect */
	if (!common) {
		mp_structure_clear(out);
		return -ENOENT;
	}

	/* Whatever only the other side constrains is carried through as well */
	for (uint8_t i = 0; i < struct2->num_fields; i++) {
		if (mp_structure_get_value(struct1, struct2->ids[i]) != NULL) {
			continue;
		}

		ret = mp_structure_append_value(out, struct2->ids[i], &struct2->values[i]);
		if (ret != 0) {
			mp_structure_clear(out);
			return ret;
		}
	}

	return 0;
}

int mp_structure_duplicate(const struct mp_structure *src, struct mp_structure *out)
{
	if (src == NULL || out == NULL) {
		return -EINVAL;
	}

	*out = *src;

	return 0;
}

bool mp_structure_is_fixed(const struct mp_structure *structure)
{
	if (structure == NULL || mp_structure_is_any(structure) || structure->num_fields == 0) {
		return false;
	}

	for (uint8_t i = 0; i < structure->num_fields; i++) {
		if (!mp_value_is_primitive(&structure->values[i])) {
			return false;
		}
	}

	return true;
}

int mp_structure_fixate(const struct mp_structure *src, struct mp_structure *out)
{
	struct mp_value fixated_value;
	int ret;

	/* Same as the intersection: out is built from src, so it cannot be src */
	if (src == NULL || out == NULL || out == src) {
		return -EINVAL;
	}

	if (mp_structure_is_any(src) || src->num_fields == 0) {
		return -ENOENT;
	}

	ret = mp_structure_init(out, src->media_type_id);
	if (ret != 0) {
		return ret;
	}

	for (uint8_t i = 0; i < src->num_fields; i++) {
		const struct mp_value *value = &src->values[i];

		switch (value->type) {
		case MP_TYPE_INT_RANGE:
			fixated_value.type = MP_TYPE_INT;
			fixated_value.v_int = mp_value_get_int_range_min(value);
			break;
		case MP_TYPE_UINT_RANGE:
			fixated_value.type = MP_TYPE_UINT;
			fixated_value.v_uint = mp_value_get_uint_range_min(value);
			break;
		default:
			/* Already a single value, so it fixates to itself */
			fixated_value = *value;
			break;
		}

		ret = mp_structure_append_value(out, src->ids[i], &fixated_value);
		if (ret != 0) {
			mp_structure_clear(out);
			return ret;
		}
	}

	return 0;
}
