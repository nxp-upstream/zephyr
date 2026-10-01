/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Socket sink element of the mpipe net plugin.
 * @ingroup mpipe_net
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_SOCKET_SINK_H_
#define ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_SOCKET_SINK_H_

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/net/mpipe_net.h>

/** @addtogroup mpipe_net */
/** @{ */

/** Socket sink property identifiers, extending @ref mpipe_prop_sink. */
enum mpipe_prop_socket_sink {
	/**
	 * Connected socket to write to (const int *), -1 for none.
	 *
	 * The element owns the descriptor from here on: it writes every buffer
	 * to it while PLAYING, fails the stream when the peer is gone, and
	 * closes it on PAUSED to READY or when another descriptor replaces it.
	 * Not settable while PLAYING (-EBUSY).
	 */
	MPIPE_PROP_SOCKET_SINK_FD = MPIPE_PROP_SINK_LAST,
	/** Last socket sink property, where a derived element starts its own */
	MPIPE_PROP_SOCKET_SINK_LAST,
};

/**
 * @brief Socket sink: writes every buffer it receives to a connected socket.
 *
 * The bytes go out as they come, with nothing added: a buffer carrying several
 * frames as a fragment chain is written fragment by fragment, in order, and
 * each fragment goes out whole or not at all. When the stack has no room to
 * send, a fragment not yet started is dropped rather than the stream stalled,
 * as a live stream wants, and one already started is completed, so the peer
 * keeps reading on block boundaries. A socket carries no format, so the sink
 * accepts any.
 */
struct mpipe_socket_sink {
	/** Base sink element. */
	struct mpipe_sink sink;
	/** Socket being written, -1 when none. */
	int fd;
	/** Fragments dropped for want of room to send, since the last start. */
	uint32_t dropped;
};

/**
 * @brief Initialize a socket sink.
 *
 * @param ssink Element to initialize.
 * @param id    Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_socket_sink_init(struct mpipe_socket_sink *ssink, uint8_t id);

/**
 * @brief Set a socket sink property.
 *
 * A derived element falls back to this for the keys it does not handle.
 *
 * @param obj The element.
 * @param key Property identifier, an @ref mpipe_prop_socket_sink or a base one.
 * @param val Pointer to the value.
 *
 * @retval 0        Success
 * @retval -EINVAL  @p val is NULL
 * @retval -EBUSY   The socket cannot change while PLAYING
 * @retval -ENOTSUP Unknown property
 */
int mpipe_socket_sink_set_property(struct mpipe_object *obj, uint32_t key, const void *val);

/**
 * @brief Get a socket sink property.
 *
 * @param obj The element.
 * @param key Property identifier, an @ref mpipe_prop_socket_sink or a base one.
 * @param val Where to write the value.
 *
 * @retval 0        Success
 * @retval -ENOTSUP Unknown property
 */
int mpipe_socket_sink_get_property(struct mpipe_object *obj, uint32_t key, void *val);

/**
 * @brief State change of a socket sink.
 *
 * An element deriving from the socket sink chains to this from its own state
 * change hook: it closes the socket on PAUSED to READY, after the base sink
 * teardown.
 *
 * @param self       The element.
 * @param transition The state transition.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_socket_sink_change_state(struct mpipe_element *self, enum mpipe_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_SOCKET_SINK_H_ */
