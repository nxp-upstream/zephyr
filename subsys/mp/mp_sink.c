/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_bus.h>
#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_object.h>
#include <zephyr/mp/mp_sink.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_message.h>

LOG_MODULE_REGISTER(mp_sink, CONFIG_MP_LOG_LEVEL);

#define MP_PAD_SINK_ID 0

static int mp_sink_set_caps(struct mp_sink *sink, const struct mp_structure *caps)
{
	if (sink == NULL) {
		return -EINVAL;
	}

	return mp_pad_set_caps(&sink->sinkpad, caps);
}

/*
 * Answer a caps query with the first supported capability the filter accepts,
 * which is the one the negotiation would settle on.
 */
static int mp_sink_query_caps(struct mp_pad *pad, struct mp_dispatch *query)
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

static int mp_sink_query(struct mp_pad *pad, struct mp_dispatch *query)
{
	struct mp_sink *self = (struct mp_sink *)pad->object.container;

	switch (query->type) {
	case MP_DISPATCH_CAPS:
		return mp_sink_query_caps(pad, query);
	case MP_DISPATCH_BUFFER_CONFIG:
		if (self->propose_allocation != NULL) {
			return self->propose_allocation(self, query);
		}

		return 0;
	default:
		return -ENOTSUP;
	}
}

int mp_sink_event(struct mp_pad *pad, struct mp_dispatch *event)
{
	struct mp_sink *sink = (struct mp_sink *)pad->object.container;

	switch (event->type) {
	case MP_DISPATCH_EOS:
		/*
		 * EOS event reached the end of the pipeline, post an EOS message to the bus so that
		 * applications know that this sink has finished processing all upstream data.
		 */
		mp_element_post_message(&sink->element, MP_MESSAGE_EOS);

		return 0;
	case MP_DISPATCH_CAPS:
		return sink->set_caps(sink, mp_dispatch_get_caps(event));
	default:
		return 0;
	}
}

static int mp_sink_chainfn(struct mp_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	/* By default, do nothing, just absorb the buffer */
	ARG_UNUSED(pad);

	net_buf_unref(in_buf);
	*out_buf = NULL;

	return 0;
}

enum mp_state_change_return mp_sink_change_state(struct mp_element *self,
						 enum mp_state_change transition)
{
	struct mp_sink *sink = (struct mp_sink *)self;

	switch (transition) {
	case MP_STATE_CHANGE_PAUSED_TO_READY:
		/*
		 * Reset the negotiated pad caps back to ANY so a subsequent
		 * re-negotiation (on replay) starts fresh. Derived sinks that
		 * override change_state must chain to this base function to
		 * inherit the reset.
		 */
		mp_pad_set_caps(&sink->sinkpad, NULL);
		break;
	default:
		break;
	}

	return MP_STATE_CHANGE_SUCCESS;
}

void mp_sink_init(struct mp_element *self)
{
	struct mp_sink *sink = (struct mp_sink *)self;

	mp_pad_init(&sink->sinkpad, MP_PAD_SINK_ID, MP_PAD_SINK, MP_PAD_ALWAYS);
	mp_element_add_pad(self, &sink->sinkpad);

	self->change_state = mp_sink_change_state;

	sink->sinkpad.queryfn = mp_sink_query;
	sink->sinkpad.eventfn = mp_sink_event;
	sink->sinkpad.chainfn = mp_sink_chainfn;
	sink->set_caps = mp_sink_set_caps;
	sink->propose_allocation = NULL;
}
