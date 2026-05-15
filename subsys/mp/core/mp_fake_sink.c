/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include <zephyr/mp/core/mp_buffer.h>
#include <zephyr/mp/core/mp_caps.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_event.h>
#include <zephyr/mp/core/mp_object.h>
#include <zephyr/mp/core/mp_pad.h>
#include <zephyr/mp/core/mp_query.h>
#include <zephyr/mp/core/mp_sink.h>

#include <zephyr/mp/core/mp_fake_sink.h>

LOG_MODULE_REGISTER(mp_fake_sink, CONFIG_MP_LOG_LEVEL);

static int fake_sink_query(struct mp_pad *pad, struct mp_query *query)
{
	LOG_DBG("[fake_sink] query type=%u on pad id=%u", query->type, pad->object.id);

	if (query->type == MP_QUERY_CAPS) {
		mp_query_set_caps(query, pad->caps);
		LOG_DBG("[fake_sink] CAPS query answered with pad caps=%p", (void *)pad->caps);
	}

	return 0;
}

static int fake_sink_event(struct mp_pad *pad, struct mp_event *event)
{
	struct mp_fake_sink *fsink = MP_FAKE_SINK(pad->object.container);
	struct mp_sink *sink = MP_SINK(fsink);

	switch (event->type) {
	case MP_EVENT_EOS:
		LOG_DBG("[fake_sink] MP_EVENT_EOS received");
		return 0;

	case MP_EVENT_CAPS:
		LOG_DBG("[fake_sink] MP_EVENT_CAPS received");
		return sink->set_caps(sink, mp_event_get_caps(event));

	default:
		return 0;
	}
}

static int fake_sink_chainfn(struct mp_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct mp_fake_sink *fsink = MP_FAKE_SINK(pad->object.container);
	struct mp_buffer_meta *meta;
	uint32_t pkt_size;
	uint32_t ts;
	uint32_t dur;

	if (in_buf == NULL) {
		LOG_ERR("[fake_sink] chainfn: received NULL buffer");
		*out_buf = NULL;
		return -EINVAL;
	}

	meta = mp_buffer_get_meta(in_buf);
	pkt_size = in_buf->len;
	ts = (meta != NULL) ? meta->timestamp : 0U;
	dur = (meta != NULL) ? (uint32_t)(uintptr_t)meta->priv : 0U;

	LOG_DBG("[fake_sink] consuming pkt #%u size=%u ts=%u ms", fsink->buffers_consumed + 1U,
		pkt_size, ts);

	if (fsink->on_buffer != NULL) {
		fsink->on_buffer(in_buf->data, pkt_size, ts, dur, fsink->user_ctx);
	}

	k_msleep(fsink->processing_delay_ms);

	fsink->buffers_consumed++;
	fsink->bytes_consumed += pkt_size;

	net_buf_unref(in_buf);
	*out_buf = NULL;

	return 0;
}

static enum mp_state_change_return fake_sink_change_state(struct mp_element *self,
							  enum mp_state_change transition)
{
	struct mp_fake_sink *fsink = MP_FAKE_SINK(self);

	switch (transition) {
	case MP_STATE_CHANGE_READY_TO_PAUSED:
		fsink->buffers_consumed = 0U;
		fsink->bytes_consumed = 0U;

		LOG_DBG("[fake_sink] READY -> PAUSED");
		break;

	case MP_STATE_CHANGE_PAUSED_TO_PLAYING:
		LOG_DBG("[fake_sink] PAUSED -> PLAYING");
		break;

	case MP_STATE_CHANGE_PLAYING_TO_PAUSED:
		LOG_DBG("[fake_sink] PLAYING -> PAUSED");
		break;

	case MP_STATE_CHANGE_PAUSED_TO_READY:
		LOG_INF("[fake_sink] PAUSED -> READY: "
			"buffers_consumed=%u bytes_consumed=%llu",
			fsink->buffers_consumed, (unsigned long long)fsink->bytes_consumed);
		break;

	default:
		LOG_DBG("[fake_sink] unknown transition=%d", transition);
		break;
	}

	return MP_STATE_CHANGE_SUCCESS;
}

void mp_fake_sink_init(struct mp_element *self)
{
	struct mp_sink *sink = MP_SINK(self);
	struct mp_fake_sink *fsink = MP_FAKE_SINK(self);

	mp_sink_init(self);

	fsink->on_buffer = NULL;
	fsink->user_ctx = NULL;
	fsink->processing_delay_ms = 0U;

	fsink->buffers_consumed = 0U;
	fsink->bytes_consumed = 0U;

	sink->sinkpad.chainfn = fake_sink_chainfn;
	sink->sinkpad.queryfn = fake_sink_query;
	sink->sinkpad.eventfn = fake_sink_event;
	self->change_state = fake_sink_change_state;
}
