/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Network plugin.
 * @ingroup mpipe_net
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_NET_H_
#define ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_NET_H_

/**
 * @defgroup mpipe_net Network
 * @ingroup mpipe_plugins
 * @brief Elements that move a stream over a socket.
 *
 * A socket carries bytes with no format attached, so the source announces no
 * capability and the sink accepts any; the parser that follows the source is
 * what works out the format, as after a file source.
 *
 * The socket source reads a connected socket the application hands it, which
 * is the shape for an application that owns its listening socket and accepts
 * several connections on it. The TCP server source and sink own a port of
 * their own and accept one client, for the single-stream case.
 */

#endif /* ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_NET_H_ */
