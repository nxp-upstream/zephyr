/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sink client: a sink whose work runs on another core.
 * @ingroup mpipe_sink
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_SINK_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_SINK_CLIENT_H_

/**
 * @addtogroup mpipe_sink
 * @{
 */

#include <stdint.h>

#include <zephyr/kernel.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_rpc_channel.h>
#include <zephyr/mpipe/mpipe_sink.h>

/**
 * @brief Sink client element structure
 *
 * Base structure for the sinks whose consumer is on another core: a display,
 * a file or a network link that core owns, alone or behind elements of its
 * own. The stream ends here as far as this pipeline is concerned.
 *
 * Every call to the server is synchronous: a buffer is handed over until the
 * other core has consumed it, and the end of stream until it has flushed, so
 * the element reports the end of stream after the remote consumer saw it.
 *
 * The other core reads the payload by address. An input that is not in the
 * element's pool, because nothing upstream adopted it, is copied into it
 * first.
 */
struct mpipe_sink_client {
	/** Base sink structure */
	struct mpipe_sink sink;
	/** Pool the inputs must live in, set by the derived element */
	struct mpipe_buffer_pool *in_pool;
	/** Channel to the server, set by the derived element before the element is initialized */
	struct mpipe_rpc_channel *channel;
	/** The base element's event handler, called after the server heard the event */
	int (*base_event_fn)(struct mpipe_pad *pad, struct mpipe_dispatch *event);
};

/**
 * @brief Initialize a sink client element
 *
 * Initializes the base sink element and installs the processing function that
 * hands a buffer over the transport instead of consuming it locally.
 *
 * The channel must already be set when this is called: it is opened before
 * the base element is initialized, so a server that cannot be reached is
 * reported through the return value and nothing is left half-built.
 *
 * The server is taken through every state transition but READY to PAUSED: a
 * server that negotiates does so when the element applies its format, which
 * happens later in that transition.
 *
 * @param sink_client Pointer to the @ref mpipe_sink_client to initialize.
 * @param id          Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_sink_client_init(struct mpipe_sink_client *sink_client, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_SINK_CLIENT_H_ */
