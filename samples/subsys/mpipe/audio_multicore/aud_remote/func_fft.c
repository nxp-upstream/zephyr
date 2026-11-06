/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * FFT function: each channel of a 32-bit PCM block becomes its spectrum,
 * with no window and no overlap; the spectra follow one another, channel 0
 * first. Each instance has work buffers of its own.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "aud_func.h"
#include "fft.h"

/* Instances the server can hold at once */
#define FFT_FUNC_MAX_INSTANCES 2U

BUILD_ASSERT(AUD_PCM_SIZE(AUD_MAX_CHANNELS, AUD_MAX_BLOCK_SAMPLES) <= AUD_BUF_SIZE);
BUILD_ASSERT(AUD_FFT_SIZE(AUD_MAX_CHANNELS, AUD_MAX_BLOCK_SAMPLES) <= AUD_BUF_SIZE);

static struct fft instances[FFT_FUNC_MAX_INSTANCES];
static uint32_t num_open;

static void *fft_func_open(void)
{
	if (num_open >= ARRAY_SIZE(instances)) {
		return NULL;
	}

	fft_init();

	return &instances[num_open++];
}

/* Each block is transformed on its own: there is no history to clear */
static void fft_func_reset(void *state)
{
	ARG_UNUSED(state);
}

static int32_t fft_func_process(void *state, uint8_t num_channels, uint32_t num_samples,
				const uint8_t *in, uint32_t in_sz, uint8_t *out, uint32_t *out_sz)
{
	const int32_t *pcm = (const int32_t *)in;
	float *bins = (float *)out;

	/* The transform takes whole blocks: a short one is never produced by the source */
	if (in_sz != AUD_PCM_SIZE(num_channels, num_samples) || !fft_validate_size(num_samples)) {
		return -EINVAL;
	}

	/* Channel ch is every num_channels-th sample, from sample ch */
	for (uint8_t ch = 0U; ch < num_channels; ch++) {
		fft_forward(state, num_samples, &pcm[ch], num_channels,
			    &bins[ch * 2U * AUD_FFT_BINS(num_samples)]);
	}

	*out_sz = AUD_FFT_SIZE(num_channels, num_samples);

	return 0;
}

const struct aud_func aud_func_fft = {
	.id = MPIPE_RPC_AUD_FUNC_FFT,
	.name = "fft",
	.in_cap = &aud_pcm_cap,
	.out_cap = &aud_fft_cap,
	.keeps_channels = true,
	.open = fft_func_open,
	.reset = fft_func_reset,
	.process = fft_func_process,
};
