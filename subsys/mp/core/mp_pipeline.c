/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include <zephyr/mp/core/mp_bus.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_pad.h>
#include <zephyr/mp/core/mp_pipeline.h>
#include <zephyr/mp/core/mp_src.h>

LOG_MODULE_REGISTER(mp_pipeline, CONFIG_MP_LOG_LEVEL);

static int mp_pipeline_set_property(struct mp_object *obj, uint32_t id, const void *val)
{
	return 0;
}

static int mp_pipeline_get_property(struct mp_object *obj, uint32_t id, void *val)
{
	return 0;
}

int mp_pipeline_push_buffer(struct mp_element *start_elem, struct net_buf *buffer)
{
	struct mp_element *cur_elem = start_elem;
	struct mp_pad *cur_srcpad;
	struct mp_pad *next_sinkpad;
	struct net_buf *out_buf;
	sys_dnode_t *srcpad_node;
	int ret;

	if (start_elem == NULL || buffer == NULL) {
		return -EINVAL;
	}

	while (cur_elem != NULL && buffer != NULL) {
		srcpad_node = sys_dlist_peek_head(&cur_elem->srcpads);
		if (srcpad_node == NULL) {
			/* Sink element reached - done */
			break;
		}
		cur_srcpad = CONTAINER_OF(srcpad_node, struct mp_pad, object.node);

		if (cur_srcpad->peer == NULL) {
			LOG_ERR("srcpad has no peer");
			return -ENOTCONN;
		}

		next_sinkpad = cur_srcpad->peer;
		if (next_sinkpad->chainfn != NULL) {
			out_buf = NULL;

			ret = next_sinkpad->chainfn(next_sinkpad, buffer, &out_buf);
			if (ret != 0) {
				LOG_ERR("chainfn failed for element %d (%d)",
					MP_OBJECT(next_sinkpad->object.container)->id, ret);
				if (buffer != NULL) {
					net_buf_unref(buffer);
				}
				return ret;
			}

			if (out_buf == NULL) {
				/* Buffer consumed (e.g. by a queue element) */
				break;
			}

			buffer = out_buf;
		}

		cur_elem = MP_ELEMENT(next_sinkpad->object.container);
	}

	return 0;
}

static void mp_pipeline_thread_func(void *p1, void *p2, void *p3)
{
	struct mp_bin *bin = p1;
	struct mp_pipeline *pipeline = p1;
	struct mp_object *obj;
	struct mp_element *element;
	struct mp_src *src = NULL;
	struct net_buf *buffer = NULL;
	struct mp_message *eos_message = NULL;
	uint32_t count = 0;

	/* Currently, only pipelines without branches and push mode are supported */

	/* Find the 1st source element */
	SYS_DLIST_FOR_EACH_CONTAINER(&bin->children, obj, node) {
		element = MP_ELEMENT(obj);
		if (sys_dlist_is_empty(&element->sinkpads)) {
			src = MP_SRC(element);
			break;
		}
	}

	if (src == NULL) {
		LOG_ERR("No source element found in the pipeline");
		return;
	}

	pipeline->thread.running = true;

	/* Main loop - in push mode, driven by source producing buffers */
	while (pipeline->thread.running) {
		/* Acquire a buffer from source */
		if (src->pool->acquire_buffer != NULL) {
			int ret = src->pool->acquire_buffer(src->pool, &buffer);

			/* End of stream */
			if (ret == -ENODATA) {
				pipeline->thread.running = false;
				eos_message = mp_message_new(MP_MESSAGE_EOS, MP_OBJECT(src), NULL);
				mp_bus_post(&bin->bus, eos_message);
				break;
			}

			if (ret != 0) {
				LOG_ERR("Failed to acquire buffer from source (%d)", ret);
				break;
			}
		}

		/* Push the buffer downstream through the pipeline */
		mp_pipeline_push_buffer(MP_ELEMENT(src), buffer);

		/* Stop the pipeline when reaching the src's num_buffers */
		if (src->num_buffers != 0 && ++count == src->num_buffers) {
			pipeline->thread.running = false;
			eos_message = mp_message_new(MP_MESSAGE_EOS, MP_OBJECT(src), NULL);
			mp_bus_post(&bin->bus, eos_message);
		}
	}
}

static enum mp_state_change_return mp_pipeline_change_state(struct mp_element *element,
							    enum mp_state_change transition)
{
	struct mp_pipeline *pipeline = MP_PIPELINE(element);
	enum mp_state_change_return ret = mp_bin_change_state_func(element, transition);

	if (ret != MP_STATE_CHANGE_SUCCESS) {
		return ret;
	}

	LOG_DBG("Pipeline id %u has successfully changed state to %u", MP_OBJECT(element)->id,
		MP_STATE_TRANSITION_NEXT(transition));

	/* Start the pipeline processing thread */
	if (transition == MP_STATE_CHANGE_PAUSED_TO_PLAYING) {
		mp_thread_create(&pipeline->thread, mp_pipeline_thread_func, element, NULL, NULL,
				 CONFIG_MP_THREAD_DEFAULT_PRIORITY);
	}

	/* Paused the pipeline processing thread */
	if (transition == MP_STATE_CHANGE_PLAYING_TO_PAUSED) {
		pipeline->thread.running = false;
		/* TODO: Wait for thread to finish */
	}

	return ret;
}

void mp_pipeline_init(struct mp_element *self)
{
	/* Init base class */
	mp_bin_init(self);

	self->object.set_property = mp_pipeline_set_property;
	self->object.get_property = mp_pipeline_get_property;
	self->change_state = mp_pipeline_change_state;
}
