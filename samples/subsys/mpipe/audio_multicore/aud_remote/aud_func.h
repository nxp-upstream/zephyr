/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Audio functions the remote server runs on request of the Cortex-M33. The
 * server opens an instance of a function for each client element and calls
 * it with that instance's state; the function sees only plain memory.
 *
 * The sample rate, the functions the server provides and their parameters
 * are Kconfig options (CONFIG_AUD_REMOTE_*), set by the sysbuild.cmake of
 * each sample. The channel count and the block duration are negotiated: the
 * Cortex-M33 sets them with each format, within the limits below, which are
 * Kconfig options set in prj.conf.
 */

#ifndef AUD_FUNC_H_
#define AUD_FUNC_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>

#include "mpipe_aud_transform_common.h"

/* Sample rate of the blocks the Cortex-M33 sends */
#define AUD_SAMPLE_RATE CONFIG_AUD_REMOTE_SAMPLE_RATE

/* Parameters of the functions that take one */
#if defined(CONFIG_AUD_REMOTE_FUNC_GAIN)
#define AUD_GAIN_PERCENT CONFIG_AUD_REMOTE_GAIN_PERCENT
#endif

/* Sample width, the only one the MICFIL driver of the Cortex-M33 produces */
#define AUD_PCM_BIT_WIDTH CONFIG_AUD_REMOTE_PCM_BIT_WIDTH

/* Most channels of a block: ifft writes them back to the two sides of a stereo block */
#define AUD_MAX_CHANNELS CONFIG_AUD_REMOTE_MAX_CHANNELS

/*
 * Samples of one channel in one block, always a power of two (the FFT
 * length). The buffers and the FFT work memory are sized for the largest
 * block.
 */
#define AUD_MIN_BLOCK_SAMPLES CONFIG_AUD_REMOTE_MIN_BLOCK_SAMPLES
#define AUD_MAX_BLOCK_SAMPLES CONFIG_AUD_REMOTE_MAX_BLOCK_SAMPLES

/* Block durations the limits above give at AUD_SAMPLE_RATE, in microseconds */
#define AUD_MIN_FRAME_INTERVAL_US                                                                  \
	((uint32_t)DIV_ROUND_UP((uint64_t)AUD_MIN_BLOCK_SAMPLES * USEC_PER_SEC, AUD_SAMPLE_RATE))
#define AUD_MAX_FRAME_INTERVAL_US                                                                  \
	((uint32_t)(((uint64_t)AUD_MAX_BLOCK_SAMPLES * USEC_PER_SEC) / AUD_SAMPLE_RATE))

/* Bytes of one sample of one channel */
#define AUD_PCM_SAMPLE_SIZE (AUD_PCM_BIT_WIDTH / 8U)

/* Bytes in one PCM block of ch channels of n samples, the channels interleaved */
#define AUD_PCM_SIZE(ch, n) ((uint32_t)(n) * AUD_PCM_SAMPLE_SIZE * (uint32_t)(ch))

/*
 * The FFT of one block: for each channel, the bins from DC to Nyquist, a
 * float real and imaginary part each. The spectrum of each channel follows
 * that of the previous one, channel 0 first.
 */
#define AUD_FFT_BINS(n)        ((uint32_t)(n) / 2U + 1U)
#define AUD_FFT_CHANNEL_SIZE(n) (AUD_FFT_BINS(n) * 2U * (uint32_t)sizeof(float))
#define AUD_FFT_SIZE(ch, n)    (AUD_FFT_CHANNEL_SIZE(n) * (uint32_t)(ch))

/* Buffers are maintained by cache line: keep each one on lines of its own */
#define AUD_BUF_ALIGN CONFIG_AUD_REMOTE_BUF_ALIGN

/*
 * Every buffer has the same size, whatever function and format it serves:
 * the largest block of any format, PCM or an FFT, of AUD_MAX_CHANNELS
 * channels of AUD_MAX_BLOCK_SAMPLES samples.
 */
#define AUD_BUF_SIZE                                                                               \
	ROUND_UP(MAX(AUD_PCM_SIZE(AUD_MAX_CHANNELS, AUD_MAX_BLOCK_SAMPLES),                       \
		     AUD_FFT_SIZE(AUD_MAX_CHANNELS, AUD_MAX_BLOCK_SAMPLES)),                       \
		 AUD_BUF_ALIGN)

struct aud_func {
	/* Value the client opens the function with */
	mpipe_rpc_aud_func id;
	/* Name in the console messages */
	const char *name;
	/* The formats each pad takes */
	const mpipe_rpc_aud_caps *in_cap;
	const mpipe_rpc_aud_caps *out_cap;
	/* True when the output has the channel count of the input */
	bool keeps_channels;
	/* State of a new instance, or NULL when the function has none left */
	void *(*open)(void);
	/* Start a new stream from silence */
	void (*reset)(void *state);
	/*
	 * Process in_sz bytes of in, a block of num_channels channels of
	 * num_samples samples each, into out; *out_sz is set to the bytes
	 * written
	 */
	int32_t (*process)(void *state, uint8_t num_channels, uint32_t num_samples,
			   const uint8_t *in, uint32_t in_sz, uint8_t *out, uint32_t *out_sz);
};

extern const struct aud_func aud_func_gain;
extern const struct aud_func aud_func_fft;
extern const struct aud_func aud_func_ifft;

/* Formats the functions take, defined in main.c */
/* PCM of 1 to AUD_MAX_CHANNELS channels, as captured */
extern const mpipe_rpc_aud_caps aud_pcm_cap;
/* The spectra of 1 to AUD_MAX_CHANNELS channels */
extern const mpipe_rpc_aud_caps aud_fft_cap;
/* Stereo PCM, as played back */
extern const mpipe_rpc_aud_caps aud_stereo_cap;

#endif /* AUD_FUNC_H_ */
