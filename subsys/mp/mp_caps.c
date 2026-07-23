/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <zephyr/mp/mp_caps.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_object.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mp_caps, LOG_LEVEL_DBG);

int mp_caps_init(struct mp_caps *caps, uint8_t flag)
{
	if (caps == NULL) {
		return -EINVAL;
	}

	caps->object.ref = ATOMIC_INIT(0);
	caps->object.flags = flag;
	caps->object.id = MP_OBJECT_ID_NONE;
	caps->object.release = NULL;
	caps->get_structure = NULL;

	return 0;
}

struct mp_caps_slist {
	struct mp_caps base;
	sys_slist_t slist;
};

static void mp_caps_slist_destroy(struct mp_object *obj)
{
	struct mp_caps_slist *caps = (struct mp_caps_slist *)obj;
	struct mp_structure *structure;

	__ASSERT_NO_MSG(obj != NULL);

	while (!sys_slist_is_empty(&caps->slist)) {
		structure = CONTAINER_OF(sys_slist_get(&caps->slist), struct mp_structure, node);
		mp_structure_unref(structure);
	}

	k_free(obj);
}

static struct mp_structure *mp_caps_slist_get_structure(struct mp_caps *caps_in, int index)
{
	struct mp_caps_slist *caps = (struct mp_caps_slist *)caps_in;
	struct mp_structure *structure;
	int i = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&caps->slist, structure, node) {
		if (i++ == index) {
			return mp_structure_ref(structure);
		}
	}

	return NULL;
}

static struct mp_caps *mp_caps_slist_new(void)
{
	struct mp_caps_slist *caps = k_calloc(1, sizeof(*caps));
	if (caps == NULL) {
		return NULL;
	}

	mp_caps_init(&caps->base, 0);
	caps->base.object.release = mp_caps_slist_destroy;
	caps->base.get_structure = mp_caps_slist_get_structure;
	sys_slist_init(&caps->slist);

	return mp_caps_ref(&caps->base);
}

struct mp_caps_any {
	struct mp_caps base;
};

static void mp_caps_any_destroy(struct mp_object *obj)
{
	k_free(obj);
}

static struct mp_structure *mp_caps_any_get_structure(struct mp_caps *caps, int index)
{
	return NULL;
}

struct mp_caps *mp_caps_any_new(void)
{
	struct mp_caps_any *caps = k_calloc(1, sizeof(*caps));
	if (caps == NULL) {
		return NULL;
	}

	mp_caps_init(&caps->base, MP_CAPS_FLAG_ANY);
	caps->base.object.release = mp_caps_any_destroy;
	caps->base.get_structure = mp_caps_any_get_structure;

	return mp_caps_ref(&caps->base);
}

struct mp_caps *mp_caps_new(uint8_t media_type_id, ...)
{
	struct mp_caps *caps;
	struct mp_structure *structure;
	va_list args;

	caps = mp_caps_slist_new();
	if (caps == NULL) {
		return NULL;
	}

	if (media_type_id == MP_MEDIA_END) {
		return caps;
	}

	va_start(args, media_type_id);

	structure = mp_structure_new_va(media_type_id, &args);
	if (structure == NULL) {
		goto error;
	}

	va_end(args);

	mp_caps_append(caps, structure);

	return caps;
error:
	mp_caps_unref(caps);
	mp_structure_unref(structure);
	return NULL;
}

int mp_caps_replace(struct mp_caps **target_caps, struct mp_caps *new_caps)
{
	struct mp_caps *old_caps;

	if (target_caps == NULL || new_caps == NULL) {
		return -EINVAL;
	}

	old_caps = *target_caps;

	/* Update the target with a new reference */
	*target_caps = mp_caps_ref(new_caps);

	/* Release the old reference */
	mp_caps_unref(old_caps);

	return 0;
}

int mp_caps_append(struct mp_caps *caps_in, struct mp_structure *structure)
{
	if (caps_in == NULL || caps_in->object.flags == MP_CAPS_FLAG_ANY || structure == NULL) {
		return -EINVAL;
	}

	struct mp_caps_slist *caps = (struct mp_caps_slist *)caps_in;
	sys_slist_append(&caps->slist, &structure->node);

	return 0;
}

void mp_caps_print(struct mp_caps *caps)
{
	if (caps == NULL) {
		printk("Caps NULL\n");
		return;
	}

	if (mp_caps_is_any(caps)) {
		printk("Caps ANY\n");
		return;
	}

	if (mp_caps_is_empty(caps)) {
		printk("Caps EMPTY\n");
		return;
	}

	for (int i = 0;; i++) {
		struct mp_structure *structure = mp_caps_get_structure(caps, i);
		if (structure == NULL) {
			break;
		}
		mp_structure_print(structure);
		mp_structure_unref(structure);
	}
}

bool mp_caps_is_empty(struct mp_caps *caps)
{
	if (caps == NULL) {
		return true;
	}

	if (caps->object.flags == MP_CAPS_FLAG_ANY) {
		return false;
	}

	struct mp_structure *structure = mp_caps_get_structure(caps, 0);
	if (structure == NULL) {
		return true;
	}
	mp_structure_unref(structure);

	return false;
}

bool mp_caps_is_any(struct mp_caps *caps)
{
	return caps != NULL && caps->object.flags == MP_CAPS_FLAG_ANY;
}

bool mp_caps_is_fixed(struct mp_caps *caps)
{
	struct mp_structure *first_structure;

	if (caps == NULL) {
		return false;
	}

	first_structure = mp_caps_get_structure(caps, 0);

	return (first_structure != NULL) ? mp_structure_is_fixed(first_structure) : false;
}

struct mp_caps_intersect {
	struct mp_caps base;
	struct mp_caps *caps1;
	struct mp_caps *caps2;
	uint16_t *index1;
	uint16_t *index2;
	size_t num_matches;
};

static struct mp_structure *mp_caps_intersect_get_structure(struct mp_caps *caps_in, int i)
{
	struct mp_caps_intersect *caps = (struct mp_caps_intersect *)caps_in;

	if (i > caps->num_matches) {
		return NULL;
	}

	struct mp_structure *s1 = mp_caps_get_structure(caps->caps1, caps->index1[i]);
	struct mp_structure *s2 = mp_caps_get_structure(caps->caps2, caps->index2[i]);
	struct mp_structure *si = mp_structure_intersect(s1, s2);
	mp_structure_unref(s1);
	mp_structure_unref(s2);
	return si;
}

static void mp_caps_intersect_destroy(struct mp_object *obj)
{
	struct mp_caps_intersect *caps = (struct mp_caps_intersect *)obj;

	mp_caps_unref(caps->caps1);
	mp_caps_unref(caps->caps2);
	k_free(caps->index1);
	k_free(caps->index2);
	k_free(caps);
}

static struct mp_caps *mp_caps_intersect_new(struct mp_caps *caps1, struct mp_caps *caps2)
{
	struct mp_caps_intersect *caps = k_calloc(1, sizeof(*caps));
	if (caps == NULL) {
		return NULL;
	}

	mp_caps_init(&caps->base, 0);
	caps->base.object.release = mp_caps_intersect_destroy;
	caps->base.get_structure = mp_caps_intersect_get_structure;
	caps->caps1 = mp_caps_ref(caps1);
	caps->caps2 = mp_caps_ref(caps2);
	caps->index1 = NULL;
	caps->index2 = NULL;

	struct mp_structure *s1;
	struct mp_structure *s2;

	for (int i1 = 0; (s1 = mp_caps_get_structure(caps1, i1)) != NULL; i1++) {
		for (int i2 = 0; (s2 = mp_caps_get_structure(caps2, i2)) != NULL; i2++) {
			struct mp_structure *struct_intersect = mp_structure_intersect(s1, s2);
			if (struct_intersect == NULL) {
				continue;
			}

			caps->num_matches++;

			caps->index1 = k_realloc(
				caps->index1, caps->num_matches * sizeof(*caps->index1));
			if (caps->index1 == NULL) {
				mp_caps_intersect_destroy((struct mp_object *)caps);
				return NULL;
			}

			caps->index2 = k_realloc(
				caps->index2, caps->num_matches * sizeof(*caps->index2));
			if (caps->index2 == NULL) {
				mp_caps_intersect_destroy((struct mp_object *)caps);
				return NULL;
			}

			caps->index1[caps->num_matches - 1] = i1;
			caps->index2[caps->num_matches - 1] = i2;

			mp_structure_unref(struct_intersect);
			mp_structure_unref(s2);
		}
		mp_structure_unref(s1);
	}

	return mp_caps_ref(&caps->base);
}

struct mp_caps *mp_caps_intersect(struct mp_caps *caps1, struct mp_caps *caps2)
{
	if (mp_caps_is_empty(caps1) || mp_caps_is_empty(caps2)) {
		return NULL;
	}

	if (mp_caps_is_any(caps1)) {
		return mp_caps_ref(caps2);
	}

	if (mp_caps_is_any(caps2)) {
		return mp_caps_ref(caps1);
	}

	return mp_caps_intersect_new(caps1, caps2);
}

bool mp_caps_can_intersect(struct mp_caps *caps1, struct mp_caps *caps2)
{

	struct mp_structure *s1, *s2;

	if (caps1 == NULL || caps2 == NULL || mp_caps_is_empty(caps1) || mp_caps_is_empty(caps2)) {
		return false;
	}

	if (mp_caps_is_any(caps1) || mp_caps_is_any(caps2)) {
		return true;
	}

	for (int i1 = 0; (s1 = mp_caps_get_structure(caps1, i1)) != NULL; i1++) {
		for (int i2 = 0; (s2 = mp_caps_get_structure(caps2, i2)) != NULL; i2++) {
			if (mp_structure_can_intersect(s1, s2)) {
				return true;
			}
			mp_structure_unref(s2);
		}
		mp_structure_unref(s1);
	}

	return false;
}

struct mp_structure *mp_caps_get_structure(struct mp_caps *caps, int index)
{
	return caps->get_structure(caps, index);
}

struct mp_caps *mp_caps_fixate(struct mp_caps *caps)
{
	struct mp_caps *fixed_caps;
	struct mp_structure *fixated_structure;
	struct mp_structure *structure;

	if (caps == NULL || mp_caps_is_any(caps) || mp_caps_is_empty(caps)) {
		return NULL;
	}

	structure = mp_caps_get_structure(caps, 0);
	if (structure == NULL) {
		return NULL;
	}

	fixed_caps = mp_caps_slist_new();
	if (fixed_caps == NULL) {
		return NULL;
	}

	fixated_structure = mp_structure_fixate(structure);
	if (fixated_structure == NULL) {
		mp_caps_unref(fixed_caps);
		mp_structure_unref(structure);
		return NULL;
	}

	mp_caps_append(fixed_caps, fixated_structure);
	mp_structure_unref(structure);

	return fixed_caps;
}
