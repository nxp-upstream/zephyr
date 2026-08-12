/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_parser.h>
#include <zephyr/mpipe/mpipe_structure.h>

LOG_MODULE_REGISTER(mpipe_parser, CONFIG_MPIPE_LOG_LEVEL);

#define MPIPE_PAD_SINK_ID 0
#define MPIPE_PAD_SRC_ID  1

static int mpipe_parser_set_caps(struct mpipe_parser *parser, enum mpipe_pad_direction direction,
				 const struct mpipe_structure *caps)
{
	if (caps == NULL) {
		return -EINVAL;
	}

	if (direction == MPIPE_PAD_SINK) {
		return mpipe_pad_set_caps(&parser->sinkpad, caps);
	}

	if (direction == MPIPE_PAD_SRC) {
		return mpipe_pad_set_caps(&parser->srcpad, caps);
	}

	return -EINVAL;
}

static inline int mpipe_parser_query_caps(struct mpipe_parser *self,
					  enum mpipe_pad_direction direction,
					  struct mpipe_dispatch *query)
{
	int ret;
	struct mpipe_pad *this_pad, *other_pad;
	struct mpipe_structure other_caps;
	struct mpipe_structure candidate;

	switch (direction) {
	case MPIPE_PAD_SINK:
		this_pad = &self->sinkpad;
		other_pad = &self->srcpad;
		break;
	case MPIPE_PAD_SRC:
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
	ret = mpipe_pad_enum_first(this_pad, mpipe_dispatch_get_caps(query), &candidate);
	if (ret != 0) {
		return ret;
	}

	/* Query the peer using what the other side supports */
	ret = mpipe_pad_enum_caps(other_pad, 0, NULL, &other_caps);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_dispatch_set_caps(query, &other_caps);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_pad_query(other_pad->peer, query);
	if (ret < 0) {
		return ret;
	}

	/* Keep the query result at other_pad to use later at caps event */
	ret = mpipe_pad_set_caps(other_pad, mpipe_dispatch_get_caps(query));
	if (ret != 0) {
		return ret;
	}

	/* Answer the query */
	ret = mpipe_dispatch_set_caps(query, &candidate);

	return ret;
}

static int mpipe_parser_event(struct mpipe_pad *pad, struct mpipe_dispatch *event)
{
	struct mpipe_parser *parser = (struct mpipe_parser *)pad->object.container;
	struct mpipe_pad *other_pad =
		(pad->direction == MPIPE_PAD_SINK) ? &parser->srcpad : &parser->sinkpad;
	int ret;

	switch (event->type) {
	case MPIPE_DISPATCH_EOS:
		return mpipe_pad_send_event_default(pad, event);
	case MPIPE_DISPATCH_CAPS:
		ret = mpipe_pad_set_caps(pad, mpipe_dispatch_get_caps(event));
		if (ret != 0) {
			return ret;
		}

		ret = mpipe_dispatch_set_caps(event, &other_pad->caps);
		if (ret < 0) {
			return ret;
		}

		return mpipe_pad_send_event_default(pad, event);
	default:
		return -ENOTSUP;
	}
}

/* TODO: Make a helper to refactor this together with mpipe_transform */
static int mpipe_parser_query(struct mpipe_pad *pad, struct mpipe_dispatch *query)
{
	if (pad == NULL || query == NULL) {
		return -EINVAL;
	}

	int ret;
	struct mpipe_parser *parser = (struct mpipe_parser *)pad->object.container;

	switch (query->type) {
	case MPIPE_DISPATCH_CAPS:
		return mpipe_parser_query_caps(parser, pad->direction, query);
	case MPIPE_DISPATCH_BUFFER_CONFIG:
		struct mpipe_dispatch peer_query;

		mpipe_dispatch_buffer_config_init(&peer_query, &parser->srcpad.caps);

		/* Query the downstream */
		ret = mpipe_pad_query(parser->srcpad.peer, &peer_query);
		if (ret < 0) {
			mpipe_dispatch_clear(&peer_query);
			return ret;
		}

		if (parser->decide_allocation != NULL) {
			ret = parser->decide_allocation(parser, &peer_query);
			if (ret < 0) {
				mpipe_dispatch_clear(&peer_query);
				return ret;
			}

			mpipe_dispatch_clear(&peer_query);
		}

		/* Configure/start the output buffer pool */
		if (parser->outpool != NULL && !parser->outpool->started) {
			ret = mpipe_buffer_pool_configure(parser->outpool, &parser->srcpad.caps);
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to configure output parser buffer pool");
				return ret;
			}

			ret = mpipe_buffer_pool_start(parser->outpool);
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

enum mpipe_state_change_return mpipe_parser_change_state(struct mpipe_element *self,
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

void mpipe_parser_init(struct mpipe_element *self)
{
	struct mpipe_parser *parser = (struct mpipe_parser *)self;

	mpipe_pad_init(&parser->sinkpad, MPIPE_PAD_SINK_ID, MPIPE_PAD_SINK, MPIPE_PAD_ALWAYS);
	mpipe_element_add_pad(self, &parser->sinkpad);

	mpipe_pad_init(&parser->srcpad, MPIPE_PAD_SRC_ID, MPIPE_PAD_SRC, MPIPE_PAD_ALWAYS);
	mpipe_element_add_pad(self, &parser->srcpad);

	parser->outpool = NULL;
	self->change_state = mpipe_parser_change_state;
	parser->set_caps = mpipe_parser_set_caps;
	parser->srcpad.queryfn = mpipe_parser_query;
	parser->sinkpad.queryfn = mpipe_parser_query;
	parser->srcpad.eventfn = mpipe_parser_event;
	parser->sinkpad.eventfn = mpipe_parser_event;
	parser->decide_allocation = NULL;
	parser->propose_allocation = NULL;
}
