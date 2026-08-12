/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_bus.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_object.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_message.h>

LOG_MODULE_REGISTER(mpipe_sink, CONFIG_MPIPE_LOG_LEVEL);

#define MPIPE_PAD_SINK_ID 0

static int mpipe_sink_set_caps(struct mpipe_sink *sink, const struct mpipe_structure *caps)
{
	if (sink == NULL) {
		return -EINVAL;
	}

	return mpipe_pad_set_caps(&sink->sinkpad, caps);
}

static int mpipe_sink_query(struct mpipe_pad *pad, struct mpipe_dispatch *query)
{
	struct mpipe_sink *self = (struct mpipe_sink *)pad->object.container;

	switch (query->type) {
	case MPIPE_DISPATCH_CAPS:
		return mpipe_pad_answer_caps_query(pad, query);
	case MPIPE_DISPATCH_BUFFER_CONFIG:
		if (self->propose_allocation != NULL) {
			return self->propose_allocation(self, query);
		}

		return 0;
	default:
		return -ENOTSUP;
	}
}

int mpipe_sink_event(struct mpipe_pad *pad, struct mpipe_dispatch *event)
{
	struct mpipe_sink *sink = (struct mpipe_sink *)pad->object.container;

	switch (event->type) {
	case MPIPE_DISPATCH_EOS: {
		struct mpipe_message msg = {
			.origin = &sink->element,
			.type = MPIPE_MESSAGE_EOS,
		};

		/*
		 * EOS event reached the end of the pipeline, post an EOS message to the bus so that
		 * applications know that this sink has finished processing all upstream data.
		 */
		(void)mpipe_message_post(&msg);

		return 0;
	}
	case MPIPE_DISPATCH_CAPS:
		return sink->set_caps(sink, mpipe_dispatch_get_caps(event));
	default:
		return 0;
	}
}

static int mpipe_sink_chainfn(struct mpipe_pad *pad, struct net_buf *in_buf,
			      struct net_buf **out_buf)
{
	/* By default, do nothing, just absorb the buffer */
	ARG_UNUSED(pad);

	net_buf_unref(in_buf);
	*out_buf = NULL;

	return 0;
}

enum mpipe_state_change_return mpipe_sink_change_state(struct mpipe_element *self,
						       enum mpipe_state_change transition)
{
	switch (transition) {
	case MPIPE_STATE_CHANGE_PAUSED_TO_READY:
		mpipe_element_reset_pad_caps(self);
		break;
	default:
		break;
	}

	return MPIPE_STATE_CHANGE_SUCCESS;
}

void mpipe_sink_init(struct mpipe_element *self)
{
	struct mpipe_sink *sink = (struct mpipe_sink *)self;

	mpipe_pad_init(&sink->sinkpad, MPIPE_PAD_SINK_ID, MPIPE_PAD_SINK, MPIPE_PAD_ALWAYS);
	mpipe_element_add_pad(self, &sink->sinkpad);

	self->change_state = mpipe_sink_change_state;

	sink->sinkpad.queryfn = mpipe_sink_query;
	sink->sinkpad.eventfn = mpipe_sink_event;
	sink->sinkpad.chainfn = mpipe_sink_chainfn;
	sink->set_caps = mpipe_sink_set_caps;
	sink->propose_allocation = NULL;
}
