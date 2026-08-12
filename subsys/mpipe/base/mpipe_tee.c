/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_pipeline.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/base/mpipe_tee.h>

LOG_MODULE_REGISTER(mpipe_tee, CONFIG_MPIPE_LOG_LEVEL);

#define DEFAULT_SRC_PADS_NUM 2

static int mpipe_tee_sink_queryfn(struct mpipe_pad *pad, struct mpipe_dispatch *query)
{
	struct mpipe_tee *tee = (struct mpipe_tee *)pad->object.container;

	switch (query->type) {
	case MPIPE_DISPATCH_CAPS: {
		struct mpipe_structure filter;
		struct mpipe_structure answer;
		bool answered = false;
		int ret;

		ret = mpipe_structure_duplicate(mpipe_dispatch_get_caps(query), &filter);
		if (ret != 0) {
			return ret;
		}

		for (uint8_t i = 0; i < tee->src_pads_num; i++) {
			if (tee->src_pads[i].peer == NULL) {
				continue;
			}

			ret = mpipe_dispatch_set_caps(query, &filter);
			if (ret != 0) {
				return ret;
			}

			ret = mpipe_pad_query(tee->src_pads[i].peer, query);
			if (ret != 0) {
				return ret;
			}

			if (!answered) {
				ret = mpipe_structure_duplicate(mpipe_dispatch_get_caps(query),
								&answer);
				answered = true;
			} else {
				struct mpipe_structure intersected;

				ret = mpipe_structure_intersect(
					&answer, mpipe_dispatch_get_caps(query), &intersected);
				if (ret == 0) {
					ret = mpipe_structure_duplicate(&intersected, &answer);
				}
			}

			if (ret != 0) {
				return ret;
			}
		}

		return answered ? mpipe_dispatch_set_caps(query, &answer) : 0;
	}
	case MPIPE_DISPATCH_BUFFER_CONFIG: {
		struct mpipe_buffer_pool_config merged = {0};

		for (uint8_t i = 0; i < tee->src_pads_num; i++) {
			if (tee->src_pads[i].peer == NULL) {
				continue;
			}

			int ret = mpipe_pad_query(tee->src_pads[i].peer, query);

			if (ret != 0) {
				return ret;
			}

			/* Get pool configs from pool or standalone config */
			struct mpipe_buffer_pool *pool = mpipe_dispatch_get_pool(query);

			struct mpipe_buffer_pool_config *cfg =
				(pool != NULL) ? &pool->config
					       : mpipe_dispatch_get_pool_config(query);

			/* Combine all downstream branch's pool config proposals */
			if (cfg != NULL) {
				merged.size = MAX(merged.size, cfg->size);
				merged.min_buffers = MAX(merged.min_buffers, cfg->min_buffers);
				int align = sys_lcm(merged.align, cfg->align);

				if (align == 0 && cfg->align != 0) {
					merged.align = cfg->align;
				} else {
					merged.align = align;
				}
			}
		}
		/*
		 * Discard all downstream pool proposals.
		 * Upstream will use its own pool; if a downstream branch cannot
		 * use the buffer, it will need to copy into its own pool.
		 */
		mpipe_dispatch_set_pool_config(query, &merged);

		return 0;
	}
	default:
		return -ENOTSUP;
	}
}

static int mpipe_tee_sink_eventfn(struct mpipe_pad *pad, struct mpipe_dispatch *event)
{
	struct mpipe_tee *tee = (struct mpipe_tee *)pad->object.container;
	int ret = 0;
	int first_err = 0;

	switch (event->type) {
	case MPIPE_DISPATCH_CAPS:
	case MPIPE_DISPATCH_EOS: {
		struct mpipe_structure evt_caps;
		bool is_caps = (event->type == MPIPE_DISPATCH_CAPS);

		if (is_caps) {
			ret = mpipe_structure_duplicate(mpipe_dispatch_get_caps(event), &evt_caps);
			if (ret != 0) {
				return ret;
			}
		}

		for (uint8_t i = 0; i < tee->src_pads_num; i++) {
			if (tee->src_pads[i].peer == NULL) {
				continue;
			}

			if (is_caps) {
				ret = mpipe_dispatch_set_caps(event, &evt_caps);
				if (ret != 0 && first_err == 0) {
					first_err = ret;
					continue;
				}

				ret = mpipe_pad_set_caps(&tee->src_pads[i], &evt_caps);
				if (ret != 0 && first_err == 0) {
					first_err = ret;
				}
			}

			ret = mpipe_pad_send_event(tee->src_pads[i].peer, event);
			if (ret != 0 && first_err == 0) {
				first_err = ret;
			}
		}

		if (is_caps && first_err == 0) {
			first_err = mpipe_pad_set_caps(pad, &evt_caps);
		}

		return first_err;
	}
	default:
		return -ENOTSUP;
	}
}

static int mpipe_tee_chainfn(struct mpipe_pad *pad, struct net_buf *in_buf,
			     struct net_buf **out_buf)
{
	struct mpipe_tee *tee = (struct mpipe_tee *)pad->object.container;
	uint8_t i = 0;
	int first_err = 0;
	int ret;

	*out_buf = NULL;

	for (i = 0; i < tee->src_pads_num; i++) {
		/* The push consumes the branch's reference, success or failure */
		ret = mpipe_push_buffer(&tee->src_pads[i], net_buf_ref(in_buf));
		if (ret != 0) {
			LOG_ERR("Tee pushes to src_pad[%u] failed (%d)", i, ret);
			if (first_err == 0) {
				first_err = ret;
			}
		}
	}

	net_buf_unref(in_buf);

	return first_err;
}

static enum mpipe_state_change_return mpipe_tee_change_state(struct mpipe_element *self,
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

static int mpipe_tee_add_src_pad(struct mpipe_tee *tee)
{
	if (tee->src_pads_num >= CONFIG_MPIPE_BASE_TEE_MAX_SRC_PADS_NUM) {
		return -EINVAL;
	}

	mpipe_pad_init(&tee->src_pads[tee->src_pads_num], tee->src_pads_num, MPIPE_PAD_SRC,
		       MPIPE_PAD_ALWAYS);
	mpipe_element_add_pad(&tee->element, &tee->src_pads[tee->src_pads_num]);
	tee->src_pads_num++;

	return 0;
}

static int mpipe_tee_get_property(struct mpipe_object *obj, uint32_t id, void *val)
{
	struct mpipe_tee *tee = (struct mpipe_tee *)obj;

	switch (id) {
	case MPIPE_PROP_BASE_TEE_SRC_PADS_NUM:
		*(uint8_t *)val = tee->src_pads_num;

		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mpipe_tee_set_property(struct mpipe_object *obj, uint32_t id, const void *val)
{
	struct mpipe_tee *tee = (struct mpipe_tee *)obj;

	switch (id) {
	case MPIPE_PROP_BASE_TEE_SRC_PADS_NUM: {
		uint8_t requested = *(const uint8_t *)val;

		if (!IN_RANGE(requested, DEFAULT_SRC_PADS_NUM,
			      CONFIG_MPIPE_BASE_TEE_MAX_SRC_PADS_NUM)) {
			return -EINVAL;
		}

		while (tee->src_pads_num < requested) {
			mpipe_tee_add_src_pad(tee);
		}

		return 0;
	}
	default:
		return -ENOTSUP;
	}
}

void mpipe_tee_init(struct mpipe_element *self)
{
	struct mpipe_tee *tee = (struct mpipe_tee *)self;

	self->object.get_property = mpipe_tee_get_property;
	self->object.set_property = mpipe_tee_set_property;
	self->change_state = mpipe_tee_change_state;

	tee->sink_pad.chainfn = mpipe_tee_chainfn;
	tee->sink_pad.queryfn = mpipe_tee_sink_queryfn;
	tee->sink_pad.eventfn = mpipe_tee_sink_eventfn;

	/* Initialize the sink pad */
	mpipe_pad_init(&tee->sink_pad, 0, MPIPE_PAD_SINK, MPIPE_PAD_ALWAYS);
	mpipe_element_add_pad(self, &tee->sink_pad);

	/* Initialize the default source pads */
	tee->src_pads_num = 0;
	while (tee->src_pads_num < DEFAULT_SRC_PADS_NUM) {
		mpipe_tee_add_src_pad(tee);
	}
}
