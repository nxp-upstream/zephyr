/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/net/mpipe_tcp_server_sink.h>

#include "tcp_server.h"

LOG_MODULE_REGISTER(mpipe_tcp_server_sink, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_tcp_server_sink_set_property(struct mpipe_object *obj, uint32_t key,
					      const void *val)
{
	struct mpipe_tcp_server_sink *tsink = (struct mpipe_tcp_server_sink *)obj;

	switch (key) {
	case MPIPE_PROP_TCP_SERVER_SINK_PORT:
		if (val == NULL) {
			return -EINVAL;
		}
		tsink->port = *(const uint16_t *)val;
		return 0;
	default:
		return mpipe_socket_sink_set_property(obj, key, val);
	}
}

static int mpipe_tcp_server_sink_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_tcp_server_sink *tsink = (struct mpipe_tcp_server_sink *)obj;

	switch (key) {
	case MPIPE_PROP_TCP_SERVER_SINK_PORT:
		*(uint16_t *)val = tsink->port;
		return 0;
	default:
		return mpipe_socket_sink_get_property(obj, key, val);
	}
}

static int mpipe_tcp_server_sink_change_state(struct mpipe_element *self,
					      enum mpipe_state_change transition)
{
	struct mpipe_tcp_server_sink *tsink = (struct mpipe_tcp_server_sink *)self;
	int fd;

	switch (transition) {
	case MPIPE_STATE_CHANGE_READY_TO_PAUSED:
		fd = mpipe_net_tcp_listen(tsink->port);
		if (fd < 0) {
			return fd;
		}
		tsink->server_fd = fd;
		break;
	case MPIPE_STATE_CHANGE_PAUSED_TO_PLAYING:
		/* The client is accepted on every start, since a pause drops it */
		fd = mpipe_net_tcp_accept(tsink->server_fd);
		if (fd < 0) {
			return fd;
		}
		mpipe_net_tcp_close(&tsink->ssink.fd);
		tsink->ssink.fd = fd;
		break;
	case MPIPE_STATE_CHANGE_PLAYING_TO_PAUSED:
		mpipe_net_tcp_close(&tsink->ssink.fd);
		break;
	case MPIPE_STATE_CHANGE_PAUSED_TO_READY:
		mpipe_net_tcp_close(&tsink->server_fd);
		break;
	default:
		break;
	}

	/* The socket sink writes the client and lets go of it on teardown */
	return mpipe_socket_sink_change_state(self, transition);
}

int mpipe_tcp_server_sink_init(struct mpipe_tcp_server_sink *tsink, uint8_t id)
{
	struct mpipe_element *self = &tsink->ssink.sink.element;
	int ret = mpipe_socket_sink_init(&tsink->ssink, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "tcp_server_sink");

	self->object.set_property = mpipe_tcp_server_sink_set_property;
	self->object.get_property = mpipe_tcp_server_sink_get_property;
	self->change_state = mpipe_tcp_server_sink_change_state;

	tsink->port = CONFIG_MPIPE_NET_SINK_PORT;
	tsink->server_fd = -1;

	return 0;
}
