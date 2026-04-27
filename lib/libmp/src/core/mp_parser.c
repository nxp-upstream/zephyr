#include <zephyr/logging/log.h>

#include "mp_buffer.h"
#include "mp_parser.h"

LOG_MODULE_REGISTER(mp_parser, CONFIG_LIBMP_LOG_LEVEL);

#define MP_PAD_SINK_ID 0
#define MP_PAD_SRC_ID  1

void mp_parser_update_caps(struct mp_parser *parser, struct mp_caps *sink_caps,
			   struct mp_caps *src_caps)
{
	mp_caps_replace(&parser->sink_caps, sink_caps);
	mp_caps_replace(&parser->sinkpad.caps, parser->sink_caps);
	mp_caps_replace(&parser->src_caps, src_caps);
	mp_caps_replace(&parser->srcpad.caps, parser->src_caps);
}

static struct mp_caps *mp_parser_get_caps(struct mp_parser *parser, enum mp_pad_direction direction)
{
	if (direction == MP_PAD_SINK) {
		return mp_caps_ref(parser->sink_caps);
	}

	if (direction == MP_PAD_SRC) {
		return mp_caps_ref(parser->src_caps);
	}

	return NULL;
}

static bool mp_parser_set_caps(struct mp_parser *parser, enum mp_pad_direction direction,
			       struct mp_caps *caps)
{
	if (caps == NULL) {
		return false;
	}

	if (direction == MP_PAD_SINK) {
		mp_caps_replace(&parser->sinkpad.caps, caps);
		return true;
	}

	if (direction == MP_PAD_SRC) {
		mp_caps_replace(&parser->srcpad.caps, caps);
		return true;
	}

	return false;
}

static inline bool mp_parser_query_caps(struct mp_parser *self, enum mp_pad_direction direction,
					struct mp_query *query)
{
	int ret = false;
	struct mp_pad *this_pad, *other_pad;
	struct mp_caps *queried_pad_caps;
	struct mp_caps *this_caps = (direction == MP_PAD_SINK) ? self->sink_caps : self->src_caps;
	struct mp_caps *other_caps = (direction == MP_PAD_SINK) ? self->src_caps : self->sink_caps;

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
		return false;
	}

	queried_pad_caps = mp_caps_intersect(mp_query_get_caps(query), this_caps);
	if (queried_pad_caps == NULL || mp_caps_is_empty(queried_pad_caps)) {
		return false;
	}

	/* Query the peer using the other side supported caps */
	ret = mp_query_set_caps(query, other_caps);
	if (!ret || !mp_pad_query(other_pad->peer, query)) {
		mp_caps_unref(queried_pad_caps);
		return false;
	}

	/* Keep query_caps result at other_pad to use later at caps event */
	mp_caps_replace(&other_pad->caps, mp_query_get_caps(query));

	/* Answer the query */
	ret = mp_query_set_caps(query, queried_pad_caps);
	mp_caps_unref(queried_pad_caps);

	return ret;
}

static bool mp_parser_event(struct mp_pad *pad, struct mp_event *event)
{
	struct mp_parser *parser = MP_PARSER(pad->object.container);
	struct mp_pad *other_pad =
		(pad->direction == MP_PAD_SINK) ? &parser->srcpad : &parser->sinkpad;

	switch (event->type) {
	case MP_EVENT_EOS:
		LOG_DBG("MP_EVENT_EOS");

		return mp_pad_send_event_default(pad, event);
	case MP_EVENT_CAPS:
		mp_caps_replace(&pad->caps, mp_event_get_caps(event));

		if (!mp_event_set_caps(event, other_pad->caps)) {
			return false;
		}

		return mp_pad_send_event_default(other_pad, event);
	default:
		return false;
	}
}

/* TODO: Make a helper to refactor this together with mp_transform */
static bool mp_parser_query(struct mp_pad *pad, struct mp_query *query)
{
	if (pad == NULL || query == NULL) {
		return false;
	}

	int ret;
	struct mp_parser *parser = MP_PARSER(pad->object.container);

	switch (query->type) {
	case MP_QUERY_CAPS:
		return mp_parser_query_caps(parser, pad->direction, query);
	case MP_QUERY_ALLOCATION:
		struct mp_query *peer_query = mp_query_new_allocation(parser->srcpad.caps);

		/* Query the downstream */
		if (!mp_pad_query(parser->srcpad.peer, peer_query)) {
			mp_query_destroy(peer_query);
			return false;
		}

		if (!parser->decide_allocation(parser, peer_query)) {
			mp_query_destroy(peer_query);
			return false;
		}

		/* Configure/start the output buffer pool */
		if (parser->outpool != NULL && !parser->outpool->started) {
			ret = mp_buffer_pool_configure(
				parser->outpool, mp_caps_get_structure(parser->srcpad.caps, 0));
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to configure output parser buffer pool");
				return false;
			}

			ret = mp_buffer_pool_start(parser->outpool);
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to start output parser buffer pool");
				return false;
			}
		}

		/* Propose allocation to upstream */
		return parser->propose_allocation(parser, query);
	default:
		return false;
	}
}

static enum mp_state_change_return mp_parser_change_state(struct mp_element *element,
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

static bool mp_parser_decide_allocation(struct mp_parser *self, struct mp_query *query)
{
	return true;
}

static bool mp_parser_propose_allocation(struct mp_parser *self, struct mp_query *query)
{
	return true;
}

void mp_parser_init(struct mp_element *self)
{
	struct mp_parser *parser = MP_PARSER(self);

	/* Default supported caps */
	parser->sink_caps = mp_caps_new_any();
	parser->src_caps = mp_caps_new_any();

	mp_pad_init(&parser->sinkpad, MP_PAD_SINK_ID, MP_PAD_SINK, MP_PAD_ALWAYS,
		    mp_caps_ref(parser->sink_caps));
	mp_element_add_pad(self, &parser->sinkpad);

	mp_pad_init(&parser->srcpad, MP_PAD_SRC_ID, MP_PAD_SRC, MP_PAD_ALWAYS,
		    mp_caps_ref(parser->src_caps));
	mp_element_add_pad(self, &parser->srcpad);

	self->change_state = mp_parser_change_state;

	parser->outpool = NULL;
	parser->get_caps = mp_parser_get_caps;
	parser->set_caps = mp_parser_set_caps;
	parser->srcpad.queryfn = mp_parser_query;
	parser->sinkpad.queryfn = mp_parser_query;
	parser->srcpad.eventfn = mp_parser_event;
	parser->sinkpad.eventfn = mp_parser_event;
	parser->decide_allocation = mp_parser_decide_allocation;
	parser->propose_allocation = mp_parser_propose_allocation;
}
