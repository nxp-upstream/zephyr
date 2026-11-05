/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Who owns the device: the standard ADC API, a reading of the extension API, or a
 * continuous reading. The other owners get -EBUSY. For the ADC direction the calls
 * are made from the sequence callback, which runs in the acquisition thread while
 * the standard read is in progress; that is deterministic, unlike racing a second
 * thread against a read that holds the device lock while it waits for DRDY.
 */

#include <errno.h>

#include "common.h"

ZTEST_SUITE(nafe13388_owner, NULL, NULL, nafe_before, nafe_after, NULL);

static int count_cb(const struct device *dev, const int32_t *raw, unsigned int n, int status,
		    void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(raw);
	ARG_UNUSED(n);
	ARG_UNUSED(status);

	(*(int *)user_data)++;

	return 0;
}

static struct {
	int start;
	int cont_start;
	int stop;
	int cont_stop;
	int trigger;
	int arm;
	int wait;
	int burst;
	int data;
	int command;
	int alarm;
	int cfg_rate;
	int setup_ext;
	int setup_std;
	int calls;
} adc_rets;

/* A valid standard channel configuration of a channel that has no devicetree node */
static const struct adc_channel_cfg std_cfg = {
	.gain = ADC_GAIN_1,
	.reference = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQ_TIME_DEFAULT,
	.channel_id = 4,
	.differential = 1,
	.input_positive = 1,
	.input_negative = 7,
};

static enum adc_action owner_cb(const struct device *dev, const struct adc_sequence *sequence,
				uint16_t sampling_index)
{
	const struct nafe13388_channel_cfg ext = {.gain_code = 3};
	const struct nafe13388_seq ss = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	const struct nafe13388_seq sc = nafe_seq(BIT(0), NAFE13388_MODE_SC, 24);
	int32_t raw[2];
	int unused = 0;

	ARG_UNUSED(sequence);
	ARG_UNUSED(sampling_index);

	adc_rets.calls++;
	adc_rets.start = nafe13388_start(dev, &ss);
	adc_rets.cont_start = nafe13388_continuous_start(dev, &sc, count_cb, &unused);
	adc_rets.stop = nafe13388_stop(dev, NAFE13388_STOP_ABORT);
	adc_rets.cont_stop = nafe13388_continuous_stop(dev, NAFE13388_STOP_ABORT);
	adc_rets.trigger = nafe13388_trigger(dev);
	adc_rets.arm = nafe13388_drdy_arm(dev);
	adc_rets.wait = nafe13388_drdy_wait(dev, K_NO_WAIT);
	adc_rets.burst = nafe13388_read_burst(dev, raw, 1);
	adc_rets.data = nafe13388_read_data(dev, 0, raw);
	adc_rets.command = nafe13388_command(dev, NAFE13388_RESET);
	adc_rets.alarm = nafe13388_command(dev, NAFE13388_CLEAR_ALARM);
	adc_rets.cfg_rate = nafe13388_cfg_rate(dev, 0, 4, 0);
	adc_rets.setup_ext = nafe13388_channel_setup_ext(dev, 4, &ext);
	adc_rets.setup_std = adc_channel_setup(dev, &std_cfg);

	return ADC_ACTION_CONTINUE;
}

/* While adc_read() runs, the extension API is refused, except for what is allowed */
ZTEST(nafe13388_owner, test_adc_api_blocks_extension)
{
	const struct adc_sequence_options opts = {.callback = owner_cb};
	int32_t buf[1] = {0};

	memset(&adc_rets, 0, sizeof(adc_rets));

	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), &opts));

	zassert_equal(adc_rets.calls, 1);
	zassert_equal(adc_rets.start, -EBUSY);
	zassert_equal(adc_rets.cont_start, -EBUSY);
	zassert_equal(adc_rets.stop, -EBUSY);
	zassert_equal(adc_rets.cont_stop, -EBUSY);
	zassert_equal(adc_rets.trigger, -EBUSY);
	zassert_equal(adc_rets.arm, -EBUSY);
	zassert_equal(adc_rets.wait, -EBUSY);
	zassert_equal(adc_rets.burst, -EBUSY);
	zassert_equal(adc_rets.data, -EBUSY);
	zassert_equal(adc_rets.command, -EBUSY);
	zassert_equal(adc_rets.alarm, 0, "CLEAR_ALARM is allowed");
	zassert_equal(adc_rets.cfg_rate, -EBUSY, "cfg_rate is refused");
	zassert_equal(adc_rets.setup_ext, -EBUSY, "channel_setup_ext is refused");
	zassert_equal(adc_rets.setup_std, -EBUSY, "adc_channel_setup() is refused too");
	ZASSERT_IDLE();

	/* The read itself was not disturbed: MCH_EN, CMD_CH0, CMD_SS, burst, plus the two */
	zassert_equal(D1->resets, 0);
	zassert_equal(D1->aborts, 0);
	zassert_equal(D1->ch[0][1], 0x0000, "cfg_rate wrote nothing");
	zassert_equal(D1->ch[4][0], 0x0000, "the refused setups wrote nothing");

	/* The device is free again, and the refused setup works now */
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_CLEAR_DATA));
	zassert_ok(adc_channel_setup(NAFE1, &std_cfg));
	zassert_not_equal(D1->ch[4][0], 0x0000);
}

ZTEST(nafe13388_owner, test_extension_blocks_adc_api)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	const struct nafe13388_channel_cfg ext = {.gain_code = 3};
	int32_t buf[1] = {0};
	uint16_t val;
	uint32_t val24;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	nafe_emul_clear_log(E1);

	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -EBUSY);
	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused read sends nothing");

	zassert_equal(nafe13388_start(NAFE1, &seq), -EBUSY);
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RESET), -EBUSY);
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_CLEAR_REG), -EBUSY);
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RELOAD), -EBUSY);
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_CLEAR_DATA), -EBUSY);
	/* A channel change is refused while a reading owns the device */
	zassert_equal(nafe13388_cfg_rate(NAFE1, 0, 1, 1), -EBUSY);
	zassert_equal(nafe13388_channel_setup_ext(NAFE1, 4, &ext), -EBUSY);
	zassert_equal(adc_channel_setup(NAFE1, &std_cfg), -EBUSY);
	zassert_equal(nafe_emul_frame_count(E1), 0);
	zassert_equal(D1->ch[0][1], 0x0000);
	zassert_equal(D1->ch[4][0], 0x0000);

	/* Allowed while a reading is active */
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_CLEAR_ALARM));
	zassert_ok(nafe13388_read_reg16(NAFE1, 0x7C, &val));
	zassert_ok(nafe13388_write_reg24(NAFE1, 0x80, 1));
	zassert_ok(nafe13388_read_reg24(NAFE1, 0x80, &val24));

	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE));
	zassert_ok(adc_channel_setup(NAFE1, &std_cfg));
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
}

ZTEST(nafe13388_owner, test_continuous_blocks_all)
{
	struct nafe13388_seq mc = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct nafe13388_seq ss = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	const struct nafe13388_channel_cfg cont_ext = {.gain_code = 3};
	int32_t buf[2] = {0};
	int32_t raw[2];
	int calls = 0;
	uint16_t val;

	zassert_ok(nafe13388_continuous_start(NAFE1, &mc, count_cb, &calls));
	NAFE_SETTLE();
	nafe_emul_clear_log(E1);

	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -EBUSY);
	zassert_equal(nafe13388_start(NAFE1, &ss), -EBUSY);
	zassert_equal(nafe13388_continuous_start(NAFE1, &mc, count_cb, &calls), -EBUSY);
	zassert_equal(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT), -EBUSY,
		      "stop() is not for continuous readings");
	zassert_equal(nafe13388_trigger(NAFE1), -EBUSY);
	zassert_equal(nafe13388_drdy_arm(NAFE1), -EBUSY);
	zassert_equal(nafe13388_drdy_wait(NAFE1, K_NO_WAIT), -EBUSY);
	zassert_equal(nafe13388_read_burst(NAFE1, raw, 2), -EBUSY);
	zassert_equal(nafe13388_read_data(NAFE1, 0, raw), -EBUSY);
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RESET), -EBUSY);
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RELOAD), -EBUSY);
	/* A channel change is refused, also during a continuous reading */
	zassert_equal(nafe13388_cfg_rate(NAFE1, 0, 2, 0), -EBUSY);
	zassert_equal(nafe13388_channel_setup_ext(NAFE1, 4, &cont_ext), -EBUSY);
	zassert_equal(adc_channel_setup(NAFE1, &std_cfg), -EBUSY);
	zassert_equal(nafe_emul_frame_count(E1), 0, "refused calls send nothing");

	/* What the header lists as allowed */
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_CLEAR_ALARM));
	zassert_ok(nafe13388_read_reg16(NAFE1, 0x7C, &val));
	zassert_equal(val, 0x1338);
	zassert_equal(nafe13388_sync_pulse(NAFE1), -EINVAL, "reading without sync_pin");

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	ZASSERT_IDLE();
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
	zassert_equal(calls, 0);
}

/* A reading that failed in start() or in the driver thread gives the device back */
ZTEST(nafe13388_owner, test_owner_released_after_errors)
{
	struct nafe13388_seq ss = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	int32_t buf[1] = {0};

	D1->auto_drdy = false;
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -ETIMEDOUT);
	ZASSERT_IDLE();
	zassert_ok(nafe13388_start(NAFE1, &ss));
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT));
	ZASSERT_IDLE();

	D1->fail_countdown = 1; /* MCH_EN */
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -EIO);
	ZASSERT_IDLE();
	D1->auto_drdy = true;
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
}
