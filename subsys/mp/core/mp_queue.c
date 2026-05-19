/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_pad.h>
#include <zephyr/mp/core/mp_pipeline.h>
#include <zephyr/mp/core/mp_queue.h>

LOG_MODULE_REGISTER(mp_queue, CONFIG_MP_LOG_LEVEL);

static int mp_queue_set_property(struct mp_object *obj, uint32_t id, const void *val)
{
	struct mp_queue *queue = MP_QUEUE(obj);

	switch (id) {
	case PROP_QUEUE_MAX_SIZE:
		queue->max_size = *(const uint16_t *)val;
		if (queue->max_size > 0) {
			k_sem_init(&queue->sem, queue->max_size, queue->max_size);
		}
		return 0;
	default:
		return -EINVAL;
	}
}

static int mp_queue_get_property(struct mp_object *obj, uint32_t id, void *val)
{
	struct mp_queue *queue = MP_QUEUE(obj);

	switch (id) {
	case PROP_QUEUE_MAX_SIZE:
		*(uint16_t *)val = queue->max_size;
		return 0;
	default:
		return -EINVAL;
	}
}

static int mp_queue_chainfn(struct mp_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct mp_queue *queue = CONTAINER_OF(pad, struct mp_queue, transform.sinkpad);

	/* Backpressure: block if queue is full */
	if (queue->max_size > 0) {
		k_sem_take(&queue->sem, K_FOREVER);
	}

	k_fifo_put(&queue->fifo, in_buf);

	*out_buf = NULL;

	return 0;
}

static void mp_queue_thread_func(void *p1, void *p2, void *p3)
{
	struct mp_queue *queue = p1;
	struct net_buf *buffer;

	queue->thread.running = true;

	while (queue->thread.running) {
		buffer = k_fifo_get(&queue->fifo, K_FOREVER);
		if (buffer == NULL) {
			/* k_fifo_cancel_wait() was called — time to exit */
			break;
		}

		/* Release backpressure: allow upstream to enqueue another buffer */
		if (queue->max_size > 0) {
			k_sem_give(&queue->sem);
		}

		mp_pipeline_push_buffer(MP_ELEMENT(queue), buffer);
	}
}

static enum mp_state_change_return mp_queue_change_state(struct mp_element *element,
							 enum mp_state_change transition)
{
	struct mp_queue *queue = MP_QUEUE(element);

	switch (transition) {
	case MP_STATE_CHANGE_PAUSED_TO_PLAYING:
		mp_thread_create(&queue->thread, mp_queue_thread_func, queue, NULL, NULL,
				 CONFIG_MP_THREAD_DEFAULT_PRIORITY);
		break;
	case MP_STATE_CHANGE_PLAYING_TO_PAUSED:
		/* Unblock the thread if waiting on an empty FIFO */
		k_fifo_cancel_wait(&queue->fifo);
		/* Wait for the thread to exit */
		queue->thread.running = false;
		k_thread_join(&queue->thread.thread_data, K_FOREVER);
		/* Buffers remain in the FIFO — processed on resume */
		break;
	case MP_STATE_CHANGE_PAUSED_TO_READY: {
		struct net_buf *buf;

		/* Drain any remaining buffers from the FIFO */
		while ((buf = k_fifo_get(&queue->fifo, K_NO_WAIT)) != NULL) {
			net_buf_unref(buf);
		}
		/* Reset the backpressure semaphore */
		if (queue->max_size > 0) {
			k_sem_init(&queue->sem, queue->max_size, queue->max_size);
		}
		break;
	}
	default:
		break;
	}

	return MP_STATE_CHANGE_SUCCESS;
}

void mp_queue_init(struct mp_element *self)
{
	struct mp_queue *queue = MP_QUEUE(self);

	mp_transform_init(self);

	self->object.set_property = mp_queue_set_property;
	self->object.get_property = mp_queue_get_property;
	self->change_state = mp_queue_change_state;

	queue->transform.sinkpad.chainfn = mp_queue_chainfn;

	/* Initialize queue-specific fields */
	k_fifo_init(&queue->fifo);
	queue->max_size = 0; /* unlimited by default */
}
