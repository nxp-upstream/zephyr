/* cvsd.c - CVSD codec for Bluetooth Hands-Free Profile */

/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "cvsd.h"

/*
 * Delta modulator parameters, as specified for the CVSD air coding format.
 *
 * The step size decays by beta per 64 kHz sample, which is a syllabic time
 * constant of 1024 / 64000 = 16 ms, and the accumulator leaks by h per sample,
 * which places the integrator corner at 64000 / (2 * pi * 32) = 318 Hz. Both
 * only make sense at the 64 kHz CVSD rate, not at the 8 kHz PCM rate.
 */
#define CVSD_STEP_MIN 10
#define CVSD_STEP_MAX 1280
#define CVSD_Y_MIN    (-32768)
#define CVSD_Y_MAX    32767

/* J out of K identical bits raise the step size. J equals K, so all of them. */
#define CVSD_K            4
#define CVSD_HISTORY_MASK ((1U << CVSD_K) - 1U)

/*
 * h = 1 - 1/32 and beta = 1 - 1/1024, applied as a multiplication followed by a
 * division because the right shift of a negative value is implementation
 * defined. The intermediate products stay well inside int32_t: the accumulator
 * is bounded by 32767 * 31 and the step size by 1280 * 1023.
 */
#define CVSD_LEAK_NUM  31
#define CVSD_LEAK_DEN  32
#define CVSD_DECAY_NUM 1023
#define CVSD_DECAY_DEN 1024

/*
 * Bit order inside an encoded byte. The baseband transmits payload bytes least
 * significant bit first, so the earliest CVSD sample of a byte is bit 0. A peer
 * that serializes the other way round needs (CVSD_OVERSAMPLING - 1U - (pos))
 * here; an encoder and a decoder that both use this file agree either way, so
 * only a real peer can tell the two apart.
 */
#define CVSD_BIT_POS(pos) (pos)

/*
 * Band-limiting filter shared by the 8x interpolator and the 8x decimator: a
 * 128 tap Hamming windowed sinc for 64 kHz with a 3.7 kHz cut-off, in Q11 with
 * a DC gain of CVSD_OVERSAMPLING. It is flat to 3 kHz, -2 dB at 3.4 kHz and
 * below -54 dB above 4.6 kHz, which keeps the delta modulation noise from
 * folding back into the voice band on decimation.
 *
 * Every polyphase branch sums to exactly 1 << CVSD_FILTER_Q. Branches with
 * unequal DC gain would turn a DC input into an 8 kHz ripple.
 */
#define CVSD_FILTER_Q   11
#define CVSD_FILTER_ONE (1 << CVSD_FILTER_Q)

static const int16_t cvsd_filter[CVSD_FILTER_TAPS] = {
	    -6,     -4,     -2,      0,      3,      6,      8,     10,
	    10,     10,      7,      3,     -2,     -9,    -15,    -20,
	   -24,    -24,    -21,    -14,     -3,     10,     24,     37,
	    47,     52,     50,     39,     22,     -2,    -30,    -58,
	   -81,    -97,   -100,    -90,    -64,    -26,     22,     75,
	   124,    163,    185,    183,    154,     98,     18,    -77,
	  -178,   -271,   -342,   -376,   -362,   -290,   -159,     32,
	   273,    551,    848,   1143,   1412,   1636,   1800,   1884,
	  1884,   1800,   1636,   1412,   1143,    848,    551,    273,
	    32,   -159,   -290,   -362,   -376,   -342,   -271,   -178,
	   -77,     18,     98,    154,    183,    185,    163,    124,
	    75,     22,    -26,    -64,    -90,   -100,    -97,    -81,
	   -58,    -30,     -2,     22,     39,     50,     52,     47,
	    37,     24,     10,     -3,    -14,    -21,    -24,    -24,
	   -20,    -15,     -9,     -2,      3,      7,     10,     10,
	    10,      8,      6,      3,      0,     -2,     -4,     -6,
};

/*
 * One CVSD sample period. Updates the step size from the bit history and feeds
 * the bit into the leaky accumulator. The encoder derives the bit from the sign
 * of its prediction error and the decoder takes it from the bitstream, so both
 * ends run the same reconstruction.
 */
static void cvsd_step(struct cvsd_state *state, uint8_t bit)
{
	int32_t leaked;
	int32_t delta;

	state->bit_history = (uint8_t)(((state->bit_history << 1) | bit) & CVSD_HISTORY_MASK);

	if ((state->bit_history == 0U) || (state->bit_history == CVSD_HISTORY_MASK)) {
		state->step = MIN(state->step + CVSD_STEP_MIN, CVSD_STEP_MAX);
	} else {
		state->step = MAX((state->step * CVSD_DECAY_NUM) / CVSD_DECAY_DEN, CVSD_STEP_MIN);
	}

	leaked = (state->accumulator * CVSD_LEAK_NUM) / CVSD_LEAK_DEN;
	delta = (bit != 0U) ? state->step : -state->step;
	state->accumulator = CLAMP(leaked + delta, CVSD_Y_MIN, CVSD_Y_MAX);
}

/*
 * Polyphase interpolation: branch phase of the shared filter holds the taps
 * cvsd_filter[d * CVSD_OVERSAMPLING + phase], which multiply the input history
 * starting at the newest sample. Increasing phase samples the prototype later,
 * which is the fractional delay wanted for output phase of the group.
 *
 * The worst case accumulator magnitude is 3870 * 32768, well inside int32_t.
 */
static int16_t cvsd_interpolate(const int16_t *history, uint8_t phase)
{
	int32_t acc = 0;

	for (uint8_t d = 0U; d < CVSD_FILTER_PHASE_TAPS; d++) {
		acc += (int32_t)cvsd_filter[(d * CVSD_OVERSAMPLING) + phase] * history[d];
	}

	return (int16_t)CLAMP(acc / CVSD_FILTER_ONE, CVSD_Y_MIN, CVSD_Y_MAX);
}

/*
 * Decimation by CVSD_OVERSAMPLING, evaluated once per output sample only. The
 * delay line is walked from the oldest sample to the newest one, so visit k
 * pairs with tap CVSD_FILTER_TAPS - 1 - k. The filter has a DC gain of
 * CVSD_OVERSAMPLING, hence the extra division by it.
 *
 * The worst case accumulator magnitude is 27372 * 32768, well inside int32_t.
 */
static int16_t cvsd_decimate(const struct cvsd_decoder *dec)
{
	int32_t acc = 0;
	uint8_t idx = dec->index;

	for (uint16_t k = 0U; k < CVSD_FILTER_TAPS; k++) {
		acc += (int32_t)cvsd_filter[CVSD_FILTER_TAPS - 1U - k] * dec->history[idx];
		idx = (uint8_t)((idx + 1U) % CVSD_FILTER_TAPS);
	}

	acc = acc / (CVSD_FILTER_ONE * CVSD_OVERSAMPLING);

	return (int16_t)CLAMP(acc, CVSD_Y_MIN, CVSD_Y_MAX);
}

static void cvsd_state_init(struct cvsd_state *state)
{
	state->accumulator = 0;
	state->step = CVSD_STEP_MIN;
	state->bit_history = 0U;
}

void cvsd_encoder_init(struct cvsd_encoder *enc)
{
	if (enc == NULL) {
		return;
	}

	cvsd_state_init(&enc->state);
	memset(enc->history, 0, sizeof(enc->history));
}

void cvsd_decoder_init(struct cvsd_decoder *dec)
{
	if (dec == NULL) {
		return;
	}

	cvsd_state_init(&dec->state);
	memset(dec->history, 0, sizeof(dec->history));
	dec->index = 0U;
}

int cvsd_encoder_encode(struct cvsd_encoder *enc, const uint8_t *pcm, size_t pcm_len, uint8_t *out,
			size_t out_size)
{
	size_t samples;

	if ((enc == NULL) || (pcm == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	if ((pcm_len % sizeof(uint16_t)) != 0U) {
		return -EINVAL;
	}

	samples = pcm_len / sizeof(uint16_t);
	if (out_size < samples) {
		return -ENOSPC;
	}

	for (size_t i = 0U; i < samples; i++) {
		uint8_t encoded = 0U;

		memmove(&enc->history[1], &enc->history[0],
			sizeof(enc->history) - sizeof(enc->history[0]));
		enc->history[0] = (int16_t)sys_get_le16(&pcm[i * sizeof(uint16_t)]);

		for (uint8_t phase = 0U; phase < CVSD_OVERSAMPLING; phase++) {
			int16_t target = cvsd_interpolate(enc->history, phase);
			uint8_t bit = (target >= enc->state.accumulator) ? 1U : 0U;

			cvsd_step(&enc->state, bit);
			encoded |= (uint8_t)(bit << CVSD_BIT_POS(phase));
		}

		out[i] = encoded;
	}

	return (int)samples;
}

int cvsd_decoder_decode(struct cvsd_decoder *dec, const uint8_t *in, size_t in_len, uint8_t *pcm,
			size_t pcm_size)
{
	if ((dec == NULL) || (in == NULL) || (pcm == NULL)) {
		return -EINVAL;
	}

	if (pcm_size < (in_len * sizeof(uint16_t))) {
		return -ENOSPC;
	}

	for (size_t i = 0U; i < in_len; i++) {
		for (uint8_t phase = 0U; phase < CVSD_OVERSAMPLING; phase++) {
			uint8_t bit = (uint8_t)((in[i] >> CVSD_BIT_POS(phase)) & 1U);

			cvsd_step(&dec->state, bit);
			dec->history[dec->index] = (int16_t)dec->state.accumulator;
			dec->index = (uint8_t)((dec->index + 1U) % CVSD_FILTER_TAPS);
		}

		sys_put_le16((uint16_t)cvsd_decimate(dec), &pcm[i * sizeof(uint16_t)]);
	}

	return (int)(in_len * sizeof(uint16_t));
}
