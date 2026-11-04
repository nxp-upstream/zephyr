/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include "codec.h"
#include "protocol.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

static uint8_t response[PROTOCOL_MAX_PAYLOAD];
static uint8_t response_type;
static size_t response_length;
static uint32_t responses;

static int capture(uint8_t type, const void *data, size_t length)
{
	assert(length <= sizeof(response));
	memcpy(response, data, length);
	response_type = type;
	response_length = length;
	responses++;
	return 0;
}

static void request(uint8_t command, const void *data, size_t length)
{
	uint8_t header[PROTOCOL_HEADER_SIZE] = {command};
	uint32_t previous = responses;

	sys_put_le32(length, header + 1U);
	for (size_t index = 0U; index < sizeof(header); index++) {
		assert(protocol_feed(header + index, 1U, capture) == 0);
	}
	for (size_t index = 0U; index < length; index++) {
		assert(protocol_feed((const uint8_t *)data + index, 1U, capture) == 0);
	}
	assert(responses == previous + 1U);
}

static void test_protocol(void)
{
	uint8_t setup[19] = {0};
	uint8_t batch[4U + 2U * 480U * 4U] = {0};
	uint8_t encoded[640];
	uint8_t combined[10] = {CMD_INFO, 0, 0, 0, 0, CMD_RESET, 0, 0, 0, 0};
	uint8_t oversize[PROTOCOL_HEADER_SIZE] = {CMD_ENC_FRAME};
	uint8_t zeros[128] = {0};
	uint32_t remaining = PROTOCOL_MAX_PAYLOAD + 1U;

	codec_reset();
	protocol_reset();
	request(CMD_ENC_FRAME, zeros, sizeof(zeros));
	assert(response_type == PROTOCOL_ERROR);
	assert((int32_t)sys_get_le32(response) == -EPERM);
	sys_put_le32(10000U, setup);
	sys_put_le32(48000U, setup + 4U);
	sys_put_le32(48000U, setup + 8U);
	sys_put_le32(128000U, setup + 12U);
	for (uint8_t format = LC3_PCM_FORMAT_S16; format <= LC3_PCM_FORMAT_FLOAT; format++) {
		setup[17] = format;
		request(CMD_ENC_SETUP, setup, sizeof(setup));
		assert(response_type == (CMD_ENC_SETUP | 0x80U));
		assert(sys_get_le32(response) == 480U);
		sys_put_le32(2U, batch);
		request(CMD_ENC_FRAMES, batch, 4U + 2U * codec_pcm_size(codec_encoder_status()));
		assert(response_type == (CMD_ENC_FRAMES | 0x80U));
		assert(response_length == 320U);
		sys_put_le32(2U, encoded);
		memcpy(encoded + 4U, response, response_length);
		setup[13] = format;
		request(CMD_DEC_SETUP, setup, 14U);
		assert(response_type == (CMD_DEC_SETUP | 0x80U));
		request(CMD_DEC_FRAMES, encoded, 324U);
		assert(response_type == (CMD_DEC_FRAMES | 0x80U));
		assert(response_length == 2U * codec_pcm_size(codec_decoder_status()));
		request(CMD_DEC_PLC, NULL, 0U);
		assert(response_type == (CMD_DEC_PLC | 0x80U));
		request(CMD_DEC_FRAMES, encoded, 323U);
		assert(response_type == PROTOCOL_ERROR);
		setup[13] = 0xf4U;
	}
	setup[18] = 1U;
	request(CMD_ENC_SETUP, setup, sizeof(setup));
	assert(response_type == PROTOCOL_ERROR);
	assert((int32_t)sys_get_le32(response) == -ENOTSUP);
	setup[18] = 0U;
	setup[17] = 4U;
	request(CMD_ENC_SETUP, setup, sizeof(setup));
	assert(response_type == PROTOCOL_ERROR);
	assert((int32_t)sys_get_le32(response) == -EINVAL);
	request(CMD_DEC_PLC, zeros, 1U);
	assert(response_type == PROTOCOL_ERROR);
	request(0x77U, NULL, 0U);
	assert(response_type == PROTOCOL_ERROR);
	assert(protocol_feed(combined, sizeof(combined), capture) == 0);
	assert(response_type == (CMD_RESET | 0x80U));
	assert(response_length == 0U);
	sys_put_le32(remaining, oversize + 1U);
	assert(protocol_feed(oversize, sizeof(oversize), capture) == 0);
	assert((int32_t)sys_get_le32(response) == -ENOSPC);
	while (remaining > 0U) {
		size_t part = remaining < sizeof(zeros) ? remaining : sizeof(zeros);

		assert(protocol_feed(zeros, part, capture) == 0);
		remaining -= part;
	}
	request(CMD_INFO, NULL, 0U);
	assert(response_type == (CMD_INFO | 0x80U));
	puts("protocol tests passed");
}

static int stdio_send(uint8_t type, const void *data, size_t length)
{
	uint8_t header[PROTOCOL_HEADER_SIZE] = {type};

	sys_put_le32(length, header + 1U);
	if (fwrite(header, 1U, sizeof(header), stdout) != sizeof(header) ||
	    fwrite(data, 1U, length, stdout) != length || fflush(stdout) != 0) {
		return -EIO;
	}
	return 0;
}

static int stdio_server(void)
{
	int value;

	while ((value = getchar()) != EOF) {
		uint8_t byte = (uint8_t)value;

		if (protocol_feed(&byte, 1U, stdio_send) != 0) {
			return 1;
		}
	}
	return ferror(stdin) != 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct codec_config config = {
		.dt_us = 10000,
		.sr_hz = 16000,
		.sr_pcm_hz = 48000,
	};
	uint32_t input[960] = {0};
	uint32_t output[960];
	uint8_t frame[LC3_HR_MAX_FRAME_BYTES];
	const uint8_t widths[] = {2U, 4U, 3U, 4U};

	if (argc == 2 && strcmp(argv[1], "--stdio") == 0) {
		return stdio_server();
	}
	for (int32_t format = LC3_PCM_FORMAT_S16; format <= LC3_PCM_FORMAT_FLOAT; format++) {
		config.format = (enum lc3_pcm_format)format;
		assert(codec_encoder_setup(&config, 128000, false) == 0);
		assert(codec_decoder_setup(&config) == 0);
		assert(codec_encoder_status()->frame_samples == 480);
		assert(codec_decoder_status()->frame_samples == 480);
		assert(codec_pcm_size(codec_encoder_status()) == 480U * widths[format]);
		assert(codec_encode(input, 480U * widths[format], frame, sizeof(frame)) == 0);
		assert(codec_decode(frame, codec_encoder_status()->frame_bytes,
				    output, sizeof(output)) >= 0);
		assert(codec_decode(NULL, 0U, output, sizeof(output)) == 1);
		assert(codec_decoder_status()->plc == 1U);
		assert(codec_encode(input, 1U, frame, sizeof(frame)) == -EINVAL);
		assert(codec_decode(frame, sizeof(frame) + 1U, output, sizeof(output)) == -EINVAL);
	}
	assert(codec_encoder_setup(&config, 128000, true) == -ENOTSUP);
	config.hrmode = true;
#if defined(CONFIG_LIBLC3_PLUS_HR)
	config.sr_hz = 96000;
	config.sr_pcm_hz = 96000;
	assert(codec_encoder_setup(&config, 256000, false) == 0);
	assert(codec_encoder_status()->frame_samples == 960);
#else
	assert(codec_encoder_setup(&config, 128000, false) == -ENOTSUP);
#endif
	config.hrmode = false;
	config.sr_hz = 44100;
	assert(codec_encoder_setup(&config, 128000, false) == -EINVAL);
	codec_reset();
	assert(!codec_encoder_status()->configured);
	assert(!codec_decoder_status()->configured);
	assert(codec_encode(input, sizeof(input), frame, sizeof(frame)) == -EPERM);
	puts("codec tests passed");
	test_protocol();
	return 0;
}
