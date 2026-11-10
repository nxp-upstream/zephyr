/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/audio/audio_caps.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_pad.h>

#include <zephyr/mpipe/aud/mpipe_aud.h>

LOG_MODULE_REGISTER(mpipe_aud, CONFIG_MPIPE_LOG_LEVEL);

struct mpipe_aud_desc {
	uint32_t value;
	uint32_t mask;
};

/* Every AUDIO_SAMPLE_RATE_* bit, in increasing rate */
static const struct mpipe_aud_desc mpipe_aud_sample_rates[] = {
	{7350U, AUDIO_SAMPLE_RATE_7350},     {8000U, AUDIO_SAMPLE_RATE_8000},
	{11025U, AUDIO_SAMPLE_RATE_11025},   {12000U, AUDIO_SAMPLE_RATE_12000},
	{14700U, AUDIO_SAMPLE_RATE_14700},   {16000U, AUDIO_SAMPLE_RATE_16000},
	{20000U, AUDIO_SAMPLE_RATE_20000},   {22050U, AUDIO_SAMPLE_RATE_22050},
	{24000U, AUDIO_SAMPLE_RATE_24000},   {29400U, AUDIO_SAMPLE_RATE_29400},
	{32000U, AUDIO_SAMPLE_RATE_32000},   {44100U, AUDIO_SAMPLE_RATE_44100},
	{48000U, AUDIO_SAMPLE_RATE_48000},   {50000U, AUDIO_SAMPLE_RATE_50000},
	{50400U, AUDIO_SAMPLE_RATE_50400},   {64000U, AUDIO_SAMPLE_RATE_64000},
	{88200U, AUDIO_SAMPLE_RATE_88200},   {96000U, AUDIO_SAMPLE_RATE_96000},
	{100800U, AUDIO_SAMPLE_RATE_100800}, {128000U, AUDIO_SAMPLE_RATE_128000},
	{176400U, AUDIO_SAMPLE_RATE_176400}, {192000U, AUDIO_SAMPLE_RATE_192000},
	{352800U, AUDIO_SAMPLE_RATE_352800}, {384000U, AUDIO_SAMPLE_RATE_384000},
	{705600U, AUDIO_SAMPLE_RATE_705600}, {768000U, AUDIO_SAMPLE_RATE_768000},
};

/* Every AUDIO_BIT_WIDTH_* bit, in increasing width */
static const struct mpipe_aud_desc mpipe_aud_bit_widths[] = {
	{8U, AUDIO_BIT_WIDTH_8},   {16U, AUDIO_BIT_WIDTH_16}, {18U, AUDIO_BIT_WIDTH_18},
	{20U, AUDIO_BIT_WIDTH_20}, {24U, AUDIO_BIT_WIDTH_24}, {32U, AUDIO_BIT_WIDTH_32},
	{64U, AUDIO_BIT_WIDTH_64},
};

/* Value at n among the entries of a table a mask advertises, or 0 if it has fewer */
static uint32_t mpipe_aud_nth(const struct mpipe_aud_desc *table, size_t len, uint32_t mask,
			      uint32_t n)
{
	uint32_t matched = 0U;

	for (size_t i = 0U; i < len; i++) {
		if ((mask & table[i].mask) == 0U) {
			continue;
		}

		if (matched == n) {
			return table[i].value;
		}

		matched++;
	}

	return 0U;
}

/* A fixed value when min == max, a range otherwise */
static int mpipe_aud_append_span(struct mpipe_structure *out, uint8_t field_id, uint32_t min,
				 uint32_t max)
{
	struct mpipe_value fixed = MPIPE_VALUE_UINT(min);
	struct mpipe_value range = MPIPE_VALUE_UINT_RANGE(min, max, 1U);

	return mpipe_structure_append_value(out, field_id, (min == max) ? &fixed : &range);
}

int mpipe_aud_enum_caps(const struct audio_caps *caps, uint32_t index,
			const struct mpipe_structure *filter, struct mpipe_structure *out)
{
	struct mpipe_structure candidate;
	struct mpipe_value value;
	uint32_t num_widths = 0U;
	uint32_t sample_rate;
	uint32_t bit_width;
	int ret;

	if (caps == NULL) {
		return -EINVAL;
	}

	/* The index spans every bit width the caps advertise for each sample rate */
	for (size_t i = 0U; i < ARRAY_SIZE(mpipe_aud_bit_widths); i++) {
		if ((caps->supported_bit_widths & mpipe_aud_bit_widths[i].mask) != 0U) {
			num_widths++;
		}
	}

	if (num_widths == 0U) {
		return -ENOENT;
	}

	sample_rate = mpipe_aud_nth(mpipe_aud_sample_rates, ARRAY_SIZE(mpipe_aud_sample_rates),
				    caps->supported_sample_rates, index / num_widths);
	if (sample_rate == 0U) {
		return -ENOENT;
	}

	bit_width = mpipe_aud_nth(mpipe_aud_bit_widths, ARRAY_SIZE(mpipe_aud_bit_widths),
				  caps->supported_bit_widths, index % num_widths);
	if (bit_width == 0U) {
		return -ENOENT;
	}

	ret = mpipe_structure_init_fields(&candidate, MPIPE_MEDIA_AUDIO_PCM,
					  MPIPE_CAPS_SAMPLE_RATE, MPIPE_TYPE_UINT, sample_rate,
					  MPIPE_CAPS_BIT_WIDTH, MPIPE_TYPE_UINT, bit_width,
					  MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_aud_append_span(&candidate, MPIPE_CAPS_NUM_CHANNELS, caps->min_total_channels,
				    caps->max_total_channels);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_aud_append_span(&candidate, MPIPE_CAPS_FRAME_INTERVAL,
				    caps->min_frame_interval, caps->max_frame_interval);
	if (ret != 0) {
		return ret;
	}

	value = (struct mpipe_value)MPIPE_VALUE_BOOLEAN(caps->interleaved);
	ret = mpipe_structure_append_value(&candidate, MPIPE_CAPS_INTERLEAVED, &value);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

int mpipe_aud_caps_get_uint(const struct mpipe_structure *caps, uint8_t field_id, uint32_t *out)
{
	const struct mpipe_value *value = mpipe_structure_get_value(caps, field_id);

	if (out == NULL) {
		return -EINVAL;
	}

	if (value == NULL) {
		LOG_ERR("Capability carries no field %u", field_id);
		return -ENOENT;
	}

	if (value->type != MPIPE_TYPE_UINT) {
		LOG_ERR("Field %u is not a fixed unsigned value", field_id);
		return -EINVAL;
	}

	*out = mpipe_value_get_uint(value);

	return 0;
}
