/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_element.h>

#include <zephyr/mpipe/net/mpipe_socket_sink.h>

LOG_MODULE_REGISTER(mpipe_socket_sink, CONFIG_MPIPE_LOG_LEVEL);

static void mpipe_socket_sink_close(struct mpipe_socket_sink *ssink)
{
	if (ssink->fd >= 0) {
		(void)zsock_close(ssink->fd);
		ssink->fd = -1;
	}
}

int mpipe_socket_sink_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_socket_sink *ssink = (struct mpipe_socket_sink *)obj;

	switch (key) {
	case MPIPE_PROP_SOCKET_SINK_FD:
		if (val == NULL) {
			return -EINVAL;
		}

		if (ssink->sink.element.current_state == MPIPE_STATE_PLAYING) {
			LOG_ERR("The socket cannot change while playing");
			return -EBUSY;
		}

		mpipe_socket_sink_close(ssink);
		ssink->fd = *(const int *)val;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mpipe_socket_sink_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_socket_sink *ssink = (struct mpipe_socket_sink *)obj;

	switch (key) {
	case MPIPE_PROP_SOCKET_SINK_FD:
		*(int *)val = ssink->fd;
		return 0;
	default:
		return -ENOTSUP;
	}
}

/* The stack is short of buffers or window for now; the peer is still there */
static bool mpipe_socket_sink_transient(int err)
{
	return err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS || err == ENOMEM;
}

/*
 * Write the buffer, fragment by fragment, each whole or not at all: a
 * transient shortage before the first byte of a fragment drops it, since a
 * live stream is better short of a block than stalled, while a shortage in
 * the middle of one is retried, so the bytes the peer reads stay aligned on
 * block boundaries. A peer that is gone fails the stream.
 */
static int mpipe_socket_sink_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					struct net_buf **out_buf)
{
	struct mpipe_socket_sink *ssink =
		CONTAINER_OF(pad->object.container, struct mpipe_socket_sink, sink.element.object);
	int ret = 0;

	*out_buf = NULL;

	if (ssink->fd < 0) {
		ret = -ENOTCONN;
	}

	for (struct net_buf *frag = in_buf; frag != NULL && ret == 0; frag = frag->frags) {
		uint32_t len = mpipe_buffer_get_meta(frag)->bytes_used;
		uint32_t sent = 0;

		while (sent < len) {
			ssize_t wr = zsock_send(ssink->fd, frag->data + sent, len - sent, 0);

			if (wr >= 0) {
				sent += (uint32_t)wr;
				continue;
			}

			if (!mpipe_socket_sink_transient(errno)) {
				ret = -errno;
				LOG_ERR("send() failed (%d)", errno);
				break;
			}

			if (sent == 0U) {
				ssink->dropped++;
				LOG_DBG("No room to send, %u bytes dropped (%u so far)", len,
					ssink->dropped);
				break;
			}
		}
	}

	net_buf_unref(in_buf);

	return ret;
}

int mpipe_socket_sink_change_state(struct mpipe_element *self, enum mpipe_state_change transition)
{
	struct mpipe_socket_sink *ssink = (struct mpipe_socket_sink *)self;
	int ret = mpipe_sink_change_state(self, transition);

	if (ret != 0) {
		return ret;
	}

	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY) {
		if (ssink->dropped > 0U) {
			LOG_INF("%u buffers dropped for want of room to send", ssink->dropped);
		}
		mpipe_socket_sink_close(ssink);
	}

	return 0;
}

int mpipe_socket_sink_init(struct mpipe_socket_sink *ssink, uint8_t id)
{
	struct mpipe_element *self = &ssink->sink.element;
	int ret = mpipe_sink_init(&ssink->sink, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "socket_sink");

	self->object.set_property = mpipe_socket_sink_set_property;
	self->object.get_property = mpipe_socket_sink_get_property;
	self->change_state = mpipe_socket_sink_change_state;
	ssink->sink.sink_pad.process_fn = mpipe_socket_sink_process_fn;

	ssink->fd = -1;
	ssink->dropped = 0;

	return 0;
}
