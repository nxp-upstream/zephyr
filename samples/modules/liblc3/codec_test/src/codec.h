/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LC3_TEST_CODEC_H_
#define LC3_TEST_CODEC_H_

#include <lc3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct codec_config {
	bool hrmode;
	int32_t dt_us;
	int32_t sr_hz;
	int32_t sr_pcm_hz;
	enum lc3_pcm_format format;
};

struct codec_status {
	struct codec_config config;
	bool configured;
	int32_t bitrate;
	int32_t frame_samples;
	int32_t frame_bytes;
	uint32_t frames;
	uint32_t errors;
	uint32_t plc;
};

int codec_config_check(const struct codec_config *config);
size_t codec_pcm_size(const struct codec_status *status);
int codec_encoder_setup(const struct codec_config *config, int32_t bitrate, bool disable_ltpf);
int codec_decoder_setup(const struct codec_config *config);
int codec_encode(const void *pcm, size_t pcm_size, void *output, size_t output_size);
int codec_decode(const void *frame, size_t frame_size, void *pcm, size_t pcm_size);
const struct codec_status *codec_encoder_status(void);
const struct codec_status *codec_decoder_status(void);
void codec_reset(void);

#endif
