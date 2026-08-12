/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/mpipe/mpipe_bus.h>
#include <zephyr/mpipe/mpipe_message.h>

/**
 * Run the sync handler for a posted message and decide its fate.
 *
 * The handler (if any) runs inline in the posting thread. Its reply decides
 * whether the message is enqueued for the asynchronous consumer. With no
 * handler installed the default is MPIPE_BUS_PASS (enqueue).
 *
 * @param bus: the bus the message was posted to
 * @param message: the message to handle
 * @return the sync reply, one of enum mpipe_bus_sync_reply
 */
static enum mpipe_bus_sync_reply mpipe_bus_sync_handler(struct mpipe_bus *bus,
							struct mpipe_message *message)
{
	if (bus->sync_handler == NULL) {
		return MPIPE_BUS_PASS;
	}

	return bus->sync_handler(bus, message, bus->sync_handler_user_data);
}

int mpipe_message_post(struct mpipe_message *message)
{
	struct mpipe_bus *bus;

	if (message == NULL || message->origin == NULL || message->type == MPIPE_MESSAGE_UNKNOWN) {
		return -EINVAL;
	}

	bus = mpipe_element_get_bus(message->origin);
	if (bus == NULL) {
		return -ENODEV;
	}

	return mpipe_bus_post(bus, message);
}

int mpipe_bus_post(struct mpipe_bus *bus, struct mpipe_message *message)
{
	enum mpipe_bus_sync_reply reply;

	if (bus == NULL || message == NULL) {
		return -EINVAL;
	}

	/* Step 1: run the sync handler in the caller's thread */
	reply = mpipe_bus_sync_handler(bus, message);

	/*
	 * Step 2: enqueue unless the handler dropped the message. MPIPE_BUS_ASYNC
	 * is reserved for future use and is currently treated like MPIPE_BUS_PASS.
	 */
	if (reply != MPIPE_BUS_DROP) {
		return k_msgq_put(&bus->msgq, message, K_NO_WAIT);
	}

	return 0;
}

int mpipe_bus_pop_msg(struct mpipe_bus *bus, uint32_t filter_mask, struct mpipe_message *out)
{
	struct mpipe_message tmp;
	int ret;

	if (bus == NULL || out == NULL) {
		return -EINVAL;
	}

	while (1) {
		ret = k_msgq_get(&bus->msgq, &tmp, K_FOREVER);
		if (ret != 0) {
			return ret;
		}

		if ((tmp.type & filter_mask) != 0U) {
			*out = tmp;
			return 0;
		}
	}
}

int mpipe_bus_pop(struct mpipe_bus *bus, struct mpipe_message *out)
{
	return mpipe_bus_pop_msg(bus, MPIPE_MESSAGE_ANY, out);
}

int mpipe_bus_peek(struct mpipe_bus *bus, struct mpipe_message *out)
{
	if (bus == NULL || out == NULL) {
		return -EINVAL;
	}

	return k_msgq_peek(&bus->msgq, out);
}

int mpipe_bus_flush(struct mpipe_bus *bus)
{
	if (bus == NULL) {
		return -EINVAL;
	}

	k_msgq_purge(&bus->msgq);

	return 0;
}

int mpipe_bus_set_sync_handler(struct mpipe_bus *bus, mpipe_bus_sync_handler_fn handler,
			       void *user_data)
{
	if (bus == NULL) {
		return -EINVAL;
	}

	bus->sync_handler = handler;
	bus->sync_handler_user_data = user_data;

	return 0;
}
