/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/random/random.h>

#include <zephyr/mp/core/mp_buffer.h>
#include <zephyr/mp/core/mp_bus.h>
#include <zephyr/mp/core/mp_caps.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_messages.h>
#include <zephyr/mp/core/mp_pad.h>
#include <zephyr/mp/core/mp_query.h>
#include <zephyr/mp/core/mp_src.h>

#include <zephyr/mp/core/mp_fake_src.h>

LOG_MODULE_REGISTER(mp_fake_src, CONFIG_MP_LOG_LEVEL);

NET_BUF_POOL_FIXED_DEFINE(mp_fake_src_pool, CONFIG_MP_NET_BUF_POOL_COUNT,
			  CONFIG_MP_FAKE_SRC_BUF_SIZE, sizeof(struct mp_buffer_meta),
			  mp_buffer_destroy);

/**
 * @brief Compute the size of the next packet according to the configured size mode.
 *
 * @param fsrc Pointer to the fake source instance.
 *
 * @return Packet size in bytes, or 0 for @ref MP_FAKE_SRC_SIZE_EMPTY mode.
 */
static uint32_t fake_src_next_size(struct mp_fake_src *fsrc)
{
	switch (fsrc->size_mode) {
	case MP_FAKE_SRC_SIZE_EMPTY:
		return 0;

	case MP_FAKE_SRC_SIZE_FIXED:
		return fsrc->size_max;

	case MP_FAKE_SRC_SIZE_RANDOM: {
		uint32_t range = (fsrc->size_max > fsrc->size_min)
					 ? (fsrc->size_max - fsrc->size_min + 1)
					 : 1;
		return fsrc->size_min + (sys_rand32_get() % range);
	}

	default:
		return fsrc->size_max;
	}
}

/**
 * @brief Fill a data buffer with the selected fill mode.
 *
 * For @ref MP_FAKE_SRC_FILL_NONE mode the buffer is left untouched.
 *
 * @param fsrc Pointer to the fake source instance.
 * @param data Pointer to the destination buffer.
 * @param size Number of bytes to fill.
 */
static void fake_src_fill(struct mp_fake_src *fsrc, uint8_t *data, uint32_t size)
{
	if (size == 0 || data == NULL) {
		return;
	}

	switch (fsrc->fill_mode) {
	case MP_FAKE_SRC_FILL_ZERO:
		memset(data, 0, size);
		break;

	case MP_FAKE_SRC_FILL_NONE:
		break;

	default:
		memset(data, 0, size);
		break;
	}
}

/**
 * @brief Derive a PTS timestamp in milliseconds.
 *
 * In data_rate mode: PTS = bytes_sent / data_rate. Must be called before
 * updating bytes_sent so the timestamp reflects the start of the packet.
 *
 * @param fsrc Pointer to the fake source instance.
 *
 * @return PTS timestamp in ms, or 0 when data_rate is not configured.
 */
static uint32_t fake_src_timestamp(struct mp_fake_src *fsrc)
{
	if (fsrc->data_rate == 0) {
		return 0;
	}

	return (uint32_t)((fsrc->bytes_sent * 1000ULL) / fsrc->data_rate);
}

/**
 * @brief Derive per-packet duration in milliseconds from packet size and data_rate.
 *
 * @param fsrc Pointer to the fake source instance.
 * @param packet_size Size of the packet in bytes.
 *
 * @return Duration in ms, or 0 when data_rate is not set or the packet is empty.
 */
static uint32_t fake_src_duration(struct mp_fake_src *fsrc, uint32_t packet_size)
{
	if (fsrc->data_rate == 0 || packet_size == 0) {
		return 0;
	}

	return (uint32_t)((((uint64_t)packet_size) * 1000ULL) / fsrc->data_rate);
}

/**
 * @brief Acquire a buffer from the static net_buf pool, fill it, and stamp metadata.
 *
 * @param pool    Pointer to the buffer pool.
 * @param buf_out Output pointer to the allocated net_buf on success.
 *
 * @retval 0        Buffer acquired successfully.
 * @retval -EINVAL  @p buf_out is NULL.
 * @retval -ENODATA Packet limit reached; EOS posted to the pipeline bus.
 * @retval -ENOBUFS net_buf allocation failed.
 */
static int fake_src_pool_acquire(struct mp_buffer_pool *pool, struct net_buf **buf_out)
{
	struct mp_fake_src *fsrc = CONTAINER_OF(pool, struct mp_fake_src, pool);
	struct net_buf *nb;
	struct mp_buffer_meta *meta;
	uint32_t pkt_size;
	uint32_t ts;
	uint32_t dur;

	if (buf_out == NULL) {
		return -EINVAL;
	}

	struct mp_src *src = &fsrc->base;

	if (src->num_buffers > 0U && fsrc->packets_sent >= src->num_buffers) {
		struct mp_bus *bus = mp_element_get_bus(&src->element);

		if (bus != NULL) {
			struct mp_message *msg =
				mp_message_new(MP_MESSAGE_EOS, &src->element.object, NULL);

			if (msg != NULL) {
				mp_bus_post(bus, msg);
			}
		}

		LOG_DBG("[fake_src] EOS posted to bus after %u packets", fsrc->packets_sent);
		return -ENODATA;
	}

	pkt_size = fake_src_next_size(fsrc);
	nb = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_NO_WAIT);
	if (nb == NULL) {
		LOG_ERR("[fake_src] net_buf_alloc_len failed");
		return -ENOBUFS;
	}

	fake_src_fill(fsrc, nb->data, pkt_size);
	nb->len = pkt_size;

	ts = fake_src_timestamp(fsrc);
	dur = fake_src_duration(fsrc, pkt_size);

	fsrc->bytes_sent += pkt_size;
	fsrc->packets_sent += 1;

	meta = mp_buffer_get_meta(nb);
	meta->pool = pool;
	meta->bytes_used = pkt_size;
	meta->timestamp = ts;
	meta->priv = (void *)(uintptr_t)dur;

	LOG_DBG("[fake_src] pkt #%u size=%u ts=%u ms dur=%u ms", fsrc->packets_sent, pkt_size, ts,
		dur);

	if (dur > 0U) {
		k_sleep(K_MSEC(dur));
	}

	*buf_out = nb;
	return 0;
}

/**
 * @brief Release a buffer back to the pool.
 *
 * Resets net_buf metadata. Called by mp_buffer_destroy when the
 * net_buf refcount reaches 0.
 *
 * @param pool Pointer to the buffer pool.
 * @param nb   Pointer to the net_buf to release.
 *
 * @retval 0 Always succeeds.
 */
static int fake_src_pool_release(struct mp_buffer_pool *pool, struct net_buf *nb)
{
	ARG_UNUSED(pool);

	if (nb == NULL) {
		return 0;
	}

	struct mp_buffer_meta *meta = mp_buffer_get_meta(nb);

	if (meta != NULL) {
		meta->bytes_used = 0;
		meta->timestamp = 0;
		meta->priv = NULL;
	}

	nb->len = 0;

	LOG_DBG("[fake_src] buffer released buf=%p", (void *)nb);
	return 0;
}

static int fake_src_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	struct mp_fake_src *fsrc = MP_FAKE_SRC(obj);

	switch (key) {
	case PROP_FAKE_SRC_SIZE_MODE:
		fsrc->size_mode = (enum mp_fake_src_size_mode)(uintptr_t)val;
		return 0;
	case PROP_FAKE_SRC_SIZE_MIN:
		fsrc->size_min = (uint32_t)(uintptr_t)val;
		return 0;
	case PROP_FAKE_SRC_SIZE_MAX:
		if (CONFIG_MP_FAKE_SRC_BUF_SIZE < (uint32_t)(uintptr_t)val) {
			return -EINVAL;
		}

		fsrc->size_max = (uint32_t)(uintptr_t)val;
		return 0;
	case PROP_FAKE_SRC_DATA_RATE:
		fsrc->data_rate = (uint32_t)(uintptr_t)val;
		return 0;
	case PROP_FAKE_SRC_FILL_MODE:
		fsrc->fill_mode = (enum mp_fake_src_fill_mode)(uintptr_t)val;
		return 0;
	default:
		return mp_src_set_property(obj, key, val);
	}
}

static int fake_src_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	struct mp_fake_src *fsrc = MP_FAKE_SRC(obj);

	switch (key) {
	case PROP_FAKE_SRC_SIZE_MODE:
		*(uint32_t *)val = (uint32_t)fsrc->size_mode;
		return 0;
	case PROP_FAKE_SRC_SIZE_MIN:
		*(uint32_t *)val = fsrc->size_min;
		return 0;
	case PROP_FAKE_SRC_SIZE_MAX:
		*(uint32_t *)val = fsrc->size_max;
		return 0;
	case PROP_FAKE_SRC_DATA_RATE:
		*(uint32_t *)val = fsrc->data_rate;
		return 0;
	case PROP_FAKE_SRC_FILL_MODE:
		*(uint32_t *)val = (uint32_t)fsrc->fill_mode;
		return 0;
	default:
		return mp_src_get_property(obj, key, val);
	}
}

static enum mp_state_change_return fake_src_change_state(struct mp_element *self,
							 enum mp_state_change transition)
{
	struct mp_fake_src *fsrc = MP_FAKE_SRC(self);
	enum mp_state_change_return ret;

	switch (transition) {
	case MP_STATE_CHANGE_READY_TO_PAUSED:
		fsrc->bytes_sent = 0;
		fsrc->packets_sent = 0;
		fsrc->pool.started = true;

		LOG_DBG("[fake_src] READY -> PAUSED: pool started");
		ret = MP_STATE_CHANGE_SUCCESS;
		break;

	case MP_STATE_CHANGE_PAUSED_TO_PLAYING:
		LOG_DBG("[fake_src] PAUSED -> PLAYING");
		ret = MP_STATE_CHANGE_SUCCESS;
		break;

	case MP_STATE_CHANGE_PLAYING_TO_PAUSED:
		LOG_DBG("[fake_src] PLAYING -> PAUSED");
		ret = MP_STATE_CHANGE_SUCCESS;
		break;

	case MP_STATE_CHANGE_PAUSED_TO_READY:
		fsrc->pool.started = false;

		LOG_DBG("[fake_src] PAUSED -> READY: pool stopped, produced=%u",
			fsrc->packets_sent);
		ret = MP_STATE_CHANGE_SUCCESS;
		break;

	default:
		LOG_DBG("[fake_src] unknown transition=%d", transition);
		ret = MP_STATE_CHANGE_SUCCESS;
		break;
	}

	return ret;
}

static int fake_src_query(struct mp_pad *pad, struct mp_query *query)
{
	LOG_DBG("[fake_src] query type=%u on pad id=%u", query->type, pad->object.id);

	if (query->type == MP_QUERY_CAPS) {
		mp_query_set_caps(query, pad->caps);
		LOG_DBG("[fake_src] CAPS query answered with pad caps=%p", (void *)pad->caps);
	}

	return 0;
}

void mp_fake_src_init(struct mp_element *self)
{
	struct mp_src *src = MP_SRC(self);
	struct mp_fake_src *fsrc = MP_FAKE_SRC(self);

	mp_src_init(self);

	fsrc->size_mode = MP_FAKE_SRC_SIZE_FIXED;
	fsrc->size_min = 0;
	fsrc->size_max = 1024;
	fsrc->data_rate = 0;
	fsrc->fill_mode = MP_FAKE_SRC_FILL_ZERO;

	fsrc->bytes_sent = 0;
	fsrc->packets_sent = 0;
	fsrc->base.num_buffers = 100;

	mp_buffer_pool_init(&fsrc->pool);
	fsrc->pool.nb_pool = &mp_fake_src_pool;
	fsrc->pool.config.size = CONFIG_MP_FAKE_SRC_BUF_SIZE;
	fsrc->pool.config.min_buffers = 1;
	fsrc->pool.config.max_buffers = CONFIG_MP_NET_BUF_POOL_COUNT;
	fsrc->pool.acquire_buffer = fake_src_pool_acquire;
	fsrc->pool.release_buffer = fake_src_pool_release;
	fsrc->pool.started = false;
	src->pool = &fsrc->pool;

	self->object.set_property = fake_src_set_property;
	self->object.get_property = fake_src_get_property;
	self->change_state = fake_src_change_state;

	src->srcpad.queryfn = fake_src_query;
}
