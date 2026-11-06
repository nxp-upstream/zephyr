/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Forward and inverse FFT of one block of 32-bit PCM, with no window and no
 * overlap: the inverse of the forward transform gives the block back, so the
 * blocks are played one after the other unchanged.
 *
 * The length n of a transform is that of the block, a power of two from 4 to
 * FFT_MAX_SIZE, chosen with each call.
 *
 * Plain math on plain memory: no RPC, buffer or mpipe code.
 */

#ifndef FFT_H_
#define FFT_H_

#include <stdbool.h>
#include <stdint.h>

#include "aud_func.h"

/* Longest transform: the largest block */
#define FFT_MAX_SIZE AUD_MAX_BLOCK_SAMPLES

/* Work buffers of one transform */
struct fft {
	float re[FFT_MAX_SIZE];
	float im[FFT_MAX_SIZE];
};

/* Build the twiddle tables shared by every transform, once */
void fft_init(void);

/* True when the FFT takes blocks of n samples */
bool fft_validate_size(uint32_t n);

/*
 * Transform n samples of one channel of pcm, read stride samples apart (the
 * channel count of interleaved PCM). bins receives n / 2 + 1 pairs of real
 * and imaginary parts, for full-scale samples in [-1, 1).
 */
void fft_forward(struct fft *fft, uint32_t n, const int32_t *pcm, uint32_t stride, float *bins);

/*
 * Transform n / 2 + 1 pairs of bins back to n samples of one channel,
 * clamped to the 32-bit range and written stride samples apart in pcm.
 */
void fft_inverse(struct fft *fft, uint32_t n, const float *bins, int32_t *pcm, uint32_t stride);

#endif /* FFT_H_ */
