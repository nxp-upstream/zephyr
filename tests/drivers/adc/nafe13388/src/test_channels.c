/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * channel_setup, channel_setup_ext and cfg_rate: the frames that are sent and the
 * register values that arrive. Expected values are computed from the CH_CONFIG0..3
 * bit layout of the data sheet (Table 29), not taken from the driver.
 */

#include <errno.h>

#include "common.h"

ZTEST_SUITE(nafe13388_channels, NULL, NULL, nafe_before, nafe_after, NULL);

static struct adc_channel_cfg base_cfg(uint8_t ch)
{
	struct adc_channel_cfg cfg = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = ch,
		.differential = 1,
		.input_positive = 1,
		.input_negative = 7,
	};

	return cfg;
}

/* The channels of the devicetree are set up again by the RESET of the "before" hook */
ZTEST(nafe13388_channels, test_dt_channels_after_reset)
{
	/* The log was cleared after the RESET, so check the registers instead */
	zassert_equal(D1->ch[0][0], 0x1770, "HV_AIP 1, HV_AIN 7, gain 1x, HV_SEL");
	zassert_equal(D1->ch[0][1], 0x0000);
	zassert_equal(D1->ch[1][0], 0x2770, "HV_AIP 2, HV_AIN 7, gain 1x, HV_SEL");
	zassert_equal(D1->ch[2][0], 0x0000, "channel 2 is not in the devicetree");
	zassert_equal(D1->sys_config0, 0x0010, "only DRDY_PIN_EDGE");
	zassert_equal(D1->mch_en, 0);
}

ZTEST(nafe13388_channels, test_channel_setup_differential)
{
	struct adc_channel_cfg cfg = base_cfg(5);

	cfg.gain = ADC_GAIN_4;
	cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 3);
	cfg.input_positive = 3;
	cfg.input_negative = 4;

	zassert_ok(adc_channel_setup(NAFE1, &cfg));

	zassert_equal(nafe_emul_frame_count(E1), 5);
	ZASSERT_FRAME(E1, 0, 0x80, 0x0A); /* CMD_CH5 */
	/* HV_AIP 3 << 12 | HV_AIN 4 << 8 | gain 4x (code 5) << 5 | HV_SEL = 0x34B0 */
	ZASSERT_FRAME(E1, 1, 0x80, 0x40, 0x34, 0xB0);
	ZASSERT_FRAME(E1, 2, 0x80, 0x42, 0x00, 0x18); /* rate code 3 << 3 */
	ZASSERT_FRAME(E1, 3, 0x80, 0x44, 0x00, 0x00);
	ZASSERT_FRAME(E1, 4, 0x80, 0x46, 0x00, 0x00);
	zassert_equal(D1->ch[5][0], 0x34B0);
	zassert_equal(D1->ch[5][1], 0x0018);
}

/*
 * Single ended HV channels are refused: the code is the same for every input type, so
 * adc_raw_to_millivolts() would give half the voltage.
 */
ZTEST(nafe13388_channels, test_channel_setup_single_ended_refused)
{
	struct adc_channel_cfg cfg = base_cfg(6);

	cfg.differential = 0;
	cfg.input_positive = 2;
	cfg.input_negative = 0;

	zassert_equal(adc_channel_setup(NAFE1, &cfg), -ENOTSUP);
	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused setup sends nothing");

	/* The replacement: differential against the internal ground */
	cfg.differential = 1;
	zassert_ok(adc_channel_setup(NAFE1, &cfg));
	ZASSERT_FRAME(E1, 0, 0x80, 0x0C);             /* CMD_CH6 */
	ZASSERT_FRAME(E1, 1, 0x80, 0x40, 0x20, 0x70); /* HV_AIP 2, HV_AIN 0, 1x, HV_SEL */
}

/* The excitation current source pins go to VIEX_AIP_EN / VIEX_AIN_EN of CH_CONFIG3 */
ZTEST(nafe13388_channels, test_channel_setup_excitation_pins)
{
	struct adc_channel_cfg cfg = base_cfg(7);

	cfg.current_source_pin_set = 1;
	cfg.current_source_pin[0] = 1;
	cfg.current_source_pin[1] = 2;

	zassert_ok(adc_channel_setup(NAFE1, &cfg));

	ZASSERT_FRAME(E1, 4, 0x80, 0x46, 0x00, 0x0A); /* 1 << 3 | 2 */
}

ZTEST(nafe13388_channels, test_channel_setup_errors)
{
	struct adc_channel_cfg cfg;

	cfg = base_cfg(5);
	cfg.gain = ADC_GAIN_3;
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "gain 3 does not exist");

	cfg = base_cfg(5);
	cfg.reference = ADC_REF_VDD_1;
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "only the internal reference");

	cfg = base_cfg(5);
	cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 5);
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "only ticks");

	cfg = base_cfg(5);
	cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, NAFE13388_DATA_RATE_CODE_MAX + 1);
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "rate code 29");

	cfg = base_cfg(16);
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "channel 16");

	cfg = base_cfg(5);
	cfg.input_positive = 9;
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "input 9");

	cfg = base_cfg(5);
	cfg.input_negative = 9;
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "input 9");

	cfg = base_cfg(5);
	cfg.current_source_pin_set = 1;
	cfg.current_source_pin[0] = 5;
	zassert_equal(adc_channel_setup(NAFE1, &cfg), -EINVAL, "excitation pin 5");

	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused setup sends nothing");
}

/* Every field of the extension configuration at a value that shows in the registers */
ZTEST(nafe13388_channels, test_channel_setup_ext_hv)
{
	const struct nafe13388_channel_cfg cfg = {
		.hv_aip = 2,
		.hv_ain = 7,
		.gain_code = 5,
		.lv = false,
		.tcc_off = true,
		.cal_pair = 3,
		.thrs_pair = 1,
		.rate = 9,
		.sinc = 2,
		.delay = 18,
		.normal_settling = true,
		.filter_reset = true,
		.chop = true,
		.viex_current = true,
		.viex_negative = true,
		.viex_mag = 9,
		.vexc_en = true,
		.odc = true,
		.viex_chop = true,
		.viex_aip = 1,
		.viex_ain = 2,
	};

	zassert_ok(nafe13388_channel_setup_ext(NAFE1, 9, &cfg));

	zassert_equal(nafe_emul_frame_count(E1), 5);
	ZASSERT_FRAME(E1, 0, 0x80, 0x12); /* CMD_CH9 */
	/* 2 << 12 | 7 << 8 | 5 << 5 | HV_SEL 0x10 | TCC_OFF 1 */
	ZASSERT_FRAME(E1, 1, 0x80, 0x40, 0x27, 0xB1);
	/* CAL 3 << 12 | THRS 1 << 8 | rate 9 << 3 | sinc 2 */
	ZASSERT_FRAME(E1, 2, 0x80, 0x42, 0x31, 0x4A);
	/* delay 18 << 10 | normal settling 0x200 | filter reset 0x100 | chop 0x80 */
	ZASSERT_FRAME(E1, 3, 0x80, 0x44, 0x4B, 0x80);
	/*
	 * VIEX_VI 0x8000 | VIEX_POL 0x4000 | MAG 9 << 10 | VEXC_EN 0x200 | ODC 0x100 |
	 * VIEX_CHOP 0x40 | AIP_EN 1 << 3 | AIN_EN 2
	 */
	ZASSERT_FRAME(E1, 4, 0x80, 0x46, 0xE7, 0x4A);
}

/*
 * Low voltage input: no HV_SEL, LVSIG_IN set and HV_AIP = HV_AIN = 9 + LVSIG_IN (Table 6).
 * The HV input fields of the configuration are ignored.
 */
ZTEST(nafe13388_channels, test_channel_setup_ext_lv)
{
	const struct nafe13388_channel_cfg cfg = {
		.hv_aip = 15,
		.hv_ain = 15,
		.gain_code = 2,
		.lv = true,
		.lv_signal = NAFE13388_LV_VADD,
	};

	zassert_ok(nafe13388_channel_setup_ext(NAFE1, 10, &cfg));

	ZASSERT_FRAME(E1, 0, 0x80, 0x14);             /* CMD_CH10 */
	ZASSERT_FRAME(E1, 1, 0x80, 0x40, 0xCC, 0x46); /* C/C, gain code 2 << 5 | LVSIG 3 << 1 */
}

/* The example of data sheet 7.3.4: LVSIG_IN 3 at gain code 0 and TCC_OFF = 1 is 0xCC07 */
ZTEST(nafe13388_channels, test_lv_data_sheet_example)
{
	const struct nafe13388_channel_cfg cfg = {
		.gain_code = 0,
		.lv = true,
		.lv_signal = NAFE13388_LV_VADD,
		.tcc_off = true,
	};

	zassert_ok(nafe13388_channel_setup_ext(NAFE1, 11, &cfg));
	zassert_equal(D1->ch[11][0], 0xCC07);
	zassert_equal(D1->errors, 0);
}

/* Every LV signal gets its own pair 9..14, so they select different inputs */
ZTEST(nafe13388_channels, test_lv_pair_per_signal)
{
	static const uint16_t expect[6] = {0x9900, 0xAA02, 0xBB04, 0xCC06, 0xDD08, 0xEE0A};

	/* HV_AIP/HV_AIN | gain code 0 << 5 | LVSIG_IN << 1 */
	for (unsigned int sig = 0; sig <= NAFE13388_LV_VHSS; sig++) {
		struct nafe13388_channel_cfg cfg = {
			.lv = true,
			.lv_signal = sig,
		};

		zassert_ok(nafe13388_channel_setup_ext(NAFE1, 12, &cfg));
		zassert_equal(D1->ch[12][0], expect[sig], "signal %u", sig);
	}
	zassert_equal(D1->errors, 0);
}

ZTEST(nafe13388_channels, test_channel_setup_ext_errors)
{
	struct nafe13388_channel_cfg cfg;

	zassert_equal(nafe13388_channel_setup_ext(NAFE1, 9, NULL), -EINVAL);

	memset(&cfg, 0, sizeof(cfg));
	zassert_equal(nafe13388_channel_setup_ext(NAFE1, 16, &cfg), -EINVAL, "channel 16");

#define EXT_BAD(field, value)                                                                      \
	do {                                                                                       \
		memset(&cfg, 0, sizeof(cfg));                                                      \
		cfg.field = (value);                                                               \
		zassert_equal(nafe13388_channel_setup_ext(NAFE1, 9, &cfg), -EINVAL, #field);       \
	} while (0)

	EXT_BAD(gain_code, 8);
	EXT_BAD(lv_signal, NAFE13388_LV_VHSS + 1);
	EXT_BAD(cal_pair, 16);
	EXT_BAD(thrs_pair, 16);
	EXT_BAD(rate, NAFE13388_DATA_RATE_CODE_MAX + 1);
	EXT_BAD(sinc, NAFE13388_SINC4 + 1);
	EXT_BAD(delay, NAFE13388_CHANNEL_DELAY_CODE_MAX + 1);
	EXT_BAD(viex_mag, 16);
	EXT_BAD(viex_aip, 5);
	EXT_BAD(viex_ain, 5);
	EXT_BAD(hv_aip, 9);
	EXT_BAD(hv_ain, 9);

	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused setup sends nothing");
}

/* cfg_rate writes only the registers whose value changes */
ZTEST(nafe13388_channels, test_cfg_rate_writes_only_on_change)
{
	/* Channel 0 is configured with rate 0, delay 0: nothing to do */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 0, 0));
	zassert_equal(nafe_emul_frame_count(E1), 0);

	/* Rate only: CMD_CH0 and CH_CONFIG1 */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 5, 0));
	zassert_equal(nafe_emul_frame_count(E1), 2);
	ZASSERT_FRAME(E1, 0, 0x80, 0x00);
	ZASSERT_FRAME(E1, 1, 0x80, 0x42, 0x00, 0x28); /* 5 << 3 */

	/* The same again: nothing */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 5, 0));
	zassert_equal(nafe_emul_frame_count(E1), 2);

	/* Delay only: CMD_CH0 and CH_CONFIG2 */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 5, 10));
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 2, 0x80, 0x00);
	ZASSERT_FRAME(E1, 3, 0x80, 0x44, 0x28, 0x00); /* 10 << 10 */

	/* Both on channel 1: CMD_CH1, CH_CONFIG1, CH_CONFIG2 */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 1, 2, 1));
	zassert_equal(nafe_emul_frame_count(E1), 7);
	ZASSERT_FRAME(E1, 4, 0x80, 0x02);
	ZASSERT_FRAME(E1, 5, 0x80, 0x42, 0x00, 0x10);
	ZASSERT_FRAME(E1, 6, 0x80, 0x44, 0x04, 0x00);

	zassert_equal(D1->ch[0][1], 0x0028);
	zassert_equal(D1->ch[0][2], 0x2800);
	zassert_equal(D1->ch[1][1], 0x0010);
	zassert_equal(D1->ch[1][2], 0x0400);
	zassert_equal(D1->ch[1][0], 0x2770, "the other fields of the channel are kept");
}

ZTEST(nafe13388_channels, test_cfg_rate_errors)
{
	zassert_equal(nafe13388_cfg_rate(NAFE1, 7, 1, 1), -EINVAL, "channel not set up");
	zassert_equal(nafe13388_cfg_rate(NAFE1, 16, 1, 1), -EINVAL, "channel 16");
	zassert_equal(nafe13388_cfg_rate(NAFE1, 0, NAFE13388_DATA_RATE_CODE_MAX + 1, 0), -EINVAL);
	zassert_equal(nafe13388_cfg_rate(NAFE1, 0, 0, NAFE13388_CHANNEL_DELAY_CODE_MAX + 1),
		      -EINVAL);
	zassert_equal(nafe_emul_frame_count(E1), 0);
}
