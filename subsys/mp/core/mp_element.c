/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/core/mp_bin.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_event.h>
#include <zephyr/mp/core/mp_pad.h>

LOG_MODULE_REGISTER(mp_element, CONFIG_MP_LOG_LEVEL);

void mp_element_add_pad(struct mp_element *element, struct mp_pad *pad)
{
	__ASSERT_NO_MSG(element != NULL);
	__ASSERT_NO_MSG(pad != NULL);

	/* Set element that contains this pad */
	MP_OBJECT(pad)->container = MP_OBJECT(element);

	if (pad->direction == MP_PAD_SRC) {
		sys_dlist_append(&element->srcpads, &pad->object.node);
	}

	if (pad->direction == MP_PAD_SINK) {
		sys_dlist_append(&element->sinkpads, &pad->object.node);
	}
}

static struct mp_pad *mp_element_get_unlinked_pad(struct mp_element *element, uint8_t pad_id,
						  enum mp_pad_direction direction)
{
	struct mp_object *obj;
	struct mp_pad *pad;

	sys_dlist_t *pads = direction == MP_PAD_SRC ? &element->srcpads : &element->sinkpads;

	SYS_DLIST_FOR_EACH_CONTAINER(pads, obj, node) {
		pad = MP_PAD(obj);
		if (pad->peer == NULL && (pad_id == UINT8_MAX || pad_id == obj->id)) {
			return pad;
		}
	}

	return NULL;
}

static int mp_element_link_pads(struct mp_element *src, uint8_t src_pad_id,
				struct mp_element *sink, uint8_t sink_pad_id)
{
	struct mp_pad *srcpad = mp_element_get_unlinked_pad(src, src_pad_id, MP_PAD_SRC);
	struct mp_pad *sinkpad = mp_element_get_unlinked_pad(sink, sink_pad_id, MP_PAD_SINK);

	if (srcpad == NULL || sinkpad == NULL) {
		LOG_ERR("Link failed: no free %s pad on element %u",
			srcpad == NULL ? "src" : "sink",
			srcpad == NULL ? src->object.id : sink->object.id);
		return -EINVAL;
	}

	if (!mp_caps_can_intersect(srcpad->caps, sinkpad->caps)) {
		return -ENOTSUP;
	}

	return mp_pad_link(srcpad, sinkpad);
}

int mp_element_link(struct mp_element *element, struct mp_element *next_element, ...)
{
	int ret;
	va_list args;

	va_start(args, next_element);
	while (next_element != NULL) {
		/* Connect the 1st unlinked pad of each element together */
		ret = mp_element_link_pads(element, UINT8_MAX, next_element, UINT8_MAX);
		if (ret != 0) {
			va_end(args);
			return ret;
		}

		element = next_element;
		next_element = va_arg(args, struct mp_element *);
	}
	va_end(args);

	return 0;
}

enum mp_state_change_return mp_element_set_state(struct mp_element *element, enum mp_state state)
{
	if (element->set_state != NULL) {
		return element->set_state(element, state);
	}

	return MP_STATE_CHANGE_FAILURE;
}

static enum mp_state_change_return mp_element_set_state_func(struct mp_element *element,
							     enum mp_state state)
{
	enum mp_state_change_return ret = MP_STATE_CHANGE_SUCCESS;
	enum mp_state_change transition;
	enum mp_state next;
	enum mp_state *current = &element->current_state;

	while (*current != state) {
		next = MP_STATE_GET_NEXT(*current, state);
		transition = MP_STATE_TRANSITION(*current, next);
		ret = element->change_state(element, transition);
		/* Do not handle ASYNC yet */
		if (ret != MP_STATE_CHANGE_SUCCESS) {
			return ret;
		}

		*current = next;
	}

	return ret;
}

static enum mp_state_change_return mp_element_change_state_func(struct mp_element *element,
								enum mp_state_change transition)
{
	enum mp_state_change_return result = MP_STATE_CHANGE_SUCCESS;

	switch (transition) {
	case MP_STATE_CHANGE_READY_TO_PAUSED:
		break;
	case MP_STATE_CHANGE_PAUSED_TO_PLAYING:
		break;
	case MP_STATE_CHANGE_PLAYING_TO_PAUSED:
		break;
	case MP_STATE_CHANGE_PAUSED_TO_READY:
		break;
	default:
		break;
	}

	return result;
}

static int mp_element_send_event_default(struct mp_element *element, struct mp_event *event)
{
	struct mp_object *obj;
	int ret = -ENOTSUP;
	sys_dlist_t *pad_list = NULL;

	if (element == NULL || event == NULL) {
		return -EINVAL;
	}

	if (MP_EVENT_DIRECTION(event) & MP_EVENT_DIRECTION_UPSTREAM) {
		pad_list = &element->sinkpads;
	}

	if (MP_EVENT_DIRECTION(event) & MP_EVENT_DIRECTION_DOWNSTREAM) {
		pad_list = &element->srcpads;
	}

	SYS_DLIST_FOR_EACH_CONTAINER(pad_list, obj, node) {
		int r = mp_pad_send_event(MP_PAD(obj), event);

		if (r == 0) {
			ret = 0;
		} else {
			LOG_DBG("pad %u: event send failed: %d", obj->id, r);
		}
	}

	return ret;
}

struct mp_bus *mp_element_get_bus(struct mp_element *element)
{
	if (element == NULL) {
		return NULL;
	}

	/* Get the top-level bin (i.e. pipeline) bus to send the message for now, but messages may
	 * be passed hierachically from the nearest bin to the pipeline if they need to be filtered
	 * or modified at each level.
	 */
	while (MP_OBJECT(element)->container != NULL) {
		element = MP_ELEMENT(MP_OBJECT(element)->container);
	}

	return &MP_BIN(element)->bus;
}

void mp_element_init(struct mp_element *self, uint8_t id)
{
	self->object.id = id;

	sys_dlist_init(&self->srcpads);
	sys_dlist_init(&self->sinkpads);

	self->current_state = MP_STATE_READY;
	self->set_state = mp_element_set_state_func;
	self->change_state = mp_element_change_state_func;
	self->eventfn = mp_element_send_event_default;
}
