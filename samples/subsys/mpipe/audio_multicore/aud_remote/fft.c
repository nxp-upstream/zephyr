/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "fft.h"

#define FFT_PI 3.14159265358979f

/* PCM full scale: 32-bit samples are mapped to [-1, 1) and back */
#define FFT_PCM_SCALE 2147483648.0f

#define FFT_MIN_SIZE 4U

BUILD_ASSERT(FFT_MAX_SIZE >= FFT_MIN_SIZE);
BUILD_ASSERT(IS_POWER_OF_TWO(FFT_MAX_SIZE), "the longest transform must be a power of two");

/*
 * Twiddle factors e^(-2 pi i k / FFT_MAX_SIZE) for k < FFT_MAX_SIZE / 2. A
 * transform of n points takes every (FFT_MAX_SIZE / n)-th of them.
 */
static float cos_tab[FFT_MAX_SIZE / 2U];
static float sin_tab[FFT_MAX_SIZE / 2U];
static bool tables_ready;

void fft_init(void)
{
	if (tables_ready) {
		return;
	}

	for (uint32_t k = 0U; k < FFT_MAX_SIZE / 2U; k++) {
		float angle = 2.0f * FFT_PI * (float)k / (float)FFT_MAX_SIZE;

		cos_tab[k] = cosf(angle);
		sin_tab[k] = sinf(angle);
	}

	tables_ready = true;
}

bool fft_validate_size(uint32_t n)
{
	return n >= FFT_MIN_SIZE && n <= FFT_MAX_SIZE && IS_POWER_OF_TWO(n);
}

/*
 * In-place iterative radix-2 complex FFT of n points on fft->re and
 * fft->im. sign is -1 for the forward transform and +1 for the inverse one,
 * which is left unscaled.
 */
static void fft_run(struct fft *fft, uint32_t n, float sign)
{
	uint32_t j = 0U;

	for (uint32_t i = 1U; i < n; i++) {
		uint32_t bit = n >> 1;

		while ((j & bit) != 0U) {
			j ^= bit;
			bit >>= 1;
		}
		j |= bit;

		if (i < j) {
			float tmp = fft->re[i];

			fft->re[i] = fft->re[j];
			fft->re[j] = tmp;
			tmp = fft->im[i];
			fft->im[i] = fft->im[j];
			fft->im[j] = tmp;
		}
	}

	for (uint32_t len = 2U; len <= n; len <<= 1) {
		uint32_t half = len >> 1;
		uint32_t step = FFT_MAX_SIZE / len;

		for (uint32_t i = 0U; i < n; i += len) {
			for (uint32_t m = 0U; m < half; m++) {
				float wr = cos_tab[m * step];
				float wi = sign * sin_tab[m * step];
				uint32_t a = i + m;
				uint32_t b = a + half;
				float tr = fft->re[b] * wr - fft->im[b] * wi;
				float ti = fft->re[b] * wi + fft->im[b] * wr;

				fft->re[b] = fft->re[a] - tr;
				fft->im[b] = fft->im[a] - ti;
				fft->re[a] += tr;
				fft->im[a] += ti;
			}
		}
	}
}

void fft_forward(struct fft *fft, uint32_t n, const int32_t *pcm, uint32_t stride, float *bins)
{
	for (uint32_t i = 0U; i < n; i++) {
		fft->re[i] = (float)pcm[i * stride] / FFT_PCM_SCALE;
		fft->im[i] = 0.0f;
	}

	fft_run(fft, n, -1.0f);

	for (uint32_t k = 0U; k <= n / 2U; k++) {
		bins[2U * k] = fft->re[k];
		bins[2U * k + 1U] = fft->im[k];
	}
}

void fft_inverse(struct fft *fft, uint32_t n, const float *bins, int32_t *pcm, uint32_t stride)
{
	/* The spectrum of a real signal: bin n - k is the conjugate of bin k */
	for (uint32_t k = 0U; k <= n / 2U; k++) {
		fft->re[k] = bins[2U * k];
		fft->im[k] = bins[2U * k + 1U];
	}

	for (uint32_t k = 1U; k < n / 2U; k++) {
		fft->re[n - k] = fft->re[k];
		fft->im[n - k] = -fft->im[k];
	}

	fft_run(fft, n, 1.0f);

	/* Each sample in [-1, 1) back to 32-bit PCM, clamped to the 32-bit range */
	for (uint32_t i = 0U; i < n; i++) {
		float s = (fft->re[i] / (float)n) * FFT_PCM_SCALE;

		if (s >= FFT_PCM_SCALE) {
			pcm[i * stride] = INT32_MAX;
		} else if (s <= -FFT_PCM_SCALE) {
			pcm[i * stride] = INT32_MIN;
		} else {
			pcm[i * stride] = (int32_t)s;
		}
	}
}
