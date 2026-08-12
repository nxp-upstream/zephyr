/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_bin.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_BIN_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_BIN_H_

/**
 * @defgroup mpipe_bin Bins
 * @ingroup mpipe_framework
 * @brief Container elements that hold and manage child elements.
 * @{
 */

#include <stdint.h>

#include <zephyr/sys/dlist.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_message.h>

/**
 * @brief Bin structure
 *
 * A container element that can hold multiple child elements.
 *
 * A bin manages the state changes of its children and handles the topology
 * of the pipeline elements within it.
 */
struct mpipe_bin {
	/** Base element structure */
	struct mpipe_element element;
	/** Number of children in the bin */
	int children_num;
	/** List of children elements in the bin */
	sys_dlist_t children;
	/**
	 * @brief The bin's message bus.
	 *
	 * A one-way, out-of-band notification channel that carries
	 * @ref mpipe_message events (end-of-stream, errors) from the elements
	 * up to the application. It is "out-of-band" because it does not travel
	 * along the data path (pads/buffers); it is a side channel for control
	 * and status notifications only.
	 *
	 * Bus is a single zbus channel, owned by the bin and shared by all of
	 * its children. It is created and registered once by
	 * @ref mpipe_bin_init, and torn down by @ref mpipe_bin_deinit_bus.
	 *
	 * A channel involves three roles:
	 *
	 * - Producers: elements post messages with @ref mpipe_message_post, which
	 *   locates the bus from the message's origin element. The message is
	 *   copied by value into the channel, so stack storage is fine. The
	 *   application must not post to a bus that it consumes.
	 * - Consumers: a channel can have one or more consumers, of two kinds:
	 *   - Subscriber (enabled with CONFIG_ZBUS_MSG_SUBSCRIBER): the
	 *     application reads messages from its own thread, blocking until one
	 *     arrives. Use this when the application wants to own the thread that
	 *     handles events. Define a subscriber with @ref ZBUS_MSG_SUBSCRIBER_DEFINE(),
	 *     register it on the channel with zbus_chan_add_obs(&bin.bus.channel, ...),
	 *     and retrieve messages with zbus_sub_wait_msg().
	 *   - Async listener (enabled with ZBUS_ASYNC_LISTENER): the message is
	 *     copied into a FIFO and the callback is later executed in the
	 *     sysworkq context. Use this for short, non-blocking handlers that do
	 *     not need a dedicated thread. Define a listener with
	 *     ZBUS_LISTENER_DEFINE() and register it on the channel with
	 *     zbus_chan_add_obs(&bin.bus.channel, ...).
	 *   Detach any observer with zbus_chan_rm_obs() on teardown; all observers
	 *   MUST be removed before @ref mpipe_bin_deinit_bus.
	 * - Validator: an optional zbus validator, set via
	 *   @ref mpipe_bin_set_bus_validator, runs in the posting thread before
	 *   the message is published, and may drop messages (e.g. collapsing N
	 *   end-of-stream events into one). Keep it short and non-blocking.
	 */
	struct zbus_runtime_channel bus;

	/** @cond INTERNAL_HIDDEN */
	/* Bus channel mutable runtime state: observer bookkeeping, lock, counters. */
	struct zbus_channel_data chan_data;
	/* Bus channel message buffer: holds the last published mpipe_message. */
	struct mpipe_message chan_msg;
	/** @endcond */
};

/**
 * @brief Initialize a bin
 *
 * Initializes the bin structure and sets up the necessary function pointers
 * and data structures.
 *
 * @param self Pointer to the @ref mpipe_element to initialize as a bin
 */
int mpipe_bin_init(struct mpipe_bin *bin, uint8_t id);

/**
 * @brief Initialize and register the bin's bus channel.
 *
 * @param bin           Pointer to the @ref mpipe_bin.
 * @param bus_validator Optional message validator installed on the channel,
 *                      or NULL for no validation.
 * @param user_data     User data associated with the bus,
 *                      retrievable from the validator/observers via
 *                      zbus_chan_user_data(), or NULL if unused.
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_bin_init_bus(struct mpipe_bin *bin, zbus_validator bus_validator, void *user_data);

/**
 * @brief Install a validator and user data on the bin's already-registered bus.
 *
 * The bin initializes and registers its bus in @ref mpipe_bin_init. A wrapping
 * element (e.g. a pipeline) uses this to install its own message validator and
 * user_data without re-initializing or re-registering the channel.
 *
 * @param bin           Pointer to the @ref mpipe_bin.
 * @param bus_validator Optional message validator installed on the channel,
 *                      or NULL for no validation.
 * @param user_data     User data associated with the bus,
 *                      retrievable from the validator/observers via
 *                      zbus_chan_user_data(), or NULL if unused.
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_bin_set_bus_validator(struct mpipe_bin *bin, zbus_validator bus_validator,
				void *user_data);

/**
 * @brief Unregister the bin's bus channel.
 *
 * Removing the channel from the global bus runtime registry.
 * Call this on teardown so the same bin can be re-initialized
 * without corrupting the registry.
 *
 * @note All observers added to the channel MUST already have been removed
 *       before calling this function.
 *
 * @param bin Pointer to the @ref mpipe_bin.
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_bin_deinit_bus(struct mpipe_bin *bin);

/**
 * @brief Add elements to a bin
 *
 * Adds the given element(s) to the bin.
 *
 * An element can only be added to one bin. Element names must be unique within the bin.
 *
 * The function accepts a variable number of elements, terminated by NULL.
 *
 * If the element's pads are linked to other pads, the pads will be unlinked
 * before the element is added to the bin.
 *
 * @param bin Pointer to the @ref mpipe_bin to add elements to
 * @param element First @ref mpipe_element to add
 * @param ... Additional mpipe_element pointers, terminated by NULL
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_bin_add(struct mpipe_bin *bin, struct mpipe_element *element, ...);

/**
 * @brief Bin state change function
 *
 * Handles state changes for the bin by propagating the state change to all
 * child elements in the appropriate order. The bin manages the topology
 * and ensures proper sequencing of state changes.
 *
 * @param element Pointer to the @ref mpipe_element (bin) changing state
 * @param transition The state transition being performed
 *
 * @return State change return value indicating success, failure, or async operation
 */
enum mpipe_state_change_return mpipe_bin_change_state_func(struct mpipe_element *element,
							   enum mpipe_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_BIN_H_ */
