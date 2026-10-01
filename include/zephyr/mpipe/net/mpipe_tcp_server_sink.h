/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief TCP server sink element of the mpipe net plugin.
 * @ingroup mpipe_net
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_TCP_SERVER_SINK_H_
#define ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_TCP_SERVER_SINK_H_

#include <zephyr/mpipe/net/mpipe_socket_sink.h>

/** @addtogroup mpipe_net */
/** @{ */

/** TCP server sink property identifiers, extending @ref mpipe_prop_socket_sink. */
enum mpipe_prop_tcp_server_sink {
	/** TCP port to listen on (uint16_t). */
	MPIPE_PROP_TCP_SERVER_SINK_PORT = MPIPE_PROP_SOCKET_SINK_LAST,
};

/**
 * @brief TCP server sink: listens on a port and writes the stream to one client.
 *
 * A socket sink that owns its listening socket: it listens on READY to
 * PAUSED, accepts one client on PAUSED to PLAYING and hands the connection to
 * the socket sink underneath, which writes to it and closes it on teardown.
 */
struct mpipe_tcp_server_sink {
	/** Socket sink writing to the accepted client. */
	struct mpipe_socket_sink ssink;
	/** TCP port number. */
	uint16_t port;
	/** Listening socket, -1 when closed. */
	int server_fd;
};

/**
 * @brief Initialize a TCP server sink.
 *
 * @param tsink Element to initialize.
 * @param id    Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_tcp_server_sink_init(struct mpipe_tcp_server_sink *tsink, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_NET_MPIPE_TCP_SERVER_SINK_H_ */
