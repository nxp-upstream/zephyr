/* main.c - CVSD codec tests */

/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "cvsd.h"

#define PCM_RATE 8000

/* One period of a 250 Hz tone. A stride over this table gives exact multiples of
 * 250 Hz without needing a sine function, so the tests stay integer only.
 */
#define TONE_PERIOD 32

static const int16_t tone_table[TONE_PERIOD] = {
	      0,    6393,   12539,   18204,   23170,   27245,   30273,   32137,
	  32767,   32137,   30273,   27245,   23170,   18204,   12539,    6393,
	      0,   -6393,  -12539,  -18204,  -23170,  -27245,  -30273,  -32137,
	 -32767,  -32137,  -30273,  -27245,  -23170,  -18204,  -12539,   -6393,
};

#define SAMPLES 2048

/* Samples skipped while the resampling filters and the step size settle. The
 * codec delay is about 16 samples, so this leaves ample margin.
 */
#define SETTLE 256

/* Delays searched when aligning the decoded signal with the input. */
#define MAX_DELAY 48

static struct cvsd_encoder enc;
static struct cvsd_decoder dec;
static uint8_t pcm_in[SAMPLES * sizeof(int16_t)];
static uint8_t pcm_out[SAMPLES * sizeof(int16_t)];
static uint8_t coded[SAMPLES];
static uint8_t coded_again[SAMPLES];

static int16_t pcm_get(const uint8_t *pcm, size_t index)
{
	return (int16_t)sys_get_le16(&pcm[index * sizeof(int16_t)]);
}

static void pcm_set(uint8_t *pcm, size_t index, int32_t value)
{
	sys_put_le16((uint16_t)(int16_t)value, &pcm[index * sizeof(int16_t)]);
}

/* Fill pcm_in with a tone of PCM_RATE / TONE_PERIOD * stride Hz. */
static void gen_tone(size_t stride, int32_t amplitude)
{
	for (size_t i = 0; i < SAMPLES; i++) {
		int32_t value = (tone_table[(i * stride) % TONE_PERIOD] * amplitude) / 32768;

		pcm_set(pcm_in, i, value);
	}
}

static void gen_constant(int32_t value)
{
	for (size_t i = 0; i < SAMPLES; i++) {
		pcm_set(pcm_in, i, value);
	}
}

static void loopback(void)
{
	int err;

	cvsd_encoder_init(&enc);
	cvsd_decoder_init(&dec);

	err = cvsd_encoder_encode(&enc, pcm_in, sizeof(pcm_in), coded, sizeof(coded));
	zassert_equal(err, (int)SAMPLES, "encode returned %d", err);

	err = cvsd_decoder_decode(&dec, coded, sizeof(coded), pcm_out, sizeof(pcm_out));
	zassert_equal(err, (int)sizeof(pcm_out), "decode returned %d", err);
}

/*
 * Signal to noise power ratio of the loopback, maximized over the codec delay
 * and corrected for its level error. Returned as a plain power ratio, so 100 is
 * 20 dB and 1000 is 30 dB.
 */
static double loopback_snr(void)
{
	double best = 0.0;

	for (size_t delay = 0; delay < MAX_DELAY; delay++) {
		double xx = 0.0;
		double xy = 0.0;
		double signal = 0.0;
		double error = 0.0;
		double gain;

		for (size_t i = SETTLE; (i + delay) < SAMPLES; i++) {
			double x = (double)pcm_get(pcm_in, i);
			double y = (double)pcm_get(pcm_out, i + delay);

			xx += x * x;
			xy += x * y;
		}

		if (xx == 0.0) {
			continue;
		}

		gain = xy / xx;

		for (size_t i = SETTLE; (i + delay) < SAMPLES; i++) {
			double x = (double)pcm_get(pcm_in, i) * gain;
			double e = (double)pcm_get(pcm_out, i + delay) - x;

			signal += x * x;
			error += e * e;
		}

		if ((error > 0.0) && ((signal / error) > best)) {
			best = signal / error;
		}
	}

	return best;
}

ZTEST_SUITE(cvsd, NULL, NULL, NULL, NULL, NULL);

/* One encoded byte per PCM sample and two PCM bytes per encoded byte. */
ZTEST(cvsd, test_size_contract)
{
	cvsd_encoder_init(&enc);
	cvsd_decoder_init(&dec);

	zassert_equal(cvsd_encoder_encode(&enc, pcm_in, 64, coded, sizeof(coded)), 32);
	zassert_equal(cvsd_decoder_decode(&dec, coded, 32, pcm_out, sizeof(pcm_out)), 64);

	zassert_equal(cvsd_encoder_encode(&enc, pcm_in, 0, coded, sizeof(coded)), 0);
	zassert_equal(cvsd_decoder_decode(&dec, coded, 0, pcm_out, sizeof(pcm_out)), 0);
}

ZTEST(cvsd, test_invalid_arguments)
{
	cvsd_encoder_init(&enc);
	cvsd_decoder_init(&dec);

	zassert_equal(cvsd_encoder_encode(NULL, pcm_in, 64, coded, sizeof(coded)), -EINVAL);
	zassert_equal(cvsd_encoder_encode(&enc, NULL, 64, coded, sizeof(coded)), -EINVAL);
	zassert_equal(cvsd_encoder_encode(&enc, pcm_in, 64, NULL, sizeof(coded)), -EINVAL);
	zassert_equal(cvsd_decoder_decode(NULL, coded, 32, pcm_out, sizeof(pcm_out)), -EINVAL);
	zassert_equal(cvsd_decoder_decode(&dec, NULL, 32, pcm_out, sizeof(pcm_out)), -EINVAL);
	zassert_equal(cvsd_decoder_decode(&dec, coded, 32, NULL, sizeof(pcm_out)), -EINVAL);

	/* An odd PCM length does not hold whole samples. */
	zassert_equal(cvsd_encoder_encode(&enc, pcm_in, 63, coded, sizeof(coded)), -EINVAL);

	zassert_equal(cvsd_encoder_encode(&enc, pcm_in, 64, coded, 31), -ENOSPC);
	zassert_equal(cvsd_decoder_decode(&dec, coded, 32, pcm_out, 63), -ENOSPC);
}

/*
 * Tone loopback. The amplitudes stay inside the slope budget of the delta
 * modulator, which is delta_max * 64 kHz and therefore falls at 6 dB per octave:
 * a tone of frequency f can only be tracked up to 81.92e6 / (2 * pi * f).
 */
ZTEST(cvsd, test_tone_snr)
{
	static const struct {
		size_t stride;
		int32_t amplitude;
		int32_t min_snr;
	} cases[] = {
		{1, 16000, 1000}, /*  250 Hz, slope budget 52000 */
		{2, 12000, 1000}, /*  500 Hz, slope budget 26000 */
		{4, 8000, 500},   /* 1000 Hz, slope budget 13036 */
		{8, 4000, 500},   /* 2000 Hz, slope budget 6518 */
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		double snr;

		gen_tone(cases[i].stride, cases[i].amplitude);
		loopback();
		snr = loopback_snr();

		TC_PRINT("%4u Hz at %6d: SNR %d (min %d)\n",
			 (unsigned int)(PCM_RATE / TONE_PERIOD * cases[i].stride),
			 cases[i].amplitude, (int)snr, cases[i].min_snr);
		zassert_true(snr >= (double)cases[i].min_snr, "tone %u SNR %d below %d",
			     (unsigned int)i, (int)snr, cases[i].min_snr);
	}
}

/*
 * A constant input must come back as the same constant. Polyphase branches with
 * unequal DC gain would instead show up as a ripple at the PCM rate, so the
 * peak to peak spread is checked as well.
 */
ZTEST(cvsd, test_constant)
{
	static const int32_t levels[] = {6000, -6000, 20000};

	for (size_t i = 0; i < ARRAY_SIZE(levels); i++) {
		int32_t low = INT16_MAX;
		int32_t high = INT16_MIN;
		int64_t sum = 0;
		int32_t mean;

		gen_constant(levels[i]);
		loopback();

		for (size_t n = SETTLE; n < SAMPLES; n++) {
			int32_t value = pcm_get(pcm_out, n);

			low = MIN(low, value);
			high = MAX(high, value);
			sum += value;
		}

		mean = (int32_t)(sum / (int64_t)(SAMPLES - SETTLE));
		TC_PRINT("DC %6d: mean %6d, ripple %5d\n", levels[i], mean, high - low);

		/* Within 5 percent of the input, ripple under 2 percent of it. */
		zassert_within(mean, levels[i], abs(levels[i]) / 20, "DC %d decoded as %d",
			       levels[i], mean);
		zassert_true((high - low) < (abs(levels[i]) / 50), "DC %d ripple %d too large",
			     levels[i], high - low);
	}
}

ZTEST(cvsd, test_silence)
{
	int32_t peak = 0;

	gen_constant(0);
	loopback();

	for (size_t i = SETTLE; i < SAMPLES; i++) {
		peak = MAX(peak, abs(pcm_get(pcm_out, i)));
	}

	TC_PRINT("silence: peak %d\n", peak);
	zassert_true(peak <= 64, "silence decoded with peak %d", peak);
}

/*
 * The decoder rebuilds the same reconstruction the encoder predicted from, so
 * after a loopback both must hold the exact same state. This is what catches a
 * mismatch between how bits are packed and how they are unpacked.
 */
ZTEST(cvsd, test_states_track)
{
	gen_tone(4, 8000);
	loopback();

	zassert_equal(dec.state.accumulator, enc.state.accumulator, "accumulator %d != %d",
		      dec.state.accumulator, enc.state.accumulator);
	zassert_equal(dec.state.step, enc.state.step, "step %d != %d", dec.state.step,
		      enc.state.step);
	zassert_equal(dec.state.bit_history, enc.state.bit_history, "bit history %u != %u",
		      dec.state.bit_history, enc.state.bit_history);
}

ZTEST(cvsd, test_deterministic)
{
	int err;

	gen_tone(4, 8000);
	loopback();

	cvsd_encoder_init(&enc);
	err = cvsd_encoder_encode(&enc, pcm_in, sizeof(pcm_in), coded_again, sizeof(coded_again));
	zassert_equal(err, (int)SAMPLES, "re-encode returned %d", err);
	zassert_mem_equal(coded, coded_again, sizeof(coded),
			  "a reset encoder produced a different bitstream");
}

/*
 * The codec is used one SCO frame at a time, so splitting a buffer over several
 * calls has to give the same result as encoding or decoding it in one go.
 */
ZTEST(cvsd, test_frame_continuity)
{
	static const size_t chunk = 32; /* PCM samples, one 4 ms eSCO frame */
	int err;

	gen_tone(4, 8000);
	loopback();

	cvsd_encoder_init(&enc);
	for (size_t i = 0; i < SAMPLES; i += chunk) {
		err = cvsd_encoder_encode(&enc, &pcm_in[i * sizeof(int16_t)],
					  chunk * sizeof(int16_t), &coded_again[i],
					  sizeof(coded_again) - i);
		zassert_equal(err, (int)chunk, "chunked encode returned %d", err);
	}
	zassert_mem_equal(coded, coded_again, sizeof(coded),
			  "chunked encoding differs from a single call");

	/* Reuse pcm_in as the destination of the chunked decode. */
	cvsd_decoder_init(&dec);
	for (size_t i = 0; i < SAMPLES; i += chunk) {
		err = cvsd_decoder_decode(&dec, &coded[i], chunk, &pcm_in[i * sizeof(int16_t)],
					  sizeof(pcm_in) - (i * sizeof(int16_t)));
		zassert_equal(err, (int)(chunk * sizeof(int16_t)), "chunked decode returned %d",
			      err);
	}
	zassert_mem_equal(pcm_in, pcm_out, sizeof(pcm_out),
			  "chunked decoding differs from a single call");
}

/* A full scale square wave drives the step size to its maximum and the
 * accumulator against its limits without leaving the 16 bit range.
 */
ZTEST(cvsd, test_slope_limit)
{
	for (size_t i = 0; i < SAMPLES; i++) {
		pcm_set(pcm_in, i, ((i / 4U) % 2U) == 0U ? INT16_MAX : INT16_MIN);
	}

	loopback();

	TC_PRINT("square wave: step %d, accumulator %d\n", enc.state.step, enc.state.accumulator);
	zassert_equal(enc.state.step, 1280, "step did not saturate: %d", enc.state.step);
	zassert_between_inclusive(enc.state.accumulator, INT16_MIN, INT16_MAX,
				  "accumulator %d left the 16 bit range", enc.state.accumulator);
}
