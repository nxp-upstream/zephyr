/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief TCP server source element of the mpipe net plugin.
 * @ingroup mpipe_net
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_TCP_SERVER_SRC_H_
#define ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_TCP_SERVER_SRC_H_

#include <zephyr/mpipe/net/mpipe_socket_src.h>

/** @addtogroup mpipe_net */
/** @{ */

/** TCP server source property identifiers, extending @ref mpipe_prop_socket_src. */
enum mpipe_prop_tcp_server_src {
	/** TCP port to listen on (uint16_t). */
	MPIPE_PROP_TCP_SERVER_SRC_PORT = MPIPE_PROP_SOCKET_SRC_LAST,
};

/**
 * @brief TCP server source: emits what one client sends, EOS when it disconnects.
 *
 * A @ref mpipe_socket_src that obtains its socket by itself: it listens on its
 * port on READY to PAUSED, accepts one client on PAUSED to PLAYING and drops
 * it on PLAYING to PAUSED. For an application that accepts several
 * connections on one port, use the socket source directly.
 */
struct mpipe_tcp_server_src {
	/** Socket source reading the accepted client. */
	struct mpipe_socket_src ssrc;
	/** TCP port number. */
	uint16_t port;
	/** Listening socket, -1 when closed. */
	int server_fd;
};

/**
 * @brief Initialize a TCP server source.
 *
 * @param tsrc Element to initialize.
 * @param id   Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_tcp_server_src_init(struct mpipe_tcp_server_src *tsrc, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_TCP_SERVER_SRC_H_ */
