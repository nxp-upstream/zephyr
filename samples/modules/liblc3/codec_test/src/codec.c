/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include "codec.h"

#include <errno.h>
#include <string.h>

static struct codec_status encoder_status;
static struct codec_status decoder_status;
static lc3_encoder_t encoder;
static lc3_decoder_t decoder;

int codec_config_check(const struct codec_config *config)
{
	if (config == NULL || config->format < LC3_PCM_FORMAT_S16 ||
	    config->format > LC3_PCM_FORMAT_FLOAT || config->sr_pcm_hz < 0) {
		return -EINVAL;
	}
#if !defined(CONFIG_LIBLC3_PLUS_HR)
	if (config->hrmode) {
		return -ENOTSUP;
	}
#endif
#if !defined(CONFIG_LIBLC3_PLUS)
	if (config->dt_us == 2500 || config->dt_us == 5000) {
		return -ENOTSUP;
	}
#endif
	if (lc3_hr_frame_samples(config->hrmode, config->dt_us, config->sr_hz) <= 0) {
		return -EINVAL;
	}
	if (config->sr_pcm_hz != 0 &&
	    (config->sr_pcm_hz < config->sr_hz ||
	     lc3_hr_frame_samples(config->hrmode, config->dt_us, config->sr_pcm_hz) <= 0)) {
		return -EINVAL;
	}
	return 0;
}

size_t codec_pcm_size(const struct codec_status *status)
{
	static const uint8_t widths[] = {2U, 4U, 3U, 4U};

	if (!status->configured) {
		return 0U;
	}
	return (size_t)status->frame_samples * widths[status->config.format];
}

int codec_encoder_setup(const struct codec_config *config, int32_t bitrate, bool disable_ltpf)
{
#if defined(CONFIG_LIBLC3_PLUS_HR)
	static LC3_ENCODER_MEM_T(10000, 96000) memory;
#else
	static lc3_encoder_mem_48k_t memory;
#endif
	int ret;
	int32_t pcm_rate;
	int32_t frame_bytes;

	encoder = NULL;
	memset(&encoder_status, 0, sizeof(encoder_status));
	if (disable_ltpf) {
		return -ENOTSUP;
	}
	ret = codec_config_check(config);
	if (ret != 0) {
		return ret;
	}
	if (bitrate <= 0) {
		return -EINVAL;
	}
	frame_bytes = lc3_hr_frame_bytes(config->hrmode, config->dt_us, config->sr_hz, bitrate);
	if (frame_bytes <= 0) {
		return -EINVAL;
	}
	pcm_rate = config->sr_pcm_hz != 0 ? config->sr_pcm_hz : config->sr_hz;
	if (lc3_hr_encoder_size(config->hrmode, config->dt_us, pcm_rate) > sizeof(memory)) {
		return -ENOMEM;
	}
	encoder = lc3_hr_setup_encoder(config->hrmode, config->dt_us, config->sr_hz,
				      pcm_rate, &memory);
	if (encoder == NULL) {
		return -EINVAL;
	}
	encoder_status.config = *config;
	encoder_status.config.sr_pcm_hz = pcm_rate;
	encoder_status.bitrate = bitrate;
	encoder_status.frame_samples = lc3_hr_frame_samples(config->hrmode, config->dt_us, pcm_rate);
	encoder_status.frame_bytes = frame_bytes;
	encoder_status.configured = true;
	return 0;
}

int codec_decoder_setup(const struct codec_config *config)
{
#if defined(CONFIG_LIBLC3_PLUS_HR)
	static LC3_DECODER_MEM_T(10000, 96000) memory;
#else
	static lc3_decoder_mem_48k_t memory;
#endif
	int ret;
	int32_t pcm_rate;

	decoder = NULL;
	memset(&decoder_status, 0, sizeof(decoder_status));
	ret = codec_config_check(config);
	if (ret != 0) {
		return ret;
	}
	pcm_rate = config->sr_pcm_hz != 0 ? config->sr_pcm_hz : config->sr_hz;
	if (lc3_hr_decoder_size(config->hrmode, config->dt_us, pcm_rate) > sizeof(memory)) {
		return -ENOMEM;
	}
	decoder = lc3_hr_setup_decoder(config->hrmode, config->dt_us, config->sr_hz,
				      pcm_rate, &memory);
	if (decoder == NULL) {
		return -EINVAL;
	}
	decoder_status.config = *config;
	decoder_status.config.sr_pcm_hz = pcm_rate;
	decoder_status.frame_samples = lc3_hr_frame_samples(config->hrmode, config->dt_us, pcm_rate);
	decoder_status.configured = true;
	return 0;
}

int codec_encode(const void *pcm, size_t pcm_size, void *output, size_t output_size)
{
	int ret;

	if (!encoder_status.configured) {
		return -EPERM;
	}
	if (pcm == NULL || output == NULL || pcm_size != codec_pcm_size(&encoder_status) ||
	    output_size < (size_t)encoder_status.frame_bytes) {
		return -EINVAL;
	}
	ret = lc3_encode(encoder, encoder_status.config.format, pcm, 1,
			 encoder_status.frame_bytes, output);
	if (ret == 0) {
		encoder_status.frames++;
	} else {
		encoder_status.errors++;
	}
	return ret;
}

int codec_decode(const void *frame, size_t frame_size, void *pcm, size_t pcm_size)
{
	int ret;

	if (!decoder_status.configured) {
		return -EPERM;
	}
	if (pcm == NULL || pcm_size < codec_pcm_size(&decoder_status) ||
	    frame_size > LC3_HR_MAX_FRAME_BYTES || (frame == NULL && frame_size != 0U) ||
	    (frame != NULL && frame_size == 0U)) {
		return -EINVAL;
	}
	ret = lc3_decode(decoder, frame, (int)frame_size, decoder_status.config.format, pcm, 1);
	if (ret >= 0) {
		decoder_status.frames++;
		if (ret == 1) {
			decoder_status.plc++;
		}
	} else {
		decoder_status.errors++;
	}
	return ret;
}

const struct codec_status *codec_encoder_status(void)
{
	return &encoder_status;
}

const struct codec_status *codec_decoder_status(void)
{
	return &decoder_status;
}

void codec_reset(void)
{
	encoder = NULL;
	decoder = NULL;
	memset(&encoder_status, 0, sizeof(encoder_status));
	memset(&decoder_status, 0, sizeof(decoder_status));
}
