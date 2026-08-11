/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Bus message
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_MESSAGE_H_
#define ZEPHYR_INCLUDE_MP_MP_MESSAGE_H_

/**
 * @defgroup mp_message Messages
 * @ingroup mp_framework
 * @brief Messages exchanged through the bus.
 * @{
 */

#include <stdint.h>

#include <zephyr/kernel.h>

#include <zephyr/mp/mp_element.h>

/** @brief Filter mask matching any message type. */
#define MP_MESSAGE_ANY UINT32_MAX

/** @brief Message types, usable as bitmask filters. */
enum mp_message_type {
	MP_MESSAGE_UNKNOWN = 0,        /**< Uninitialized */
	MP_MESSAGE_EOS = (1 << 0),     /**< End of stream */
	MP_MESSAGE_ERROR = (1 << 1),   /**< Error */
	MP_MESSAGE_WARNING = (1 << 2), /**< Warning */
};

/**
 * @brief Message structure carrying type and origin of the message.
 */
struct mp_message {
	struct mp_element *origin; /**< Origin of message */
	uint32_t type;             /**< Message type (see @ref mp_message_type) */
};

/**
 * @brief Post a message to the bus of the bin holding its origin.
 *
 * Locates the bus from the message's origin via @ref mp_element_get_bus and
 * posts. The bus copies the message by value, so stack storage is fine. This
 * is the way elements post; a poster that is not an element but holds a bus
 * uses mp_bus_post() directly.
 *
 * @code{.c}
 * struct mp_message msg = {
 *	.origin = &sink->element,
 *	.type = MP_MESSAGE_EOS,
 * };
 *
 * (void)mp_message_post(&msg);
 * @endcode
 *
 * @param message Message to post, with at least its origin and type set.
 *
 * @retval 0 on success
 * @retval -EINVAL if @p message is NULL, its origin is NULL or its type is 0
 * @retval -ENODEV if the origin has no bus
 * @retval -ENOMSG if the bus queue is full
 */
int mp_message_post(struct mp_message *message);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_MP_MESSAGE_H_ */
