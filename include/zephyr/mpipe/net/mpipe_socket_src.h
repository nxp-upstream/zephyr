/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Socket source element of the mpipe net plugin.
 * @ingroup mpipe_net
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_SOCKET_SRC_H_
#define ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_SOCKET_SRC_H_

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_src.h>
#include <zephyr/mpipe/net/mpipe_net.h>

/** @addtogroup mpipe_net */
/** @{ */

/** Socket source property identifiers, extending @ref mpipe_prop_src. */
enum mpipe_prop_socket_src {
	/**
	 * Connected socket to read from (const int *), -1 for none.
	 *
	 * The element owns the descriptor from here on: it reads it while
	 * PLAYING, reports the end of stream when the peer closes, and closes
	 * it on PAUSED to READY or when another descriptor replaces it. Not
	 * settable while PLAYING (-EBUSY).
	 */
	MPIPE_PROP_SOCKET_SRC_FD = MPIPE_PROP_SRC_LAST,
	/** Last socket source property, where a derived element starts its own */
	MPIPE_PROP_SOCKET_SRC_LAST,
};

/**
 * @brief Socket source: emits what a connected socket delivers.
 *
 * The stream is received into chunks of the element's own pool, and each read
 * is pushed downstream as a view of the chunk it landed in (see
 * @ref mpipe_buffer_view), so a frame that fits in a chunk reaches its consumer
 * without a copy. A chunk returns to the pool once the last view of it is
 * released.
 */
struct mpipe_socket_src {
	/** Base source element. */
	struct mpipe_src src;
	/** Pool of the chunks the stream is received into. */
	struct mpipe_buffer_pool pool;
	/** Chunk being filled, or NULL. */
	struct net_buf *chunk;
	/** Socket being read, -1 when none. */
	int fd;
};

/**
 * @brief Initialize a socket source.
 *
 * @param ssrc Element to initialize.
 * @param id   Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_socket_src_init(struct mpipe_socket_src *ssrc, uint8_t id);

/**
 * @brief Set a socket source property.
 *
 * A derived element falls back to this for the keys it does not handle.
 *
 * @param obj The element.
 * @param key Property identifier, an @ref mpipe_prop_socket_src or a base one.
 * @param val Pointer to the value.
 *
 * @retval 0        Success
 * @retval -EINVAL  @p val is NULL
 * @retval -EBUSY   The socket cannot change while PLAYING
 * @retval -ENOTSUP Unknown property
 */
int mpipe_socket_src_set_property(struct mpipe_object *obj, uint32_t key, const void *val);

/**
 * @brief Get a socket source property.
 *
 * @param obj The element.
 * @param key Property identifier, an @ref mpipe_prop_socket_src or a base one.
 * @param val Where to write the value.
 *
 * @retval 0        Success
 * @retval -ENOTSUP Unknown property
 */
int mpipe_socket_src_get_property(struct mpipe_object *obj, uint32_t key, void *val);

/**
 * @brief State change of a socket source.
 *
 * An element deriving from the socket source chains to this from its own
 * state change hook: it closes the socket and releases the chunk being filled
 * on PAUSED to READY, after the base source teardown.
 *
 * @param self       The element.
 * @param transition The state transition.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_socket_src_change_state(struct mpipe_element *self, enum mpipe_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_SOCKET_SRC_H_ */
