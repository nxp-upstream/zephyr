/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mp/core/mp_event.h>
#include <zephyr/mp/core/mp_query.h>
#include <zephyr/mp/core/mp_sink.h>

LOG_MODULE_REGISTER(mp_sink, CONFIG_MP_LOG_LEVEL);

#define MP_PAD_SINK_ID 0

int mp_sink_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	return 0;
}

int mp_sink_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	return 0;
}

void mp_sink_update_caps(struct mp_sink *sink, struct mp_caps *caps)
{
	mp_caps_replace(&sink->sink_caps, caps);
	mp_caps_replace(&sink->sinkpad.caps, sink->sink_caps);
}

static struct mp_caps *mp_sink_get_caps(struct mp_sink *sink)
{
	return sink ? mp_caps_ref(sink->sink_caps) : NULL;
}

static bool mp_sink_set_caps(struct mp_sink *sink, struct mp_caps *caps)
{
	if (sink == NULL) {
		return false;
	}

	mp_caps_replace(&sink->sinkpad.caps, caps);

	return true;
}

/* TODO */
static enum mp_state_change_return mp_sink_change_state(struct mp_element *self,
							enum mp_state_change transition)
{
	enum mp_state_change_return ret = MP_STATE_CHANGE_SUCCESS;

	switch (transition) {
	case MP_STATE_CHANGE_READY_TO_PAUSED:
		break;
	case MP_STATE_CHANGE_PAUSED_TO_PLAYING:
		break;
	default:
		break;
	}

	return ret;
}

static bool mp_sink_propose_allocation(struct mp_sink *self, struct mp_query *query)
{
	return true;
}

static bool mp_sink_query(struct mp_pad *pad, struct mp_query *query)
{
	struct mp_sink *self = MP_SINK(pad->object.container);
	struct mp_caps *caps_intersect, *query_caps;
	int ret;

	switch (query->type) {
	case MP_QUERY_CAPS:
		query_caps = mp_query_get_caps(query);
		if (query_caps != NULL) {
			caps_intersect = mp_caps_intersect(self->sink_caps, query_caps);
			if (caps_intersect == NULL || mp_caps_is_empty(caps_intersect)) {
				return false;
			}
			ret = mp_query_set_caps(query, caps_intersect);
			mp_caps_unref(caps_intersect);
			return ret;
		} else {
			return mp_query_set_caps(query, self->sink_caps);
		}
	case MP_QUERY_ALLOCATION:
		return self->propose_allocation(self, query);
	default:
		return false;
	}
}

bool mp_sink_event(struct mp_pad *pad, struct mp_event *event)
{
	struct mp_sink *sink = MP_SINK(pad->object.container);

	switch (event->type) {
	case MP_EVENT_EOS:
		LOG_DBG("MP_EVENT_EOS");
		return true;
	case MP_EVENT_CAPS:
		LOG_DBG("MP_EVENT_CAPS");
		return sink->set_caps(sink, mp_event_get_caps(event));
	default:
		return true;
	}
}

void mp_sink_init(struct mp_element *self)
{
	struct mp_sink *sink = MP_SINK(self);

	/* Default supported caps */
	sink->sink_caps = mp_caps_new_any();

	mp_pad_init(&sink->sinkpad, MP_PAD_SINK_ID, MP_PAD_SINK, MP_PAD_ALWAYS,
		    mp_sink_get_caps(sink));
	mp_element_add_pad(self, &sink->sinkpad);

	sink->sinkpad.queryfn = mp_sink_query;
	sink->sinkpad.eventfn = mp_sink_event;

	self->object.set_property = mp_sink_set_property;
	self->object.get_property = mp_sink_get_property;
	self->change_state = mp_sink_change_state;

	sink->get_caps = mp_sink_get_caps;
	sink->set_caps = mp_sink_set_caps;
	sink->propose_allocation = mp_sink_propose_allocation;
}
