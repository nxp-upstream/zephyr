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

void mp_sink_update_caps(struct mp_sink *sink, struct mp_caps *caps)
{
	mp_caps_replace(&sink->sink_caps, caps);
	mp_caps_replace(&sink->sinkpad.caps, sink->sink_caps);
}

static struct mp_caps *mp_sink_get_caps(struct mp_sink *sink)
{
	return sink ? mp_caps_ref(sink->sink_caps) : NULL;
}

static int mp_sink_set_caps(struct mp_sink *sink, struct mp_caps *caps)
{
	if (sink == NULL) {
		return -EINVAL;
	}

	mp_caps_replace(&sink->sinkpad.caps, caps);

	return 0;
}

static int mp_sink_query(struct mp_pad *pad, struct mp_query *query)
{
	struct mp_sink *self = MP_SINK(pad->object.container);
	struct mp_caps *caps_intersect, *query_caps;
	int ret;

	switch (query->type) {
	case MP_QUERY_CAPS:
		query_caps = mp_query_get_caps(query);
		if (query_caps != NULL) {
			caps_intersect = mp_caps_intersect(self->sink_caps, query_caps);
			if (caps_intersect == NULL) {
				return -ENODATA;
			}
			if (mp_caps_is_empty(caps_intersect)) {
				mp_caps_unref(caps_intersect);
				return -ENODATA;
			}
			ret = mp_query_set_caps(query, caps_intersect);
			mp_caps_unref(caps_intersect);
			return ret;
		} else {
			return mp_query_set_caps(query, self->sink_caps);
		}
	case MP_QUERY_ALLOCATION:
		if (self->propose_allocation != NULL) {
			return self->propose_allocation(self, query);
		}

		return 0;
	default:
		return -ENOTSUP;
	}
}

int mp_sink_event(struct mp_pad *pad, struct mp_event *event)
{
	struct mp_sink *sink = MP_SINK(pad->object.container);

	switch (event->type) {
	case MP_EVENT_EOS:
		LOG_DBG("MP_EVENT_EOS");
		return 0;
	case MP_EVENT_CAPS:
		LOG_DBG("MP_EVENT_CAPS");
		return sink->set_caps(sink, mp_event_get_caps(event));
	default:
		return 0;
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

	sink->get_caps = mp_sink_get_caps;
	sink->set_caps = mp_sink_set_caps;
	sink->propose_allocation = NULL;
}
