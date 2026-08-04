/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_pad.h>

LOG_MODULE_REGISTER(mp_pad, CONFIG_MP_LOG_LEVEL);

static int mp_pad_enum_caps_default(struct mp_pad *pad, uint32_t index,
				    const struct mp_structure *filter, struct mp_structure *out)
{
	/* A pad carries one capability, so there is nothing past index 0 */
	if (index > 0U) {
		return -ENOENT;
	}

	if (filter == NULL) {
		return mp_structure_duplicate(&pad->caps, out);
	}

	/* This capability cannot satisfy the filter, but a later one might */
	return (mp_structure_intersect(&pad->caps, filter, out) != 0) ? -EAGAIN : 0;
}

int mp_pad_enum_caps(struct mp_pad *pad, uint32_t index, const struct mp_structure *filter,
		     struct mp_structure *out)
{
	if (pad == NULL || pad->enum_capsfn == NULL || out == NULL) {
		return -EINVAL;
	}

	return pad->enum_capsfn(pad, index, filter, out);
}

int mp_pad_answer_caps_query(struct mp_pad *pad, struct mp_dispatch *query)
{
	struct mp_structure candidate;
	int ret;

	ret = mp_pad_enum_first(pad, mp_dispatch_get_caps(query), &candidate);
	if (ret != 0) {
		return ret;
	}

	ret = mp_dispatch_set_caps(query, &candidate);
	mp_structure_clear(&candidate);

	return ret;
}

int mp_pad_enum_first(struct mp_pad *pad, const struct mp_structure *filter,
		      struct mp_structure *out)
{
	/* A filter that constrains nothing accepts the capability as it is */
	if (filter != NULL && mp_structure_is_any(filter)) {
		filter = NULL;
	}

	for (uint32_t index = 0;; index++) {
		int ret = mp_pad_enum_caps(pad, index, filter, out);

		if (ret == -EAGAIN) {
			continue;
		}

		return (ret == -ENOENT) ? -ENODATA : ret;
	}
}

void mp_pad_init(struct mp_pad *pad, uint8_t id, enum mp_pad_direction direction,
		 enum mp_pad_presence presence)
{
	__ASSERT_NO_MSG(pad != NULL);

	mp_object_init(&pad->object);
	pad->object.id = id;
	pad->direction = direction;
	pad->presence = presence;
	atomic_set(&pad->flushing, 0);

	/* A pad constrains nothing until a capability is negotiated onto it */
	mp_structure_init_any(&pad->caps);
	pad->enum_capsfn = mp_pad_enum_caps_default;
}

int mp_pad_set_caps(struct mp_pad *pad, const struct mp_structure *caps)
{
	if (pad == NULL) {
		return -EINVAL;
	}

	if (caps == &pad->caps) {
		return 0;
	}

	mp_structure_clear(&pad->caps);

	if (caps == NULL) {
		return mp_structure_init_any(&pad->caps);
	}

	return mp_structure_duplicate(caps, &pad->caps);
}

int mp_pad_link(struct mp_pad *srcpad, struct mp_pad *sinkpad)
{
	if (srcpad == NULL || sinkpad == NULL) {
		return -EINVAL;
	}

	/* Set peer pad */
	srcpad->peer = sinkpad;
	sinkpad->peer = srcpad;

	return 0;
}

int mp_pad_query(struct mp_pad *pad, struct mp_dispatch *query)
{
	int ret;

	if (pad == NULL || query == NULL) {
		return -EINVAL;
	}

	if (pad->queryfn == NULL) {
		return -ENOTSUP;
	}

	ret = pad->queryfn(pad, query);
	if (ret != 0) {
		return ret;
	}

	/* Caps query is considered successful only if the query's caps is valid */
	if (query->type == MP_DISPATCH_CAPS && mp_structure_is_empty(mp_dispatch_get_caps(query))) {
		return -ENODATA;
	}

	return 0;
}

int mp_pad_send_event_default(struct mp_pad *pad, struct mp_dispatch *event)
{
	int ret = -ENOTSUP;

	if (pad == NULL || event == NULL) {
		return -EINVAL;
	}

	struct mp_element *element = (struct mp_element *)pad->object.container;
	struct mp_object *obj;
	sys_dlist_t *otherpad_list;

	if (pad->direction == MP_PAD_SINK) {
		otherpad_list = &element->srcpads;
	} else {
		otherpad_list = &element->sinkpads;
	}

	SYS_DLIST_FOR_EACH_CONTAINER(otherpad_list, obj, node) {
		struct mp_pad *otherpad = (struct mp_pad *)obj;

		if (otherpad->peer == NULL) {
			LOG_DBG("pad %u: no peer, skipping", obj->id);
			continue;
		}

		int r = mp_pad_send_event(otherpad->peer, event);

		if (r == 0) {
			ret = 0;
		} else {
			LOG_DBG("pad %u: event send to peer failed: %d", obj->id, r);
		}
	}

	return ret;
}

int mp_pad_send_event(struct mp_pad *pad, struct mp_dispatch *event)
{
	if (pad == NULL || event == NULL) {
		return -EINVAL;
	}

	if (pad->eventfn == NULL) {
		return -ENOTSUP;
	}

	return pad->eventfn(pad, event);
}
