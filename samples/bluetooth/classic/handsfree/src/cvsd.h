/* cvsd.h - CVSD codec for Bluetooth Hands-Free Profile */

/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ZEPHYR_SAMPLES_CLASSIC_HANDSFREE_CVSD_H_
#define _ZEPHYR_SAMPLES_CLASSIC_HANDSFREE_CVSD_H_

#include <stddef.h>
#include <stdint.h>

/*
 * CVSD on a SCO or eSCO link occupies 64 kbit/s and carries narrowband voice
 * sampled at 8 kHz, so one CVSD bit covers one 64 kHz sample and eight CVSD
 * bits cover one PCM sample. The codec resamples between the two rates
 * internally, which fixes the size relation between both sides: one encoded
 * byte per PCM sample and two PCM bytes per encoded byte.
 *
 * PCM is 16-bit little-endian and is addressed as bytes, so buffers taken
 * straight out of a ring buffer need no particular alignment.
 */

/* Ratio between the CVSD sample rate (64 kHz) and the PCM sample rate (8 kHz) */
#define CVSD_OVERSAMPLING 8

/* Taps of the band-limiting filter shared by the interpolator and the decimator */
#define CVSD_FILTER_TAPS 128

/* Taps of a single polyphase branch of the interpolator */
#define CVSD_FILTER_PHASE_TAPS (CVSD_FILTER_TAPS / CVSD_OVERSAMPLING)

/* Delta modulator state, driven identically by the encoder and the decoder */
struct cvsd_state {
	int32_t accumulator; /* Reconstructed sample, y(k) */
	int32_t step;        /* Step size, delta(k) */
	uint8_t bit_history; /* Last K bits, bit 0 being the most recent one */
};

struct cvsd_encoder {
	struct cvsd_state state;
	/* 8 kHz input history of the interpolator, index 0 being the newest sample */
	int16_t history[CVSD_FILTER_PHASE_TAPS];
};

struct cvsd_decoder {
	struct cvsd_state state;
	/* 64 kHz delay line of the decimator, used as a circular buffer */
	int16_t history[CVSD_FILTER_TAPS];
	/* Slot holding the oldest sample, which is also the next one to write */
	uint8_t index;
};

/* Reset the codec state. Both directions are stateful, so a new SCO connection
 * must start from a reset encoder and decoder.
 */
void cvsd_encoder_init(struct cvsd_encoder *enc);
void cvsd_decoder_init(struct cvsd_decoder *dec);

/* Encode pcm_len bytes of 16-bit little-endian PCM into out.
 *
 * out must hold at least pcm_len / 2 bytes. Returns the number of bytes
 * written, -EINVAL if pcm_len is odd or an argument is NULL, or -ENOSPC if out
 * is too small.
 */
int cvsd_encoder_encode(struct cvsd_encoder *enc, const uint8_t *pcm, size_t pcm_len, uint8_t *out,
			size_t out_size);

/* Decode in_len bytes of CVSD into 16-bit little-endian PCM.
 *
 * pcm must hold at least in_len * 2 bytes. Returns the number of bytes
 * written, -EINVAL if an argument is NULL, or -ENOSPC if pcm is too small.
 */
int cvsd_decoder_decode(struct cvsd_decoder *dec, const uint8_t *in, size_t in_len, uint8_t *pcm,
			size_t pcm_size);

#endif /* _ZEPHYR_SAMPLES_CLASSIC_HANDSFREE_CVSD_H_ */
