/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/net/mpipe_tcp_server_src.h>

#include "tcp_server.h"

LOG_MODULE_REGISTER(mpipe_tcp_server_src, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_tcp_server_src_set_property(struct mpipe_object *obj, uint32_t key,
					     const void *val)
{
	struct mpipe_tcp_server_src *tsrc = (struct mpipe_tcp_server_src *)obj;

	switch (key) {
	case MPIPE_PROP_TCP_SERVER_SRC_PORT:
		if (val == NULL) {
			return -EINVAL;
		}
		tsrc->port = *(const uint16_t *)val;
		return 0;
	default:
		return mpipe_socket_src_set_property(obj, key, val);
	}
}

static int mpipe_tcp_server_src_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_tcp_server_src *tsrc = (struct mpipe_tcp_server_src *)obj;

	switch (key) {
	case MPIPE_PROP_TCP_SERVER_SRC_PORT:
		*(uint16_t *)val = tsrc->port;
		return 0;
	default:
		return mpipe_socket_src_get_property(obj, key, val);
	}
}

static int mpipe_tcp_server_src_change_state(struct mpipe_element *self,
					     enum mpipe_state_change transition)
{
	struct mpipe_tcp_server_src *tsrc = (struct mpipe_tcp_server_src *)self;
	int fd;

	switch (transition) {
	case MPIPE_STATE_CHANGE_READY_TO_PAUSED:
		fd = mpipe_net_tcp_listen(tsrc->port);
		if (fd < 0) {
			return fd;
		}
		tsrc->server_fd = fd;
		break;
	case MPIPE_STATE_CHANGE_PAUSED_TO_PLAYING:
		/* The client is accepted on every start, since a pause drops it */
		fd = mpipe_net_tcp_accept(tsrc->server_fd);
		if (fd < 0) {
			return fd;
		}
		mpipe_net_tcp_close(&tsrc->ssrc.fd);
		tsrc->ssrc.fd = fd;
		break;
	case MPIPE_STATE_CHANGE_PLAYING_TO_PAUSED:
		mpipe_net_tcp_close(&tsrc->ssrc.fd);
		break;
	case MPIPE_STATE_CHANGE_PAUSED_TO_READY:
		mpipe_net_tcp_close(&tsrc->server_fd);
		break;
	default:
		break;
	}

	/* The socket source reads the client and lets go of it on teardown */
	return mpipe_socket_src_change_state(self, transition);
}

int mpipe_tcp_server_src_init(struct mpipe_tcp_server_src *tsrc, uint8_t id)
{
	struct mpipe_element *self = &tsrc->ssrc.src.element;
	int ret = mpipe_socket_src_init(&tsrc->ssrc, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "tcp_server_src");

	self->object.set_property = mpipe_tcp_server_src_set_property;
	self->object.get_property = mpipe_tcp_server_src_get_property;
	self->change_state = mpipe_tcp_server_src_change_state;

	tsrc->port = CONFIG_MPIPE_NET_SRC_PORT;
	tsrc->server_fd = -1;

	return 0;
}
