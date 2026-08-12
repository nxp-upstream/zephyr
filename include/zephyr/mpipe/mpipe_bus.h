/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Message Bus header file.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_BUS_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_BUS_H_

/**
 * @defgroup mpipe_bus Message Bus
 * @ingroup mpipe_framework
 * @brief Asynchronous element-to-application message channel.
 *
 * The bus is the one-way communication channel that carries out-of-band
 * notifications (end-of-stream, error, ...) from the elements inside a
 * pipeline up to the controlling application. It follows the producer/consumer contract.
 *
 * @section mpipe_bus_ownership Ownership and lifetime
 *
 * A bus is embedded in (and owned by) the top-level bin, i.e. the pipeline; it
 * is initialized by mpipe_bus_init() as part of pipeline construction and lives
 * exactly as long as the pipeline. Callers never allocate or free a bus
 * directly. An element obtains its pipeline's bus with mpipe_element_get_bus(),
 * which walks up the parent chain to the top-level bin. Messages posted by any
 * element in the hierarchy therefore converge on the single pipeline bus.
 *
 * @section mpipe_bus_producers Producers: elements only
 *
 * Only elements post to the bus, through mpipe_message_post(): the message names
 * its origin element, and the helper locates the bus from it. A failure
 * carries its domain and code in the message it initializes. The
 * generic layer underneath is mpipe_bus_post(), for a poster that is not an
 * element but already holds the bus: the framework internals and the tests
 * post that way.
 *
 * The application must NOT post to the bus it also consumes. Doing so runs the
 * sync handler inline in the application's own thread and turns the app into
 * both producer and consumer of the same queue, which invites re-entrancy and
 * self-deadlock (a full queue can never drain if the only reader is blocked
 * posting).
 *
 * @section mpipe_bus_consumers Consumers: the application reads
 *
 * The application obtains the bus (mpipe_element_get_bus()) and reads messages
 * with mpipe_bus_pop_msg() / mpipe_bus_pop() (blocking) or mpipe_bus_peek()
 * (non-blocking), typically from its own thread. The consumer runs in its own
 * context, decoupled in time and thread from the posting element, so it may
 * safely drive heavy work such as tearing the pipeline down.
 *
 * @section mpipe_bus_sync_handler The sync handler
 *
 * A bus has at most one sync handler, installed with mpipe_bus_set_sync_handler().
 * It runs synchronously, in the context of the thread that posted the message,
 * before mpipe_bus_post() returns, and BEFORE the message is enqueued for the
 * asynchronous consumer. Its return value (see @ref mpipe_bus_sync_reply) decides
 * the fate of the message:
 *   - MPIPE_BUS_DROP  : discard the message; it is NOT enqueued and the consumer
 *                    never sees it.
 *   - MPIPE_BUS_PASS  : enqueue the message so the consumer can pop it.
 *   - MPIPE_BUS_ASYNC : reserved for future use; currently treated like PASS.
 *
 * Because it runs in the posting (pipeline) thread, the sync handler must be
 * short and must not block or trigger pipeline state changes. Its intended use
 * is filtering/aggregation. For example, a pipeline with N sinks emits N EOS
 * messages; the sync handler drops the first N-1 and passes only the last, so
 * the application observes a single, pipeline-level end-of-stream instead of
 * one per branch.
 *
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>

#include <zephyr/mpipe/mpipe_message.h>

/**
 * @brief Return value of a bus sync handler, deciding a message's fate.
 *
 * The sync handler runs inline in the posting thread and its reply controls
 * whether the message is subsequently enqueued for the asynchronous consumer.
 */
enum mpipe_bus_sync_reply {
	/** Discard the message: do not enqueue it, the consumer never sees it. */
	MPIPE_BUS_DROP = 0,
	/** Keep the message: enqueue it so the consumer can pop it. */
	MPIPE_BUS_PASS = 1,
	/**
	 * Reserved for a future asynchronous-dispatch mode. Currently treated
	 * exactly like MPIPE_BUS_PASS (the message is enqueued).
	 */
	MPIPE_BUS_ASYNC = 2,
};

/**
 * @brief Bus sync handler callback type.
 *
 * Invoked synchronously by mpipe_bus_post() in the posting thread's context,
 * before the message is enqueued. Keep it short and non-blocking; do not change
 * pipeline state from it.
 *
 * @param bus       The bus the message was posted to.
 * @param message   The message being posted.
 * @param user_data User data supplied to mpipe_bus_set_sync_handler().
 *
 * @return One of @ref mpipe_bus_sync_reply.
 */
typedef enum mpipe_bus_sync_reply (*mpipe_bus_sync_handler_fn)(struct mpipe_bus *bus,
							       struct mpipe_message *message,
							       void *user_data);

/**
 * @struct mpipe_bus
 * Message bus structure.
 *
 * Treat all fields as internal; use the mpipe_bus_*() API. The bus is embedded in
 * the top-level bin (pipeline) and shares its lifetime.
 */
struct mpipe_bus {
	/** Message queue used to store messages until the consumer pops them. */
	struct k_msgq msgq;
	/** Backing buffer for the message queue. */
	char msgq_buf[sizeof(struct mpipe_message) * CONFIG_MPIPE_BUS_QUEUE_DEPTH]
		__aligned(__alignof__(struct mpipe_message));
	/** Optional single sync handler, or NULL if none is installed. */
	mpipe_bus_sync_handler_fn sync_handler;
	/** User data passed to the sync handler. */
	void *sync_handler_user_data;
};

/**
 * @brief Initialize a message bus.
 *
 * Called as part of pipeline construction; the bus then lives as long as its
 * owning pipeline. No sync handler is installed initially.
 *
 * @param bus Pointer to the bus to initialize.
 */
static inline void mpipe_bus_init(struct mpipe_bus *bus)
{
	k_msgq_init(&bus->msgq, bus->msgq_buf, sizeof(struct mpipe_message),
		    CONFIG_MPIPE_BUS_QUEUE_DEPTH);
	bus->sync_handler = NULL;
	bus->sync_handler_user_data = NULL;
}

/**
 * @brief Post a message to the bus (low-level).
 *
 * Runs the sync handler (if any) inline in the caller's thread, then enqueues
 * the message unless the handler returned MPIPE_BUS_DROP. Elements should prefer
 * mpipe_message_post(), which finds the bus from the message's origin.
 *
 * @param bus Pointer to the bus.
 * @param message Pointer to the message to post.
 * @retval 0 on success (message queued, or dropped by the sync handler)
 * @retval -EINVAL if @p bus or @p message is NULL
 * @retval -ENOMSG if the queue is full
 */
int mpipe_bus_post(struct mpipe_bus *bus, struct mpipe_message *message);

/**
 * @brief Pop a message from the bus matching a given type filter.
 *
 * Blocks until a message matching @p filter_mask is available.
 *
 * @param bus Pointer to the bus.
 * @param filter_mask Message type filter mask.
 * @param out Pointer to output message buffer.
 * @retval 0 on success
 * @retval -EINVAL if @p bus or @p out is NULL
 */
int mpipe_bus_pop_msg(struct mpipe_bus *bus, uint32_t filter_mask, struct mpipe_message *out);

/**
 * @brief Pop any message from the bus.
 *
 * Blocks until a message is available.
 *
 * @param bus Pointer to the bus.
 * @param out Pointer to output message buffer.
 * @retval 0 on success
 * @retval -EINVAL if @p bus or @p out is NULL
 */
int mpipe_bus_pop(struct mpipe_bus *bus, struct mpipe_message *out);

/**
 * @brief Peek at the head message without removing it.
 *
 * @param bus Pointer to the bus.
 * @param out Pointer to output message buffer.
 * @retval 0 on success
 * @retval -EINVAL if @p bus or @p out is NULL
 * @retval -ENOMSG if the queue is empty
 */
int mpipe_bus_peek(struct mpipe_bus *bus, struct mpipe_message *out);

/**
 * @brief Flush all messages from the bus.
 *
 * @param bus Pointer to the bus.
 * @retval 0 on success
 * @retval -EINVAL if @p bus is NULL
 */
int mpipe_bus_flush(struct mpipe_bus *bus);

/**
 * @brief Install (or clear) the bus sync handler.
 *
 * A bus has at most one sync handler; installing a new one replaces any
 * previous one. Pass @p handler as NULL to clear it. See
 * @ref mpipe_bus_sync_handler for the execution context and constraints.
 *
 * @param bus Pointer to the bus.
 * @param handler The sync handler to install, or NULL to clear.
 * @param user_data User data passed to the handler on each invocation.
 * @retval 0 on success
 * @retval -EINVAL if @p bus is NULL
 */
int mpipe_bus_set_sync_handler(struct mpipe_bus *bus, mpipe_bus_sync_handler_fn handler,
			       void *user_data);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_BUS_H_ */
