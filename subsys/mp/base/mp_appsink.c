/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/mp/base/mp_appsink.h>

LOG_MODULE_REGISTER(mp_appsink, CONFIG_MP_LOG_LEVEL);

enum {
	MP_APPSINK_PAD_ID,
};

static int mp_appsink_set_property(struct mp_object *object, uint32_t id, const void *val)
{
	struct mp_appsink *appsink = (struct mp_appsink *)object;

	switch (id) {
	case MP_APPSINK_PROP_HOOK:
		appsink->hook = val;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mp_appsink_chainfn(struct mp_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct mp_appsink *appsink = (struct mp_appsink *)pad->object.container;

	if (appsink->hook != NULL) {
		appsink->hook((void *)appsink, MP_APPSINK_PAD_ID);
	}

	net_buf_unref(in_buf);
	*out_buf = NULL;

	return 0;
}

void mp_appsink_init(struct mp_element *self)
{
	struct mp_appsink *appsink = (struct mp_appsink *)self;

	mp_sink_init((struct mp_element *)appsink);

	appsink->sink.sinkpad.chainfn = mp_appsink_chainfn;
	self->object.set_property = mp_appsink_set_property;
}
