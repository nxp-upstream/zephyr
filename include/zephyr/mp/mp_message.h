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
#include <zephyr/sys/util_macro.h>

#include <zephyr/mp/mp_element.h>

/** @brief Message types, usable as bitmask filters. */
enum mp_message_type {
	/** Uninitialized: what a zeroed message carries; cannot be posted */
	MP_MESSAGE_UNKNOWN = 0,
	MP_MESSAGE_EOS = BIT(0),   /**< End of stream */
	MP_MESSAGE_ERROR = BIT(1), /**< Error */
};

/**
 * @brief Filter mask matching any message type.
 *
 * A macro rather than an enumerator: UINT32_MAX does not fit in an int, and
 * a filter mask is a plain uint32_t like the type field it matches against.
 */
#define MP_MESSAGE_ANY UINT32_MAX

/**
 * @brief The domain a failure happened in: what the element was doing.
 *
 * A pipeline fails in a small number of distinct places, and knowing which one
 * is most of the way to knowing why. This names them so a report can say what
 * the element was doing, not only that something went wrong.
 *
 * With the errno in @ref mp_message.code, the pair is what an application
 * branches on:
 *
 * @code{.c}
 * if (msg.domain == MP_ERROR_RESOURCE && msg.code == -ENOENT) {
 *	// the input file does not exist
 * }
 * @endcode
 */
enum mp_error_domain {
	/**
	 * Nothing more specific fits. Also the neutral value: a message that is
	 * not a failure carries it, so a designated initializer that leaves the
	 * domain out is correct.
	 */
	MP_ERROR_FAILED = 0,
	/** Capability negotiation: the formats would not intersect; try another config */
	MP_ERROR_CAPS,
	/** Buffer negotiation: a pool would not configure or start; reduce counts or sizes */
	MP_ERROR_ALLOC,
	/** Buffer flow: a chain, acquire or push failed while streaming; stop or restart */
	MP_ERROR_FLOW,
	/** A device, file or driver refused; check the media or the hardware */
	MP_ERROR_RESOURCE,
	/** One past the last domain; not itself a domain */
	MP_ERROR_DOMAIN_END,
};

/**
 * @brief Message structure carrying type and origin of the message.
 *
 * @ref domain and @ref code describe a failure and are only meaningful on an
 * MP_MESSAGE_ERROR.
 */
struct mp_message {
	struct mp_element *origin; /**< Origin of message */
	uint32_t type;             /**< Message type (see @ref mp_message_type) */
	int32_t code;              /**< errno-style code, negative, 0 if unset */
	uint8_t domain;            /**< What the element was doing, see @ref mp_error_domain */
};

/**
 * @brief Post a message to the bus of the bin holding its origin.
 *
 * Locates the bus from the message's origin via @ref mp_element_get_bus and
 * posts. The bus copies the message by value, so stack storage is fine. This
 * is the way elements post; a poster that is not an element but holds a bus
 * uses mp_bus_post() directly.
 *
 * The caller declares and initializes the message. A field left out of a
 * designated initializer is zero, which is the neutral value for every field:
 * MP_ERROR_FAILED domain, no code.
 *
 * Post an error when a failure is fatal to the stream, which is what an
 * application needs to hear about. A failure the caller can report by
 * returning an errno stays an errno; an error message is not for every
 * failed call.
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
