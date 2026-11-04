/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include "codec.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

static uint8_t header[PROTOCOL_HEADER_SIZE];
static uint8_t payload[PROTOCOL_MAX_PAYLOAD];
static union {
	uint8_t bytes[PROTOCOL_MAX_PAYLOAD];
	int16_t s16[PROTOCOL_MAX_PAYLOAD / 2U];
	int32_t s24[PROTOCOL_MAX_PAYLOAD / 4U];
	float fp[PROTOCOL_MAX_PAYLOAD / 4U];
} output;
static union {
	int16_t s16[LC3_HR_MAX_FRAME_SAMPLES];
	int32_t s24[LC3_HR_MAX_FRAME_SAMPLES];
	uint8_t packed[LC3_HR_MAX_FRAME_SAMPLES * 3U];
	float fp[LC3_HR_MAX_FRAME_SAMPLES];
} pcm_input;
static size_t header_pos;
static size_t payload_pos;
static uint32_t payload_length;
static uint32_t discard;

static void *input_pcm(enum lc3_pcm_format format)
{
	switch (format) {
	case LC3_PCM_FORMAT_S16:
		return pcm_input.s16;
	case LC3_PCM_FORMAT_S24:
		return pcm_input.s24;
	case LC3_PCM_FORMAT_S24_3LE:
		return pcm_input.packed;
	case LC3_PCM_FORMAT_FLOAT:
		return pcm_input.fp;
	default:
		return NULL;
	}
}

static void setup_config(struct codec_config *config, size_t flags_offset)
{
	*config = (struct codec_config){
		.dt_us = (int32_t)sys_get_le32(payload),
		.sr_hz = (int32_t)sys_get_le32(payload + 4U),
		.sr_pcm_hz = (int32_t)sys_get_le32(payload + 8U),
		.hrmode = payload_length > flags_offset && payload[flags_offset] != 0U,
		.format = payload_length > flags_offset + 1U ?
			  (enum lc3_pcm_format)payload[flags_offset + 1U] : LC3_PCM_FORMAT_S16,
	};
}

static int encode_frames(bool batch)
{
	const struct codec_status *status = codec_encoder_status();
	uint32_t count = 1U;
	size_t offset = 0U;
	size_t pcm_size;
	void *pcm;
	int ret;

	if (!status->configured) {
		return -EPERM;
	}
	if (batch) {
		if (payload_length < 4U) {
			return -EINVAL;
		}
		count = sys_get_le32(payload);
		offset = 4U;
	}
	if (count == 0U || count > 100U) {
		return -EINVAL;
	}
	pcm_size = codec_pcm_size(status);
	if (payload_length - offset != count * pcm_size) {
		return -EINVAL;
	}
	if (count * (size_t)status->frame_bytes > sizeof(output.bytes)) {
		return -ENOSPC;
	}
	pcm = input_pcm(status->config.format);
	for (uint32_t index = 0U; index < count; index++) {
		memcpy(pcm, payload + offset + index * pcm_size, pcm_size);
		ret = codec_encode(pcm, pcm_size, output.bytes + index * status->frame_bytes,
				   status->frame_bytes);
		if (ret != 0) {
			return ret;
		}
	}
	return (int)(count * status->frame_bytes);
}

static int decode_frames(uint8_t command)
{
	const struct codec_status *status = codec_decoder_status();
	uint32_t count = 1U;
	size_t offset = 0U;
	size_t frame_size = payload_length;
	size_t pcm_size;
	int ret;

	if (!status->configured) {
		return -EPERM;
	}
	if (command == CMD_DEC_FRAMES) {
		if (payload_length < 4U) {
			return -EINVAL;
		}
		count = sys_get_le32(payload);
		offset = 4U;
	} else if (command == CMD_DEC_PLC) {
		if (payload_length != 0U && payload_length != 4U) {
			return -EINVAL;
		}
		count = payload_length == 4U ? sys_get_le32(payload) : 1U;
	}
	if (count == 0U || count > 100U) {
		return -EINVAL;
	}
	if (command != CMD_DEC_PLC) {
		if ((payload_length - offset) % count != 0U) {
			return -EINVAL;
		}
		frame_size = (payload_length - offset) / count;
		if (frame_size == 0U || frame_size > LC3_HR_MAX_FRAME_BYTES) {
			return -EINVAL;
		}
	}
	pcm_size = codec_pcm_size(status);
	if (count * pcm_size > sizeof(output.bytes)) {
		return -ENOSPC;
	}
	for (uint32_t index = 0U; index < count; index++) {
		const void *frame = command == CMD_DEC_PLC ? NULL :
				    payload + offset + index * frame_size;

		ret = codec_decode(frame, frame != NULL ? frame_size : 0U,
				   output.bytes + index * pcm_size, pcm_size);
		if (ret < 0) {
			return ret;
		}
	}
	return (int)(count * pcm_size);
}

static int query(void)
{
	bool hrmode;
	int32_t dt_us;
	int32_t rate;
	int32_t bitrate;
	int32_t channels;
	int32_t nbytes;
	int32_t values[7];

	if (payload_length != 25U) {
		return -EINVAL;
	}
	hrmode = payload[0] != 0U;
	dt_us = (int32_t)sys_get_le32(payload + 1U);
	rate = (int32_t)sys_get_le32(payload + 5U);
	bitrate = (int32_t)sys_get_le32(payload + 13U);
	channels = (int32_t)sys_get_le32(payload + 17U);
	nbytes = (int32_t)sys_get_le32(payload + 21U);
#if !defined(CONFIG_LIBLC3_PLUS_HR)
	if (hrmode) {
		return -ENOTSUP;
	}
#endif
#if !defined(CONFIG_LIBLC3_PLUS)
	if (dt_us == 2500 || dt_us == 5000) {
		return -ENOTSUP;
	}
#endif
	if (channels < 0 || channels > 8) {
		return -EINVAL;
	}
	values[0] = lc3_hr_frame_samples(hrmode, dt_us, rate);
	values[1] = lc3_hr_frame_bytes(hrmode, dt_us, rate, bitrate);
	values[2] = lc3_hr_frame_block_bytes(hrmode, dt_us, rate, channels, bitrate);
	values[3] = lc3_hr_resolve_bitrate(hrmode, dt_us, rate, nbytes);
	values[4] = lc3_hr_delay_samples(hrmode, dt_us, rate);
	values[5] = lc3_hr_encoder_size(hrmode, dt_us, rate);
	values[6] = lc3_hr_decoder_size(hrmode, dt_us, rate);
	for (size_t index = 0U; index < 7U; index++) {
		sys_put_le32((uint32_t)values[index], output.bytes + index * 4U);
	}
	return sizeof(values);
}

static int dispatch(void)
{
	struct codec_config config;
	const struct codec_status *enc = codec_encoder_status();
	const struct codec_status *dec = codec_decoder_status();
	int ret;

	switch (header[0]) {
	case CMD_ENC_SETUP:
		if (payload_length < 16U || payload_length > 19U) {
			return -EINVAL;
		}
		setup_config(&config, 16U);
		ret = codec_encoder_setup(&config, (int32_t)sys_get_le32(payload + 12U),
					  payload_length == 19U && payload[18] != 0U);
		if (ret != 0) {
			return ret;
		}
		sys_put_le32(enc->frame_samples, output.bytes);
		sys_put_le32(enc->frame_bytes, output.bytes + 4U);
		return 8;
	case CMD_DEC_SETUP:
		if (payload_length < 12U || payload_length > 14U) {
			return -EINVAL;
		}
		setup_config(&config, 12U);
		ret = codec_decoder_setup(&config);
		if (ret != 0) {
			return ret;
		}
		sys_put_le32(dec->frame_samples, output.bytes);
		return 4;
	case CMD_ENC_FRAME:
	case CMD_ENC_FRAMES:
		return encode_frames(header[0] == CMD_ENC_FRAMES);
	case CMD_DEC_FRAME:
	case CMD_DEC_FRAMES:
	case CMD_DEC_PLC:
		return decode_frames(header[0]);
	case CMD_QUERY:
		return query();
	case CMD_INFO:
		if (payload_length != 0U) {
			return -EINVAL;
		}
		return snprintf((char *)output.bytes, sizeof(output.bytes),
			"Zephyr liblc3; LC3plus=%d HR=%d LTPF-disable=unsupported\r\n"
			"=== LC3 Encoder ===\r\n"
			"  configured=%d hrmode=%d dt_us=%d sr_hz=%d sr_pcm_hz=%d\r\n"
			"  bitrate=%d frame_samples=%d frame_bytes=%d\r\n"
			"  frames_enc=%u errors=%u\r\n"
			"=== LC3 Decoder ===\r\n"
			"  configured=%d hrmode=%d dt_us=%d sr_hz=%d sr_pcm_hz=%d\r\n"
			"  frame_samples=%d frames_dec=%u plc=%u errors=%u\r\n",
#if defined(CONFIG_LIBLC3_PLUS)
			1,
#else
			0,
#endif
#if defined(CONFIG_LIBLC3_PLUS_HR)
			1,
#else
			0,
#endif
			enc->configured, enc->config.hrmode, enc->config.dt_us,
			enc->config.sr_hz, enc->config.sr_pcm_hz, enc->bitrate,
			enc->frame_samples, enc->frame_bytes, enc->frames, enc->errors,
			dec->configured, dec->config.hrmode, dec->config.dt_us,
			dec->config.sr_hz, dec->config.sr_pcm_hz,
			dec->frame_samples, dec->frames, dec->plc, dec->errors);
	case CMD_RESET:
		if (payload_length != 0U) {
			return -EINVAL;
		}
		codec_reset();
		return 0;
	default:
		return -EINVAL;
	}
}

static int send_error(protocol_send_fn send, int error)
{
	uint8_t message[80];
	int length;

	sys_put_le32((uint32_t)error, message);
	length = snprintf((char *)message + 4U, sizeof(message) - 4U,
			  "command 0x%02x: %s", header[0],
			  error == -ENOTSUP ? "mode not supported" : "invalid request or codec error");
	return send(PROTOCOL_ERROR, message, 4U + (size_t)length);
}

void protocol_reset(void)
{
	header_pos = 0U;
	payload_pos = 0U;
	payload_length = 0U;
	discard = 0U;
}

int protocol_feed(const uint8_t *data, size_t length, protocol_send_fn send)
{
	while (length > 0U) {
		size_t part;
		int ret;

		if (discard != 0U) {
			part = length < discard ? length : discard;
			discard -= part;
		} else if (header_pos < sizeof(header)) {
			part = sizeof(header) - header_pos;
			part = part < length ? part : length;
			memcpy(header + header_pos, data, part);
			header_pos += part;
			if (header_pos == sizeof(header)) {
				payload_length = sys_get_le32(header + 1U);
				if (payload_length > sizeof(payload)) {
					discard = payload_length;
					ret = send_error(send, -ENOSPC);
					header_pos = 0U;
					if (ret != 0) {
						return ret;
					}
				}
			}
		} else {
			part = payload_length - payload_pos;
			part = part < length ? part : length;
			memcpy(payload + payload_pos, data, part);
			payload_pos += part;
		}
		data += part;
		length -= part;
		if (header_pos == sizeof(header) && payload_pos == payload_length) {
			ret = dispatch();
			ret = ret < 0 ? send_error(send, ret) :
					send(header[0] | 0x80U, output.bytes, (size_t)ret);
			protocol_reset();
			if (ret != 0) {
				return ret;
			}
		}
	}
	return 0;
}
