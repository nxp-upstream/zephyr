/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_object.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_transform.h>

LOG_MODULE_REGISTER(mp_transform, CONFIG_MP_LOG_LEVEL);

#define MP_PAD_SINK_ID 0
#define MP_PAD_SRC_ID  1

static int mp_transform_chainfn(struct mp_pad *pad, struct net_buf *in_buf,
				struct net_buf **out_buf)
{
	ARG_UNUSED(pad);

	/* Default implementation for MP_MODE_PASSTHROUGH - return same buffer */
	*out_buf = in_buf;
	return 0;
}

int mp_transform_set_caps(struct mp_transform *transform, enum mp_pad_direction direction,
			  const struct mp_structure *caps)
{
	if (direction == MP_PAD_SINK) {
		return mp_pad_set_caps(&transform->sinkpad, caps);
	}

	if (direction == MP_PAD_SRC) {
		return mp_pad_set_caps(&transform->srcpad, caps);
	}

	return -EINVAL;
}

static int mp_transform_transform_caps(struct mp_transform *self, enum mp_pad_direction direction,
				       const struct mp_structure *in, uint32_t index,
				       struct mp_structure *out)
{
	ARG_UNUSED(self);
	ARG_UNUSED(direction);

	/* Passthrough by default: the capability crosses the element unchanged */
	if (index > 0U) {
		return -ENOENT;
	}

	return mp_structure_duplicate(in, out);
}

/*
 * Produce the transformation at or after *index, skipping the indices that
 * yield nothing, and report through *index the one it settled on so the caller
 * can resume past it.
 */
static int mp_transform_enum_caps(struct mp_transform *self, enum mp_pad_direction direction,
				  const struct mp_structure *in, uint32_t *index,
				  struct mp_structure *out)
{
	int ret;

	for (;; (*index)++) {
		ret = self->transform_caps(self, direction, in, *index, out);
		if (ret != -EAGAIN) {
			return ret;
		}
	}
}

/*
 * Transform the peer's answer back to this pad's side and narrow it by the
 * candidate the attempt started from. The answer may map back to several
 * capabilities, so they are walked and the first that still contains the
 * candidate wins.
 */
static int mp_transform_narrow_to_candidate(struct mp_transform *self, struct mp_pad *this_pad,
					    const struct mp_structure *answer,
					    const struct mp_structure *candidate,
					    struct mp_structure *out)
{
	struct mp_structure back;
	int ret;

	for (uint32_t index = 0;; index++) {
		ret = mp_transform_enum_caps(self, this_pad->direction, answer, &index, &back);
		if (ret != 0) {
			return -ENODATA;
		}

		/* Narrow by the candidate this attempt started from */
		ret = mp_structure_intersect(&back, candidate, out);
		if (ret == 0) {
			return 0;
		}
	}
}

/*
 * Offer one transformation of a candidate to the peer on the other side and
 * keep what comes back when the peer accepts it. Reports -ENODATA when the peer
 * refuses it or its answer does not lead back to the candidate, which is the
 * caller's cue to offer the next transformation.
 */
static int mp_transform_offer(struct mp_transform *self, struct mp_pad *this_pad,
			      struct mp_pad *other_pad, const struct mp_structure *candidate,
			      const struct mp_structure *transformed, struct mp_dispatch *query,
			      struct mp_structure *out)
{
	int ret;

	/* Query the peer pad with the transformed caps */
	ret = mp_dispatch_set_caps(query, transformed);
	if (ret < 0) {
		return ret;
	}

	ret = mp_pad_query(other_pad->peer, query);
	if (ret < 0) {
		/*
		 * Capabilities are offered one at a time, so a peer refusing one
		 * of them is an ordinary step of the negotiation and not a
		 * failure. The source reports the error if none is accepted.
		 */
		LOG_DBG("element id = %u: peer refused the transformed caps",
			self->element.object.id);
		return -ENODATA;
	}

	if (mp_structure_is_empty(mp_dispatch_get_caps(query))) {
		return -ENODATA;
	}

	ret = mp_transform_narrow_to_candidate(self, this_pad, mp_dispatch_get_caps(query),
					       candidate, out);
	if (ret != 0) {
		return ret;
	}

	/*
	 * Keep the peer's answer at other_pad: the caps event needs it to narrow
	 * the transformations of the fixated capability back down. Published only
	 * now that the attempt has succeeded, so a refused candidate leaves the
	 * pad as it found it.
	 */
	return mp_pad_set_caps(other_pad, mp_dispatch_get_caps(query));
}

/*
 * Offer one of this pad's capabilities across the element. The element may map
 * it to several capabilities on the other side, so those are offered to the
 * peer one at a time as well. Reports -ENODATA when this candidate leads
 * nowhere, which is the caller's cue to offer the next one.
 */
static int mp_transform_try_candidate(struct mp_transform *self, struct mp_pad *this_pad,
				      struct mp_pad *other_pad,
				      const struct mp_structure *candidate,
				      struct mp_dispatch *query, struct mp_structure *out)
{
	struct mp_structure transformed;
	int ret;

	for (uint32_t index = 0;; index++) {
		ret = mp_transform_enum_caps(self, other_pad->direction, candidate, &index,
					     &transformed);
		if (ret == -ENOENT) {
			return -ENODATA;
		}

		if (ret != 0) {
			return ret;
		}

		ret = mp_transform_offer(self, this_pad, other_pad, candidate, &transformed, query,
					 out);
		if (ret != -ENODATA) {
			return ret;
		}
	}
}

static inline int mp_transform_query_caps(struct mp_transform *self,
					  enum mp_pad_direction direction,
					  struct mp_dispatch *query)
{
	struct mp_pad *this_pad, *other_pad;
	struct mp_structure candidate;
	struct mp_structure result;
	struct mp_structure filter;
	int ret;

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
	 * Keep a copy of the incoming filter: offering a candidate to the peer
	 * reuses the query and overwrites its caps.
	 */
	ret = mp_structure_duplicate(mp_dispatch_get_caps(query), &filter);
	if (ret != 0) {
		return ret;
	}

	for (uint32_t index = 0;; index++) {
		ret = mp_pad_enum_caps(this_pad, index, &filter, &candidate);
		if (ret == -EAGAIN) {
			continue;
		}

		if (ret != 0) {
			ret = -ENODATA;
			break;
		}

		ret = mp_transform_try_candidate(self, this_pad, other_pad, &candidate, query,
						 &result);
		if (ret != -ENODATA) {
			break;
		}
	}

	if (ret != 0) {
		return ret;
	}

	/* Answer the upstream query */
	ret = mp_dispatch_set_caps(query, &result);

	return ret;
}

static int mp_transform_query(struct mp_pad *pad, struct mp_dispatch *query)
{
	struct mp_transform *self = (struct mp_transform *)pad->object.container;
	int ret;

	switch (query->type) {
	case MP_DISPATCH_CAPS:
		return mp_transform_query_caps(self, pad->direction, query);
	case MP_DISPATCH_BUFFER_CONFIG:
		struct mp_dispatch peer_query;

		mp_dispatch_buffer_config_init(&peer_query, &self->srcpad.caps);

		/* Query the downstream */
		ret = mp_pad_query(self->srcpad.peer, &peer_query);
		if (ret < 0) {
			mp_dispatch_clear(&peer_query);
			return ret;
		}

		/* Decide allocation for downstream */
		if (self->decide_allocation != NULL) {
			ret = self->decide_allocation(self, &peer_query);
			if (ret < 0) {
				mp_dispatch_clear(&peer_query);
				return ret;
			}
		}

		mp_dispatch_clear(&peer_query);

		/* Configure/start the output buffer pool */
		if (self->mode == MP_MODE_NORMAL) {
			ret = mp_buffer_pool_configure(self->outpool, &self->srcpad.caps);
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to configure output transform buffer pool");
				return ret;
			}

			ret = mp_buffer_pool_start(self->outpool);
			if (ret != 0 && ret != -ENOSYS) {
				LOG_ERR("Failed to start output transform buffer pool");
				return ret;
			}
		}

		/* Propose allocation to upstream */
		if (self->propose_allocation != NULL) {
			return self->propose_allocation(self, query);
		}

		return 0;
	default:
		return -ENOTSUP;
	}
}

/*
 * Cross a fixed capability over to the other pad at caps event time. A
 * transformation unfixes it again, so every transformation is narrowed by the
 * peer's answer kept at other_pad during the caps query and the first that
 * survives is fixated.
 */
static int mp_transform_cross_caps(struct mp_transform *self, struct mp_pad *other_pad,
				   const struct mp_structure *in, struct mp_structure *out)
{
	struct mp_structure transformed;
	struct mp_structure narrowed;
	int ret;

	for (uint32_t index = 0;; index++) {
		ret = mp_transform_enum_caps(self, other_pad->direction, in, &index, &transformed);
		if (ret == -ENOENT) {
			return -ENODATA;
		}

		if (ret != 0) {
			return ret;
		}

		ret = mp_structure_intersect(&transformed, &other_pad->caps, &narrowed);
		if (ret != 0) {
			continue;
		}

		ret = mp_structure_fixate(&narrowed, out);
		if (ret == 0) {
			return 0;
		}
	}
}

static int mp_transform_event(struct mp_pad *pad, struct mp_dispatch *event)
{
	int ret;

	switch (event->type) {
	case MP_DISPATCH_EOS:
		LOG_DBG("MP_DISPATCH_EOS");
		return mp_pad_send_event_default(pad, event);
	case MP_DISPATCH_CAPS:
		LOG_DBG("MP_DISPATCH_CAPS");
		struct mp_transform *transform = (struct mp_transform *)pad->object.container;
		struct mp_pad *other_pad;
		struct mp_structure incoming;
		struct mp_structure fixated;

		other_pad =
			(pad->direction == MP_PAD_SINK) ? &transform->srcpad : &transform->sinkpad;

		/*
		 * A caps event carries a fixed format. One that constrains
		 * nothing means the source could not fixate, and there is
		 * nothing to cross over.
		 */
		if (mp_structure_is_any(mp_dispatch_get_caps(event))) {
			return -EINVAL;
		}

		/*
		 * Take a copy: the event's own capability is replaced below with
		 * what crosses to the other side, but this side still has to be
		 * applied afterwards.
		 */
		ret = mp_structure_duplicate(mp_dispatch_get_caps(event), &incoming);
		if (ret != 0) {
			return ret;
		}

		ret = mp_transform_cross_caps(transform, other_pad, &incoming, &fixated);
		if (ret != 0) {
			return ret;
		}

		ret = mp_dispatch_set_caps(event, &fixated);
		if (ret == 0) {
			ret = mp_pad_send_event(other_pad->peer, event);
		}

		/*
		 * Apply this side only once the peer has accepted, and only then
		 * the other side: a capsfilter drops itself out of the graph from
		 * set_caps(), so the event must already have been forwarded.
		 */
		if (ret == 0) {
			ret = transform->set_caps(transform, pad->direction, &incoming);
		}

		if (ret == 0) {
			ret = transform->set_caps(transform, other_pad->direction, &fixated);
		}

		return ret;
	default:
		return -ENOTSUP;
	}
}

enum mp_state_change_return mp_transform_change_state(struct mp_element *self,
						      enum mp_state_change transition)
{
	switch (transition) {
	case MP_STATE_CHANGE_PAUSED_TO_READY:
		mp_element_reset_pad_caps(self);
		break;
	default:
		break;
	}

	return MP_STATE_CHANGE_SUCCESS;
}

void mp_transform_init(struct mp_element *self)
{
	struct mp_transform *transform = (struct mp_transform *)self;

	mp_pad_init(&transform->sinkpad, MP_PAD_SINK_ID, MP_PAD_SINK, MP_PAD_ALWAYS);
	mp_pad_init(&transform->srcpad, MP_PAD_SRC_ID, MP_PAD_SRC, MP_PAD_ALWAYS);
	mp_element_add_pad(self, &transform->sinkpad);
	mp_element_add_pad(self, &transform->srcpad);

	self->change_state = mp_transform_change_state;

	transform->mode = MP_MODE_PASSTHROUGH;
	transform->set_caps = mp_transform_set_caps;
	transform->transform_caps = mp_transform_transform_caps;
	transform->sinkpad.chainfn = mp_transform_chainfn;
	transform->sinkpad.queryfn = mp_transform_query;
	transform->srcpad.queryfn = mp_transform_query;
	transform->sinkpad.eventfn = mp_transform_event;
	transform->srcpad.eventfn = mp_transform_event;
	transform->decide_allocation = NULL;
	transform->propose_allocation = NULL;
}
