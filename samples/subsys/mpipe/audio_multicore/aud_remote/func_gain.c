/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Gain function: each 32-bit PCM sample of every channel is scaled by
 * AUD_GAIN_PERCENT and clamped to the 32-bit range. The format passes
 * through unchanged.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "aud_func.h"

#define GAIN_FUNC_MAX_INSTANCES 4U

/* Q16.16 fixed point */
#define GAIN_FUNC_UNITY (1U << 16)

BUILD_ASSERT(AUD_PCM_SIZE(AUD_MAX_CHANNELS, AUD_MAX_BLOCK_SAMPLES) <= AUD_BUF_SIZE);
BUILD_ASSERT(((uint64_t)AUD_GAIN_PERCENT * GAIN_FUNC_UNITY) / 100U <= INT32_MAX,
	     "gain too large for Q16.16");

struct gain_state {
	int32_t gain_fixed;
};

static struct gain_state states[GAIN_FUNC_MAX_INSTANCES];
static uint32_t num_open;

static void *gain_open(void)
{
	struct gain_state *s;

	if (num_open >= ARRAY_SIZE(states)) {
		return NULL;
	}

	s = &states[num_open++];
	s->gain_fixed = (int32_t)(((uint64_t)AUD_GAIN_PERCENT * GAIN_FUNC_UNITY) / 100U);

	return s;
}

static void gain_reset(void *state)
{
	ARG_UNUSED(state);
}

static int32_t gain_process(void *state, uint8_t num_channels, uint32_t num_samples,
			    const uint8_t *in, uint32_t in_sz, uint8_t *out, uint32_t *out_sz)
{
	const struct gain_state *s = state;
	const int32_t *src = (const int32_t *)in;
	int32_t *dst = (int32_t *)out;
	uint32_t nsamples = in_sz / sizeof(int32_t);

	/* Every sample is scaled alike, whatever channel it belongs to */
	if (in_sz > AUD_PCM_SIZE(num_channels, num_samples)) {
		return -EINVAL;
	}

	for (uint32_t i = 0U; i < nsamples; i++) {
		int64_t v = ((int64_t)src[i] * s->gain_fixed) >> 16;

		dst[i] = (int32_t)CLAMP(v, INT32_MIN, INT32_MAX);
	}

	*out_sz = nsamples * sizeof(int32_t);

	return 0;
}

const struct aud_func aud_func_gain = {
	.id = MPIPE_RPC_AUD_FUNC_GAIN,
	.name = "gain",
	.in_cap = &aud_pcm_cap,
	.out_cap = &aud_pcm_cap,
	.keeps_channels = true,
	.open = gain_open,
	.reset = gain_reset,
	.process = gain_process,
};
