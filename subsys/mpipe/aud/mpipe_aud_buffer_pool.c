/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>

#include <zephyr/mpipe/aud/mpipe_aud.h>
#include <zephyr/mpipe/aud/mpipe_aud_buffer_pool.h>

LOG_MODULE_REGISTER(mpipe_aud_buffer_pool, CONFIG_MPIPE_LOG_LEVEL);

#define AUD_BUFFER_POOL_BASE_ALIGN sizeof(void *)
/* What one slab may hold: the slice of the store it is given */
#define AUD_BUFFER_POOL_SIZE                                                                       \
	(CONFIG_MPIPE_AUD_BUFFER_POOL_SZ_MAX * CONFIG_MPIPE_AUD_BUFFER_POOL_NUM_MAX)

static __nocache __aligned(AUD_BUFFER_POOL_BASE_ALIGN)
uint8_t aud_buffer_pool_buf[CONFIG_MPIPE_AUD_BUFFER_POOL_COUNT][AUD_BUFFER_POOL_SIZE];

/* The slab each slice was given to, NULL while free */
static struct k_mem_slab *aud_buffer_pool_slabs[CONFIG_MPIPE_AUD_BUFFER_POOL_COUNT];

/*
 * The slice of the store backing a slab: the one it already has, or the next
 * free one. A slab keeps its slice for the life of the program, so a pool
 * configured again, or initialized again over the same slab, finds it.
 */
static uint8_t *mpipe_aud_buffer_pool_slice(struct k_mem_slab *slab)
{
	int free_slot = -1;

	for (int i = 0; i < CONFIG_MPIPE_AUD_BUFFER_POOL_COUNT; i++) {
		if (aud_buffer_pool_slabs[i] == slab) {
			return aud_buffer_pool_buf[i];
		}
		if (aud_buffer_pool_slabs[i] == NULL && free_slot < 0) {
			free_slot = i;
		}
	}

	if (free_slot < 0) {
		return NULL;
	}

	aud_buffer_pool_slabs[free_slot] = slab;

	return aud_buffer_pool_buf[free_slot];
}

static int mpipe_aud_buffer_pool_config(struct mpipe_buffer_pool *pool,
					const struct mpipe_structure *config)
{
	struct mpipe_aud_buffer_pool *aud_pool = (struct mpipe_aud_buffer_pool *)pool;
	int align = 0;
	uint32_t required_align = 0;
	uint8_t *slice;
	uint8_t *base;
	int ret;

	uint32_t sample_rate, bit_width, num_of_channel, frame_interval;

	if (mpipe_aud_caps_get_uint(config, MPIPE_CAPS_SAMPLE_RATE, &sample_rate) != 0 ||
	    mpipe_aud_caps_get_uint(config, MPIPE_CAPS_BIT_WIDTH, &bit_width) != 0 ||
	    mpipe_aud_caps_get_uint(config, MPIPE_CAPS_NUM_CHANNELS, &num_of_channel) != 0 ||
	    mpipe_aud_caps_get_uint(config, MPIPE_CAPS_FRAME_INTERVAL, &frame_interval) != 0) {
		return -EINVAL;
	}

	pool->config.size = (bit_width / BITS_PER_BYTE) * (sample_rate * frame_interval / 1000000) *
			    num_of_channel;
	/* The address needs to be aligned to the size of the DMA transfer */
	required_align = bit_width >> 3;

	/* Decide alignment using LCM */
	align = sys_lcm(AUD_BUFFER_POOL_BASE_ALIGN, required_align);

	if (align == -1) {
		LOG_ERR("Incompatible alignment requirements");
		return -EINVAL;
	} else if (align == 0 && required_align != 0) {
		pool->config.align = required_align;
	} else if (align != 0) {
		pool->config.align = align;
	} else {
		/* Both are 0, use base alignment */
		pool->config.align = AUD_BUFFER_POOL_BASE_ALIGN;
	}

	if (pool->config.size * pool->config.min_buffers > AUD_BUFFER_POOL_SIZE) {
		LOG_ERR("%u blocks of %u bytes needed, the pool holds %u bytes",
			pool->config.min_buffers, pool->config.size,
			(uint32_t)AUD_BUFFER_POOL_SIZE);
		return -EINVAL;
	}

	if (pool->config.min_buffers > CONFIG_MPIPE_AUD_BUFFER_POOL_NUM_MAX) {
		LOG_ERR("%u blocks needed, MPIPE_AUD_BUFFER_POOL_NUM_MAX allows %d",
			pool->config.min_buffers, CONFIG_MPIPE_AUD_BUFFER_POOL_NUM_MAX);
		return -EINVAL;
	}

	if (aud_pool->mem_slab == NULL) {
		LOG_ERR("Memory slab not configured");
		return -EINVAL;
	}

	slice = mpipe_aud_buffer_pool_slice(aud_pool->mem_slab);
	if (slice == NULL) {
		LOG_ERR("No store left for this slab, MPIPE_AUD_BUFFER_POOL_COUNT allows %d",
			CONFIG_MPIPE_AUD_BUFFER_POOL_COUNT);
		return -ENOSPC;
	}

	memset(aud_pool->blocks, 0, sizeof(aud_pool->blocks));

	ret = k_mem_slab_init(aud_pool->mem_slab, (void *)slice, pool->config.size,
			      pool->config.min_buffers);
	if (ret != 0) {
		LOG_ERR("Unable to initialize memory slab (%d)", ret);
		return ret;
	}

	base = (uint8_t *)aud_pool->mem_slab->buffer;

	for (uint8_t i = 0; i < pool->config.min_buffers; i++) {
		/* Keep the mem_slab chunk mapping used by the audio drivers */
		aud_pool->blocks[i] = &base[pool->config.size * i];
	}

	return 0;
}

/* Address stored in the buffer metadata of a block the driver now owns */
static uint8_t mpipe_aud_buffer_pool_handed_over;

void mpipe_aud_buffer_pool_hand_over(struct net_buf *buf)
{
	mpipe_buffer_get_meta(buf)->priv = &mpipe_aud_buffer_pool_handed_over;
}

/* A free block of the slab, wrapped in an empty buffer */
static int mpipe_aud_buffer_pool_acquire_buffer(struct mpipe_buffer_pool *pool,
						struct net_buf **buf)
{
	struct mpipe_aud_buffer_pool *aud_pool = (struct mpipe_aud_buffer_pool *)pool;
	struct mpipe_buffer_meta *meta;
	struct net_buf *out;
	void *block;

	if (aud_pool->mem_slab == NULL) {
		return -EINVAL;
	}

	if (k_mem_slab_alloc(aud_pool->mem_slab, &block, K_NO_WAIT) != 0) {
		return -ENOBUFS;
	}

	out = net_buf_alloc_with_data(pool->nb_pool, block, pool->config.size, K_NO_WAIT);
	if (out == NULL) {
		k_mem_slab_free(aud_pool->mem_slab, block);
		return -ENOBUFS;
	}

	meta = mpipe_buffer_get_meta(out);
	meta->pool = pool;
	meta->bytes_used = 0;
	meta->timestamp = 0;
	meta->driver_buf = NULL;
	meta->priv = NULL;
	meta->parent = NULL;
	out->len = 0;

	*buf = out;

	return 0;
}

/*
 * The block goes back to the slab unless a driver was handed it, in which
 * case the driver frees it. Runs before the buffer header is destroyed, while
 * its data pointer still names the block.
 */
static int mpipe_aud_buffer_pool_release_buffer(struct mpipe_buffer_pool *pool, struct net_buf *buf)
{
	struct mpipe_aud_buffer_pool *aud_pool = (struct mpipe_aud_buffer_pool *)pool;
	struct mpipe_buffer_meta *meta = mpipe_buffer_get_meta(buf);

	if (meta->priv != &mpipe_aud_buffer_pool_handed_over && buf->__buf != NULL &&
	    aud_pool->mem_slab != NULL) {
		k_mem_slab_free(aud_pool->mem_slab, buf->__buf);
	}

	meta->priv = NULL;
	meta->bytes_used = 0;

	return 0;
}

void mpipe_aud_buffer_pool_init(struct mpipe_buffer_pool *pool)
{
	__ASSERT_NO_MSG(pool != NULL);

	struct mpipe_aud_buffer_pool *aud_pool = (struct mpipe_aud_buffer_pool *)pool;

	aud_pool->aud_dev = NULL;
	aud_pool->mem_slab = NULL;
	memset(aud_pool->blocks, 0, sizeof(aud_pool->blocks));

	mpipe_buffer_pool_init(pool);

	pool->configure = mpipe_aud_buffer_pool_config;
	pool->acquire_buffer = mpipe_aud_buffer_pool_acquire_buffer;
	pool->release_buffer = mpipe_aud_buffer_pool_release_buffer;
}
