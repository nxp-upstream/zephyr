/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_object.h>
#include <zephyr/mp/mp_pad.h>
#include <zephyr/mp/mp_src.h>
#include <zephyr/mp/mp_structure.h>

LOG_MODULE_REGISTER(mp_src, CONFIG_MP_LOG_LEVEL);

#define MP_PAD_SRC_ID 0

int mp_src_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	struct mp_src *src = (struct mp_src *)obj;

	switch (key) {
	case MP_PROP_SRC_NUM_BUFS:
		src->num_buffers = (uint32_t)(uintptr_t)val;
		return 0;
	default:
		LOG_ERR("Property %d is unknown", key);
		return -ENOTSUP;
	}
}

int mp_src_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	struct mp_src *src = (struct mp_src *)obj;

	switch (key) {
	case MP_PROP_SRC_NUM_BUFS:
		*(uint32_t *)val = src->num_buffers;
		break;
	default:
		LOG_ERR("Property %d is unknown", key);
		return -ENOTSUP;
	}

	return 0;
}

static int mp_src_set_caps(struct mp_src *src, const struct mp_structure *caps)
{
	if (src == NULL) {
		return -EINVAL;
	}

	return mp_pad_set_caps(&src->srcpad, caps);
}

static int mp_src_query(struct mp_pad *pad, struct mp_dispatch *query)
{
	switch (query->type) {
	case MP_DISPATCH_CAPS:
		return mp_pad_answer_caps_query(pad, query);
	default:
		return -ENOTSUP;
	}
}

/*
 * Offer one candidate to the downstream peer and, when the peer accepts it,
 * keep the result on the src pad. Reports -ENODATA when the peer has nothing
 * in common with this candidate, which is the caller's cue to offer the next.
 */
static int mp_src_offer_candidate(struct mp_src *src, const struct mp_structure *candidate)
{
	struct mp_dispatch caps_query;
	int ret;

	/* The dispatch holds its own copy for as long as the query lasts */
	mp_dispatch_caps_init(&caps_query, candidate);

	ret = mp_pad_query(src->srcpad.peer, &caps_query);
	if (ret != 0) {
		mp_dispatch_clear(&caps_query);
		return -ENODATA;
	}

	if (mp_structure_is_empty(mp_dispatch_get_caps(&caps_query))) {
		mp_dispatch_clear(&caps_query);
		return -ENODATA;
	}

	/* Store negotiated (possibly unfixed) caps on the src pad */
	ret = mp_pad_set_caps(&src->srcpad, mp_dispatch_get_caps(&caps_query));
	mp_dispatch_clear(&caps_query);

	return ret;
}

static int mp_src_negotiate(struct mp_src *src)
{
	struct mp_structure candidate;
	struct mp_structure fixated;
	struct mp_dispatch alloc_query;
	struct mp_dispatch caps_event;
	uint32_t index;
	bool is_fixated;
	int ret;

	/*
	 * Offer the supported capabilities one at a time and keep the first one
	 * the peer accepts. Offering them individually is what allows a rejected
	 * candidate to be retried with the next one instead of the whole
	 * negotiation failing, and it keeps the peer's answer down to what one
	 * candidate can match.
	 */
	for (index = 0;; index++) {
		ret = mp_pad_enum_caps(&src->srcpad, index, NULL, &candidate);
		if (ret == -EAGAIN) {
			continue;
		}

		if (ret == -ENOENT) {
			/*
			 * An element with no capability at all cannot negotiate,
			 * which is a caller error rather than a failed negotiation.
			 */
			return (index == 0) ? -EINVAL : -ENODATA;
		}

		if (ret != 0) {
			return ret;
		}

		ret = mp_src_offer_candidate(src, &candidate);
		if (ret == 0) {
			break;
		}

		if (ret != -ENODATA) {
			return ret;
		}
	}

	is_fixated = (mp_structure_fixate(&src->srcpad.caps, &fixated) == 0);

	/*
	 * Push a caps event downstream. The result only matters when a fixated
	 * capability was sent; an ANY event is informational.
	 */
	mp_dispatch_caps_init(&caps_event, is_fixated ? &fixated : NULL);
	ret = mp_pad_send_event(src->srcpad.peer, &caps_event);
	mp_dispatch_clear(&caps_event);

	/* Apply the fixated capability to the element itself */
	if (is_fixated) {
		if (ret == 0) {
			ret = src->set_caps(src, &fixated);
		}

		if (ret != 0) {
			return ret;
		}
	}

	/* Query the peer's allocation proposal */
	mp_dispatch_buffer_config_init(&alloc_query, &src->srcpad.caps);
	ret = mp_pad_query(src->srcpad.peer, &alloc_query);
	if (ret != 0) {
		mp_dispatch_clear(&alloc_query);
		return ret;
	}

	/* Decide the allocation */
	if (src->decide_allocation != NULL) {
		ret = src->decide_allocation(src, &alloc_query);
		mp_dispatch_clear(&alloc_query);
		return ret;
	}

	mp_dispatch_clear(&alloc_query);

	return 0;
}

enum mp_state_change_return mp_src_change_state(struct mp_element *self,
						enum mp_state_change transition)
{
	struct mp_src *src = (struct mp_src *)self;
	enum mp_state_change_return ret = MP_STATE_CHANGE_SUCCESS;
	int pool_ret;

	switch (transition) {
	case MP_STATE_CHANGE_READY_TO_PAUSED:
		/* Perform negotiation */
		if (mp_src_negotiate(src) < 0) {
			LOG_ERR("Negotiation failed");
			return MP_STATE_CHANGE_FAILURE;
		}

		/* Config buffer pool */
		pool_ret = mp_buffer_pool_configure(src->pool, &src->srcpad.caps);
		if (pool_ret != 0 && pool_ret != -ENOSYS) {
			LOG_ERR("Failed to configure source buffer pool");
			return MP_STATE_CHANGE_FAILURE;
		}

		/* Start buffer pool */
		pool_ret = mp_buffer_pool_start(src->pool);
		if (pool_ret != 0 && pool_ret != -ENOSYS) {
			LOG_ERR("Failed to start source buffer pool");
			return MP_STATE_CHANGE_FAILURE;
		}

		break;
	case MP_STATE_CHANGE_PAUSED_TO_READY:
		/*
		 * Stop the buffer pool on teardown. This is the counterpart of
		 * the READY_TO_PAUSED start above and is what makes stop/replay
		 * symmetric: e.g. the video pool issues video_stream_stop() and
		 * releases its driver buffers here so a subsequent replay can
		 * start streaming cleanly. A pool without a stop hook returns
		 * -ENOSYS, which is not an error.
		 */
		pool_ret = mp_buffer_pool_stop(src->pool);
		if (pool_ret != 0 && pool_ret != -ENOSYS) {
			LOG_ERR("Failed to stop source buffer pool");
			return MP_STATE_CHANGE_FAILURE;
		}

		mp_element_reset_pad_caps(self);

		break;
	default:
		break;
	}

	return ret;
}

void mp_src_init(struct mp_element *self)
{
	struct mp_src *src = (struct mp_src *)self;

	mp_pad_init(&src->srcpad, MP_PAD_SRC_ID, MP_PAD_SRC, MP_PAD_ALWAYS);
	mp_element_add_pad(self, &src->srcpad);

	self->object.set_property = mp_src_set_property;
	self->object.get_property = mp_src_get_property;
	self->change_state = mp_src_change_state;

	src->set_caps = mp_src_set_caps;
	src->srcpad.queryfn = mp_src_query;
	src->decide_allocation = NULL;
}
