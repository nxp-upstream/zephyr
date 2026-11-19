/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Source client: a source whose buffers come from another core.
 * @ingroup mpipe_src
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_SRC_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_SRC_CLIENT_H_

/**
 * @addtogroup mpipe_src
 * @{
 */

#include <stdint.h>

#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_rpc_channel.h>
#include <zephyr/mpipe/mpipe_src.h>

/**
 * @brief Source client element structure
 *
 * Base structure for the sources whose producer is on another core: a camera
 * or a microphone that core owns, alone or in front of elements of its own.
 * The stream starts here as far as this pipeline is concerned.
 *
 * The buffers are the element's: the pool of the derived element allocates
 * them in memory the other core reaches, and the server fills one at each
 * call. The server waits for a buffer for a bounded time, so the element
 * asks again until one comes, and a stop is never held by a source that has
 * nothing to give.
 */
struct mpipe_src_client {
	/** Base source structure */
	struct mpipe_src src;
	/** Channel to the server, set by the derived element before the element is initialized */
	struct mpipe_rpc_channel *channel;
};

/**
 * @brief Initialize a source client element
 *
 * The channel must already be set when this is called: it is opened before
 * the base element is initialized, so a server that cannot be reached is
 * reported through the return value and nothing is left half-built.
 *
 * The server is taken through every state transition but READY to PAUSED: a
 * server that negotiates does so when the element applies its format, which
 * happens later in that transition.
 *
 * The derived element sets the pool of the source, whose @c acquire_buffer
 * takes a free buffer and has the server fill it with
 * mpipe_src_client_fill().
 *
 * @param src_client Pointer to the @ref mpipe_src_client to initialize.
 * @param id         Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_src_client_init(struct mpipe_src_client *src_client, uint8_t id);

/**
 * @brief Have the server fill one buffer
 *
 * For the @c acquire_buffer of the pool of the derived element. The values
 * returned are those the pipeline expects from the pool of a source.
 *
 * @param src_client The source client.
 * @param buf A free buffer of the element's pool.
 *
 * @retval 0 The buffer holds the next buffer of the server
 * @retval -EAGAIN The server had nothing to give this time: ask again
 * @retval -ENODATA The stream of the server ended
 * @retval -EIO The call to the server failed
 */
int mpipe_src_client_fill(struct mpipe_src_client *src_client, struct net_buf *buf);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_SRC_CLIENT_H_ */
