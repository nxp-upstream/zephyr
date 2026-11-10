/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/aud/mpipe_aud.h>
#include <zephyr/mpipe/aud/mpipe_aud_buffer_pool_client.h>
#include <zephyr/mpipe/aud/mpipe_aud_transform_client.h>

LOG_MODULE_REGISTER(mpipe_aud_transform_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_aud_transform_client_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
						 struct net_buf **out_buf)
{
	struct mpipe_aud_transform_client *atc =
		(struct mpipe_aud_transform_client *)pad->object.container;
	struct mpipe_buffer_pool *in_pool = &atc->in_pool.pool;
	struct mpipe_buffer_pool *out_pool = &atc->out_pool.pool;
	struct mpipe_buffer_meta *in_meta;
	struct mpipe_buffer_meta *out_meta;
	uint32_t out_used = 0U;
	int ret;

	if (in_buf == NULL || out_buf == NULL) {
		return -EINVAL;
	}

	/*
	 * A buffer from another pool is copied into one of the remote's input
	 * buffers: the remote reads only memory of its own.
	 */
	if (mpipe_buffer_get_meta(in_buf)->pool != in_pool) {
		struct net_buf *remote_buf;
		struct mpipe_buffer_meta *remote_meta;
		uint32_t bytes = mpipe_buffer_get_meta(in_buf)->bytes_used;

		/* Started on first use: upstream never starts a pool it did not take */
		ret = mpipe_buffer_pool_start(in_pool);
		if (ret != 0) {
			LOG_ERR("Unable to start the input buffer pool (%d)", ret);
			net_buf_unref(in_buf);
			return ret;
		}

		if (bytes > in_pool->config.size) {
			LOG_WRN_ONCE("Input of %u bytes truncated to %u", bytes,
				     in_pool->config.size);
			bytes = in_pool->config.size;
		}

		ret = in_pool->acquire_buffer(in_pool, &remote_buf);
		if (ret != 0) {
			LOG_ERR("Unable to copy the input to the remote (%d)", ret);
			net_buf_unref(in_buf);
			return ret;
		}

		memcpy(remote_buf->data, in_buf->data, bytes);

		remote_meta = mpipe_buffer_get_meta(remote_buf);
		remote_meta->bytes_used = bytes;
		remote_meta->timestamp = mpipe_buffer_get_meta(in_buf)->timestamp;
		remote_buf->len = (uint16_t)MIN(bytes, UINT16_MAX);

		net_buf_unref(in_buf);
		in_buf = remote_buf;
	}

	in_meta = mpipe_buffer_get_meta(in_buf);

	ret = out_pool->acquire_buffer(out_pool, out_buf);
	if (ret != 0 || *out_buf == NULL) {
		LOG_ERR("Failed to acquire an output buffer (%d)", ret);
		*out_buf = NULL;
		net_buf_unref(in_buf);
		return -ENOMEM;
	}

	/* The remote reads the input from memory: push it out of this core's cache */
	(void)sys_cache_data_flush_range(in_buf->data, in_meta->bytes_used);

	/* The RPC interface carries 32-bit addresses */
	ret = atc->process_rpc(atc->handle, (uint32_t)(uintptr_t)in_buf->data, in_meta->bytes_used,
			       (uint32_t)(uintptr_t)(*out_buf)->data, &out_used);

	out_meta = mpipe_buffer_get_meta(*out_buf);
	out_meta->timestamp = in_meta->timestamp;
	net_buf_unref(in_buf);

	if (ret != 0) {
		LOG_ERR("Remote processing failed (%d)", ret);
		net_buf_unref(*out_buf);
		*out_buf = NULL;
		return -EIO;
	}

	/*
	 * The remote wrote the output to memory: drop what this core has cached
	 * of the whole buffer, whatever size the remote reported.
	 */
	(void)sys_cache_data_invd_range((*out_buf)->data, out_pool->config.size);

	if (out_used > out_pool->config.size) {
		LOG_WRN_ONCE("Remote reported %u bytes in a %u-byte buffer", out_used,
			     out_pool->config.size);
		out_used = out_pool->config.size;
	}

	out_meta->bytes_used = out_used;
	(*out_buf)->len = (uint16_t)MIN(out_used, UINT16_MAX);

	return 0;
}

/*
 * The remote answers with audio_caps, each standing for several fixed
 * capabilities: walk them until the one at index
 */
static int mpipe_aud_transform_client_enum(struct mpipe_aud_transform_client *atc,
					   enum mpipe_pad_direction direction,
					   const struct mpipe_structure *in, uint32_t index,
					   const struct mpipe_structure *filter,
					   struct mpipe_structure *out)
{
	struct audio_caps in_caps;
	struct audio_caps caps;
	uint32_t first = 0U;

	if (in != NULL && mpipe_aud_caps_from_structure(in, &in_caps) != 0) {
		return -ENOENT;
	}

	for (uint32_t ind = 0U; ind <= UINT8_MAX; ind++) {
		uint32_t count;
		int32_t ret;

		memset(&caps, 0, sizeof(caps));

		if (in == NULL) {
			ret = atc->get_format_caps_rpc(atc->handle, direction, (uint8_t)ind, &caps);
		} else {
			ret = atc->transform_cap_rpc(atc->handle, direction, (uint16_t)ind,
						     &in_caps, &caps);
		}

		if (ret != 0) {
			return -ENOENT;
		}

		/* The fixed capabilities of this entry, one per sample rate and bit width */
		count = (uint32_t)__builtin_popcount(caps.supported_sample_rates) *
			(uint32_t)__builtin_popcount(caps.supported_bit_widths);
		if (index < first + count) {
			return mpipe_aud_enum_caps(&caps, index - first, filter, out);
		}

		first += count;
	}

	return -ENOENT;
}

static int mpipe_aud_transform_client_enum_caps(struct mpipe_pad *pad, uint32_t index,
						const struct mpipe_structure *filter,
						struct mpipe_structure *out)
{
	struct mpipe_aud_transform_client *atc =
		(struct mpipe_aud_transform_client *)pad->object.container;

	if (pad->direction != MPIPE_PAD_SINK && pad->direction != MPIPE_PAD_SRC) {
		return -EINVAL;
	}

	return mpipe_aud_transform_client_enum(atc, pad->direction, NULL, index, filter, out);
}

static int mpipe_aud_transform_client_set_caps(struct mpipe_transform *transform,
					       enum mpipe_pad_direction direction,
					       const struct mpipe_structure *caps)
{
	struct mpipe_aud_transform_client *atc = (struct mpipe_aud_transform_client *)transform;
	struct mpipe_buffer_pool *pool;
	struct audio_caps fmt;
	uint32_t buf_size = 0U;
	int ret;

	if (caps == NULL || !mpipe_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	if (direction == MPIPE_PAD_SINK) {
		pool = transform->in_pool;
	} else if (direction == MPIPE_PAD_SRC) {
		pool = transform->out_pool;
	} else {
		LOG_ERR("Pad direction is invalid");
		return -EINVAL;
	}

	/* A fixed capability gives one bit of each mask and min == max */
	ret = mpipe_aud_caps_from_structure(caps, &fmt);
	if (ret != 0) {
		return ret;
	}

	ret = atc->set_format_rpc(atc->handle, direction, &fmt, &buf_size);
	if (ret != 0 || buf_size == 0U) {
		LOG_ERR("Unable to set format: dir=%d format=%u rates=0x%x widths=0x%x ch=%u",
			direction, fmt.format, fmt.supported_sample_rates,
			fmt.supported_bit_widths, fmt.min_total_channels);
		return (ret != 0) ? ret : -EINVAL;
	}

	/* The remote sizes the buffers of the instance: the format must fit them */
	if (buf_size > pool->config.size) {
		LOG_ERR("Format needs %u bytes, remote buffers have %u", buf_size,
			pool->config.size);
		return -ENOSPC;
	}

	/* Set pad's caps only when everything is OK */
	return mpipe_pad_set_caps(
		direction == MPIPE_PAD_SRC ? &transform->src_pad : &transform->sink_pad, caps);
}

static int mpipe_aud_transform_client_transform_caps(struct mpipe_transform *self,
						     enum mpipe_pad_direction direction,
						     const struct mpipe_structure *in,
						     uint32_t index, struct mpipe_structure *out)
{
	struct mpipe_aud_transform_client *atc = (struct mpipe_aud_transform_client *)self;

	if (direction != MPIPE_PAD_SINK && direction != MPIPE_PAD_SRC) {
		return -EINVAL;
	}

	if (in == NULL) {
		return -ENOENT;
	}

	return mpipe_aud_transform_client_enum(atc, direction, in, index, NULL, out);
}

static int mpipe_aud_transform_client_change_state(struct mpipe_element *self,
						   enum mpipe_state_change transition)
{
	struct mpipe_aud_transform_client *atc = (struct mpipe_aud_transform_client *)self;
	int ret;

	/* The base transform resets the pad caps and stops the output pool */
	ret = mpipe_transform_change_state(self, transition);

	/* The input pool is this element's too: it was started on first use */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY) {
		int err = mpipe_buffer_pool_stop(&atc->in_pool.pool);

		if (ret == 0) {
			ret = err;
		}
	}

	return ret;
}

/* Ask the remote for the buffers of one direction of the instance and hand them to a pool */
static int mpipe_aud_transform_client_setup_pool(struct mpipe_aud_transform_client *atc,
						 struct mpipe_aud_buffer_pool_client *abpc,
						 enum mpipe_pad_direction direction)
{
	uint8_t num_buffers = 0U;
	uint32_t buf_size = 0U;
	uint16_t align = 0U;
	int32_t ret;

	mpipe_aud_buffer_pool_client_init(&abpc->pool, direction);

	ret = atc->get_buf_caps_rpc(atc->handle, direction, &num_buffers, &buf_size, &align);
	if (ret != 0) {
		LOG_ERR("Unable to retrieve the remote buffers of handle %u (%d)", atc->handle,
			(int)ret);
		return -ENODEV;
	}

	return mpipe_aud_buffer_pool_client_setup(&abpc->pool, atc->handle, num_buffers, buf_size,
						  align, atc->get_buf_rpc);
}

int mpipe_aud_transform_client_init(struct mpipe_aud_transform_client *atc, uint8_t id,
				    enum mpipe_aud_remote_func func)
{
	__ASSERT_NO_MSG(atc != NULL);

	struct mpipe_element *self = &atc->transform.transform.element;
	struct mpipe_transform *transform = &atc->transform.transform;
	int ret;

	if (atc->open_rpc == NULL || atc->get_buf_caps_rpc == NULL || atc->get_buf_rpc == NULL ||
	    atc->get_format_caps_rpc == NULL || atc->set_format_rpc == NULL ||
	    atc->transform_cap_rpc == NULL || atc->process_rpc == NULL) {
		return -EINVAL;
	}

	ret = mpipe_transform_client_init(&atc->transform, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "aud_transform_client");

	ret = atc->open_rpc(func, &atc->handle);
	if (ret != 0) {
		LOG_ERR("Remote refused function %d (%d)", (int)func, ret);
		return ret;
	}

	LOG_DBG("Function %d opened as handle %u", (int)func, atc->handle);

	/* The remote keeps the buffers of each instance apart: ask for those of this one */
	ret = mpipe_aud_transform_client_setup_pool(atc, &atc->in_pool, MPIPE_PAD_SINK);
	if (ret == 0) {
		ret = mpipe_aud_transform_client_setup_pool(atc, &atc->out_pool, MPIPE_PAD_SRC);
	}

	if (ret != 0) {
		return ret;
	}

	transform->in_pool = &atc->in_pool.pool;
	transform->out_pool = &atc->out_pool.pool;

	/*
	 * Replace the base processing: the call carries the handle of the
	 * function instance, which the base transform client knows nothing of.
	 */
	atc->transform.process_fn_rpc = NULL;
	transform->sink_pad.process_fn = mpipe_aud_transform_client_process_fn;

	transform->sink_pad.enum_caps_fn = mpipe_aud_transform_client_enum_caps;
	transform->src_pad.enum_caps_fn = mpipe_aud_transform_client_enum_caps;
	transform->set_caps = mpipe_aud_transform_client_set_caps;
	transform->transform_caps = mpipe_aud_transform_client_transform_caps;

	self->change_state = mpipe_aud_transform_client_change_state;

	return 0;
}
