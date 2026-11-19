/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Transform client: a transform whose work runs on another core.
 * @ingroup mpipe_transform
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_

/**
 * @addtogroup mpipe_transform
 * @{
 */

#include <zephyr/kernel.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_rpc_channel.h>
#include <zephyr/mpipe/mpipe_transform.h>

/**
 * @brief Transform client element structure
 *
 * Base structure for all transform elements on the client side of multi-core pipeline.
 * These elements call RPC to the server side to off-load the processing work.
 *
 * The server may run one function or a whole pipeline: the client is one
 * transform either way, and each buffer crosses to the other core once.
 * Every call to the server is synchronous. A queue in front of the element is what lets the
 * elements upstream work while the other core does.
 *
 * An input that is not in the element's input pool, because nothing upstream
 * adopted the pool, is copied into it first: the other core reads the payload
 * by address, so it must sit in the memory that pool allocates from.
 */
struct mpipe_transform_client {
	/** Base transform structure */
	struct mpipe_transform transform;
	/** Channel to the server, set by the derived element before the element is initialized */
	struct mpipe_rpc_channel *channel;
	/** The base element's event handler, called after the server heard the event */
	int (*base_event_fn)(struct mpipe_pad *pad, struct mpipe_dispatch *event);
};

/**
 * @brief Initialize a transform client element
 *
 * Initializes the base transform element and installs the processing function that
 * forwards a buffer over the transport instead of processing it locally.
 *
 * The channel must already be set when this is called: it is opened before
 * the base element is initialized, so a server that cannot be reached is
 * reported through the return value and nothing is left half-built.
 *
 * The server is taken through every state transition but READY to PAUSED: a
 * server that negotiates does so when the element applies its formats, which
 * happens later in that transition.
 *
 * @param transform_client Pointer to the @ref mpipe_transform_client to
 *                         initialize.
 *
 * @param id               Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_transform_client_init(struct mpipe_transform_client *transform_client, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_ */
