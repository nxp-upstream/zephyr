/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * IFFT function: the spectra of a block become a 32-bit stereo PCM block
 * again. With two channels each one goes back to its own side; a single
 * channel is written to both. Each instance has work buffers of its own.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "aud_func.h"
#include "fft.h"

/* Instances the server can hold at once */
#define IFFT_FUNC_MAX_INSTANCES 2U

BUILD_ASSERT(AUD_FFT_SIZE(AUD_MAX_CHANNELS, AUD_MAX_BLOCK_SAMPLES) <= AUD_BUF_SIZE);
BUILD_ASSERT(AUD_PCM_SIZE(2U, AUD_MAX_BLOCK_SAMPLES) <= AUD_BUF_SIZE);
BUILD_ASSERT(AUD_MAX_CHANNELS <= 2U, "the output is stereo");

static struct fft instances[IFFT_FUNC_MAX_INSTANCES];
static uint32_t num_open;

static void *ifft_func_open(void)
{
	if (num_open >= ARRAY_SIZE(instances)) {
		return NULL;
	}

	fft_init();

	return &instances[num_open++];
}

/* Each spectrum is transformed on its own: there is no history to clear */
static void ifft_func_reset(void *state)
{
	ARG_UNUSED(state);
}

static int32_t ifft_func_process(void *state, uint8_t num_channels, uint32_t num_samples,
				 const uint8_t *in, uint32_t in_sz, uint8_t *out, uint32_t *out_sz)
{
	const float *bins = (const float *)in;
	int32_t *pcm = (int32_t *)out;

	if (in_sz != AUD_FFT_SIZE(num_channels, num_samples) || !fft_validate_size(num_samples)) {
		return -EINVAL;
	}

	/* Left, then right from the second spectrum, or a copy of the left with a single one */
	fft_inverse(state, num_samples, bins, &pcm[0], 2U);

	if (num_channels == 2U) {
		fft_inverse(state, num_samples, &bins[2U * AUD_FFT_BINS(num_samples)], &pcm[1], 2U);
	} else {
		for (uint32_t i = 0U; i < num_samples; i++) {
			pcm[2U * i + 1U] = pcm[2U * i];
		}
	}

	*out_sz = AUD_PCM_SIZE(2U, num_samples);

	return 0;
}

const struct aud_func aud_func_ifft = {
	.id = MPIPE_RPC_AUD_FUNC_IFFT,
	.name = "ifft",
	.in_cap = &aud_fft_cap,
	.out_cap = &aud_stereo_cap,
	.keeps_channels = false,
	.open = ifft_func_open,
	.reset = ifft_func_reset,
	.process = ifft_func_process,
};
