/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_parser.h>
#include <zephyr/mp/mp_structure.h>

LOG_MODULE_REGISTER(mp_parser, CONFIG_MP_LOG_LEVEL);

#define MP_PAD_SINK_ID 0
#define MP_PAD_SRC_ID  1

static int mp_parser_set_caps(struct mp_parser *parser, enum mp_pad_direction direction,
			      const struct mp_structure *caps)
{
	if (caps == NULL) {
		return -EINVAL;
	}

	if (direction == MP_PAD_SINK) {
		return mp_pad_set_caps(&parser->sinkpad, caps);
	}

	if (direction == MP_PAD_SRC) {
		return mp_pad_set_caps(&parser->srcpad, caps);
	}

	return -EINVAL;
}

static inline int mp_parser_query_caps(struct mp_parser *self, enum mp_pad_direction direction,
				       struct mp_dispatch *query)
{
	int ret;
	struct mp_pad *this_pad, *other_pad;
	struct mp_structure other_caps;
	struct mp_structure candidate;

	switch (direction) {
	case MP_PAD_SINK:
		this_pad = &self->sinkpad;
		other_pad = &self->srcpad;
		break;
	case MP_PAD_SRC:
		this_pad = &self->srcpad;
		other_pad = &self->sinkpad;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Keep the first supported capability the query accepts. The peer is
	 * asked with the caps supported on the other side, which do not depend
	 * on the candidate, so there is nothing here to backtrack over.
	 */
	ret = mp_pad_enum_first(this_pad, mp_dispatch_get_caps(query), &candidate);
	if (ret != 0) {
		return ret;
	}

	/* Query the peer using what the other side supports */
	ret = mp_pad_enum_caps(other_pad, 0, NULL, &other_caps);
	if (ret != 0) {
		return ret;
	}

	ret = mp_dispatch_set_caps(query, &other_caps);
	if (ret < 0) {
		return ret;
	}

	ret = mp_pad_query(other_pad->peer, query);
	if (ret < 0) {
		return ret;
	}

	/* Keep the query result at other_pad to use later at caps event */
	ret = mp_pad_set_caps(other_pad, mp_dispatch_get_caps(query));
	if (ret != 0) {
		return ret;
	}

	/* Answer the query */
	ret = mp_dispatch_set_caps(query, &candidate);

	return ret;
}

static int mp_parser_event(struct mp_pad *pad, struct mp_dispatch *event)
{
	struct mp_parser *parser = (struct mp_parser *)pad->object.container;
	struct mp_pad *other_pad =
		(pad->direction == MP_PAD_SINK) ? &parser->srcpad : &parser->sinkpad;
	int ret;

	switch (event->type) {
	case MP_DISPATCH_EOS:
		return mp_pad_send_event_default(pad, event);
	case MP_DISPATCH_CAPS:
		ret = mp_pad_set_caps(pad, mp_dispatch_get_caps(event));
		if (ret != 0) {
			return ret;
		}

		ret = mp_dispatch_set_caps(event, &other_pad->caps);
		if (ret < 0) {
			return ret;
		}

		return mp_pad_send_event_default(pad, event);
	default:
		return -ENOTSUP;
	}
}

/* TODO: Make a helper to refactor this together with mp_transform */
static int mp_parser_query(struct mp_pad *pad, struct mp_dispatch *query)
{
	if (pad == NULL || query == NULL) {
		return -EINVAL;
	}

	int ret;
	struct mp_parser *parser = (struct mp_parser *)pad->object.container;

	switch (query->type) {
	case MP_DISPATCH_CAPS:
		return mp_parser_query_caps(parser, pad->direction, query);
	case MP_DISPATCH_BUFFER_CONFIG:
		struct mp_dispatch peer_query;

		mp_dispatch_buffer_config_init(&peer_query, &parser->srcpad.caps);

		/* Query the downstream */
		ret = mp_pad_query(parser->srcpad.peer, &peer_query);
		if (ret < 0) {
			mp_dispatch_clear(&peer_query);
			return ret;
		}

		if (parser->decide_allocation != NULL) {
			ret = parser->decide_allocation(parser, &peer_query);
			if (ret < 0) {
				mp_dispatch_clear(&peer_query);
				return ret;
			}

			mp_dispatch_clear(&peer_query);
		}

		/* Configure/start the output buffer pool */
		if (parser->outpool != NULL && !parser->outpool->started) {
			ret = mp_buffer_pool_configure(parser->outpool, &parser->srcpad.caps);
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to configure output parser buffer pool");
				return ret;
			}

			ret = mp_buffer_pool_start(parser->outpool);
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to start output parser buffer pool");
				return ret;
			}
		}

		/* Propose allocation to upstream */
		if (parser->propose_allocation != NULL) {
			return parser->propose_allocation(parser, query);
		}

		return 0;
	default:
		return -ENOTSUP;
	}
}

enum mp_state_change_return mp_parser_change_state(struct mp_element *self,
						   enum mp_state_change transition)
{
	struct mp_parser *parser = (struct mp_parser *)self;

	switch (transition) {
	case MP_STATE_CHANGE_PAUSED_TO_READY:
		/*
		 * Reset the negotiated pad caps back to ANY so a subsequent
		 * re-negotiation (on replay) starts fresh. Derived parsers that
		 * override change_state must chain to this base function to
		 * inherit the reset.
		 */
		mp_pad_set_caps(&parser->sinkpad, NULL);
		mp_pad_set_caps(&parser->srcpad, NULL);
		break;
	default:
		break;
	}

	return MP_STATE_CHANGE_SUCCESS;
}

void mp_parser_init(struct mp_element *self)
{
	struct mp_parser *parser = (struct mp_parser *)self;

	mp_pad_init(&parser->sinkpad, MP_PAD_SINK_ID, MP_PAD_SINK, MP_PAD_ALWAYS);
	mp_element_add_pad(self, &parser->sinkpad);

	mp_pad_init(&parser->srcpad, MP_PAD_SRC_ID, MP_PAD_SRC, MP_PAD_ALWAYS);
	mp_element_add_pad(self, &parser->srcpad);

	parser->outpool = NULL;
	self->change_state = mp_parser_change_state;
	parser->set_caps = mp_parser_set_caps;
	parser->srcpad.queryfn = mp_parser_query;
	parser->sinkpad.queryfn = mp_parser_query;
	parser->srcpad.eventfn = mp_parser_event;
	parser->sinkpad.eventfn = mp_parser_event;
	parser->decide_allocation = NULL;
	parser->propose_allocation = NULL;
}
