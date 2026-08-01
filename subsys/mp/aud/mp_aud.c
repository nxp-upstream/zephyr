/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/audio/audio_caps.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/aud/mp_aud.h>

LOG_MODULE_REGISTER(mp_aud, CONFIG_MP_LOG_LEVEL);

struct mp_aud_desc {
	uint32_t value;
	uint32_t mask;
};

static const struct mp_aud_desc mp_aud_sample_rates[] = {
	{MP_AUD_SAMPLE_RATE_8000, AUDIO_SAMPLE_RATE_8000},
	{MP_AUD_SAMPLE_RATE_16000, AUDIO_SAMPLE_RATE_16000},
	{MP_AUD_SAMPLE_RATE_32000, AUDIO_SAMPLE_RATE_32000},
	{MP_AUD_SAMPLE_RATE_44100, AUDIO_SAMPLE_RATE_44100},
	{MP_AUD_SAMPLE_RATE_48000, AUDIO_SAMPLE_RATE_48000},
	{MP_AUD_SAMPLE_RATE_96000, AUDIO_SAMPLE_RATE_96000},
};

static const struct mp_aud_desc mp_aud_bit_widths[] = {
	{MP_AUD_BIT_WIDTH_16, AUDIO_BIT_WIDTH_16},
	{MP_AUD_BIT_WIDTH_24, AUDIO_BIT_WIDTH_24},
	{MP_AUD_BIT_WIDTH_32, AUDIO_BIT_WIDTH_32},
};

uint32_t audio2mp_sample_rate(uint32_t sample_rate_mask)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(mp_aud_sample_rates); i++) {
		if (mp_aud_sample_rates[i].mask == sample_rate_mask) {
			return mp_aud_sample_rates[i].value;
		}
	}

	return 0;
}

uint32_t audio2mp_bit_width(uint32_t bit_width_mask)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(mp_aud_bit_widths); i++) {
		if (mp_aud_bit_widths[i].mask == bit_width_mask) {
			return mp_aud_bit_widths[i].value;
		}
	}

	return 0;
}

uint32_t mp_aud_count_bit_widths(uint32_t bit_width_mask)
{
	uint32_t count = 0;

	for (uint8_t i = 0; i < 32U; i++) {
		if ((bit_width_mask & BIT(i)) != 0U && audio2mp_bit_width(BIT(i)) > 0U) {
			count++;
		}
	}

	return count;
}

uint32_t mp_aud_nth_sample_rate(uint32_t sample_rate_mask, uint32_t n)
{
	uint32_t matched = 0;

	for (uint8_t i = 0; i < 32U; i++) {
		uint32_t sr;

		if ((sample_rate_mask & BIT(i)) == 0U) {
			continue;
		}

		sr = audio2mp_sample_rate(BIT(i));
		if (sr == 0U) {
			continue;
		}

		if (matched == n) {
			return sr;
		}

		matched++;
	}

	return 0;
}

uint32_t mp_aud_nth_bit_width(uint32_t bit_width_mask, uint32_t n)
{
	uint32_t matched = 0;

	for (uint8_t i = 0; i < 32U; i++) {
		uint32_t bw;

		if ((bit_width_mask & BIT(i)) == 0U) {
			continue;
		}

		bw = audio2mp_bit_width(BIT(i));
		if (bw == 0U) {
			continue;
		}

		if (matched == n) {
			return bw;
		}

		matched++;
	}

	return 0;
}

int mp_aud_get_uint(const struct mp_structure *caps, uint8_t field_id, uint32_t *out)
{
	const struct mp_value *value = mp_structure_get_value(caps, field_id);

	if (out == NULL) {
		return -EINVAL;
	}

	if (value == NULL) {
		LOG_ERR("Capability carries no field %u", field_id);
		return -ENOENT;
	}

	if (value->type != MP_TYPE_UINT) {
		LOG_ERR("Field %u is not a fixed unsigned value", field_id);
		return -EINVAL;
	}

	*out = mp_value_get_uint(value);

	return 0;
}
