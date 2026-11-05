/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * adc_read(): single channel (CMD_SS) and multichannel (CMD_MM) readings, 24 and 16
 * bit, buffer layout, errors, DRDY timeout. Frame bytes: command word = DEV_AD << 15 |
 * RW_L << 14 | address << 1, with the conversion commands 0x2000..0x2005 written as
 * RW_L = 1 and address 0..5 (Table 25).
 */

#include <errno.h>
#include <string.h>

#include "common.h"

ZTEST_SUITE(nafe13388_read, NULL, NULL, nafe_before, nafe_after, NULL);

static int cb_calls;

static enum adc_action layout_cb(const struct device *dev, const struct adc_sequence *sequence,
				 uint16_t sampling_index)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sequence);
	ARG_UNUSED(sampling_index);

	cb_calls++;
	D1->code[0]++;
	D1->code[1]++;

	return ADC_ACTION_CONTINUE;
}

ZTEST(nafe13388_read, test_ss_24bit)
{
	int32_t buf[1] = {0};

	D1->code[0] = -5;

	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], -5);

	/* MCH_EN, CMD_CH0, CMD_SS, CMD_BURST_DATA */
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 0, 0x80, 0x48, 0x00, 0x01);
	ZASSERT_FRAME(E1, 1, 0x80, 0x00);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x00);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x0A, 0x00, 0x00, 0x00);
	zassert_equal(D1->pulses, 1);
	zassert_equal(D1->convs, 1);

	/* A second reading of the same channel does not write MCH_EN again */
	D1->code[0] = 0x123456;
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], 0x123456);
	zassert_equal(nafe_emul_frame_count(E1), 7);
	ZASSERT_FRAME(E1, 4, 0x80, 0x00);

	ZASSERT_IDLE();
}

ZTEST(nafe13388_read, test_ss_16bit)
{
	int32_t buf24[1] = {0};
	int16_t buf[1] = {0};

	/* The upper 16 bit of the 24 bit code arrive on the wire */
	D1->code[1] = 0x123456;

	zassert_ok(nafe_read(BIT(1), 16, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], 0x1234);

	/* SYS_CONFIG0 = ADC_DATA_OUT_16BIT | DRDY_PIN_EDGE, MCH_EN, CMD_CH1, CMD_SS, burst */
	zassert_equal(nafe_emul_frame_count(E1), 5);
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x40, 0x10);
	ZASSERT_FRAME(E1, 1, 0x80, 0x48, 0x00, 0x02);
	ZASSERT_FRAME(E1, 2, 0x80, 0x02);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x00);
	ZASSERT_FRAME(E1, 4, 0xC0, 0x0A, 0x00, 0x00);
	zassert_equal(D1->sys_config0, 0x4010);

	D1->code[1] = -0x1000;
	zassert_ok(nafe_read(BIT(1), 16, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], -16);

	/* Back to 24 bit: the width bit is cleared again */
	zassert_ok(nafe_read(BIT(1), 24, buf24, sizeof(buf24), NULL));
	zassert_equal(D1->sys_config0, 0x0010);
}

ZTEST(nafe13388_read, test_mm_24bit)
{
	int32_t buf[2] = {0};

	D1->code[0] = 0x7FFFFF;
	D1->code[1] = -0x800000;

	zassert_ok(nafe_read(0x3, 24, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], 8388607);
	zassert_equal(buf[1], -8388608);

	/* MCH_EN, CMD_MM, burst of two channels: no CMD_CHn for several channels */
	zassert_equal(nafe_emul_frame_count(E1), 3);
	ZASSERT_FRAME(E1, 0, 0x80, 0x48, 0x00, 0x03);
	ZASSERT_FRAME(E1, 1, 0xC0, 0x04);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	zassert_equal(D1->pulses, 1, "one DRDY per sequence");
	zassert_equal(D1->convs, 2);
}

ZTEST(nafe13388_read, test_mm_16bit)
{
	int16_t buf[2] = {0};

	D1->code[0] = 0x7FFFFF;
	D1->code[1] = -0x800000;

	zassert_ok(nafe_read(0x3, 16, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], 32767);
	zassert_equal(buf[1], -32768);

	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x40, 0x10);
	ZASSERT_FRAME(E1, 1, 0x80, 0x48, 0x00, 0x03);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x04);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x0A, 0x00, 0x00, 0x00, 0x00);
}

/* The buffer holds the channels of a sampling in index order, sampling after sampling */
ZTEST(nafe13388_read, test_extra_samplings_layout)
{
	const struct adc_sequence_options opts = {
		.callback = layout_cb,
		.extra_samplings = 1,
	};
	int32_t buf[4] = {0};

	D1->code[0] = 100;
	D1->code[1] = -100;
	cb_calls = 0;

	zassert_ok(nafe_read(0x3, 24, buf, sizeof(buf), &opts));
	zassert_equal(cb_calls, 2, "the callback is called after every sampling");
	zassert_equal(buf[0], 100);
	zassert_equal(buf[1], -100);
	zassert_equal(buf[2], 101);
	zassert_equal(buf[3], -99);

	/* MCH_EN once, then CMD_MM and a burst for each of the two samplings */
	zassert_equal(nafe_emul_frame_count(E1), 5);
	ZASSERT_FRAME(E1, 0, 0x80, 0x48, 0x00, 0x03);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x04);
	zassert_equal(D1->pulses, 2);
}

/* A channel set up at runtime with adc_channel_setup() can be read */
ZTEST(nafe13388_read, test_runtime_channel)
{
	const struct adc_channel_cfg cfg = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 4,
		.differential = 1,
		.input_positive = 1,
		.input_negative = 7,
	};
	int32_t buf[1] = {0};

	zassert_equal(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL), -EINVAL, "not set up yet");
	zassert_ok(adc_channel_setup(NAFE1, &cfg));
	zassert_ok(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], 0x50000);
}

ZTEST(nafe13388_read, test_errors)
{
	struct nafe13388_channel_cfg chop = {.gain_code = 3, .viex_chop = true};
	const struct adc_sequence_options opts = {.extra_samplings = 1};
	int32_t buf[4];
	struct adc_sequence seq = {
		.channels = BIT(0),
		.buffer = buf,
		.buffer_size = sizeof(buf),
		.resolution = 24,
	};

	zassert_equal(nafe_read(0, 24, buf, sizeof(buf), NULL), -EINVAL, "no channel");
	zassert_equal(nafe_read(BIT(2), 24, buf, sizeof(buf), NULL), -EINVAL, "not set up");
	zassert_equal(nafe_read(BIT(16), 24, buf, sizeof(buf), NULL), -EINVAL, "channel 16");
	zassert_equal(nafe_read(BIT(0), 12, buf, sizeof(buf), NULL), -EINVAL, "12 bit");

	seq.oversampling = 1;
	zassert_equal(adc_read(NAFE1, &seq), -ENOTSUP, "oversampling");
	seq.oversampling = 0;
	seq.calibrate = true;
	zassert_equal(adc_read(NAFE1, &seq), -ENOTSUP, "calibration");

	zassert_equal(nafe_read(0x3, 24, buf, 4, NULL), -ENOMEM, "2 channels need 8 bytes");
	zassert_equal(nafe_read(0x3, 16, buf, 3, NULL), -ENOMEM, "2 channels 16 bit need 4 bytes");
	zassert_equal(nafe_read(BIT(0), 24, buf, 4, &opts), -ENOMEM, "2 samplings need 8 bytes");
	zassert_equal(nafe_read(BIT(0), 24, NULL, 4, NULL), -ENOMEM, "no buffer");

	/* Different excitation chopping in one sequence */
	zassert_ok(nafe13388_channel_setup_ext(NAFE1, 4, &chop));
	nafe_emul_clear_log(E1);
	zassert_equal(nafe_read(BIT(0) | BIT(4), 24, buf, sizeof(buf), NULL), -EINVAL, "VIEX_CHOP");

	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused reading sends nothing");
	ZASSERT_IDLE();
}

/* No DRDY: the driver aborts the conversion and reports -ETIMEDOUT, the next reading works */
ZTEST(nafe13388_read, test_drdy_timeout)
{
	int32_t buf[1] = {0};
	int64_t t0;
	int dt;

	D1->auto_drdy = false;
	t0 = k_uptime_get();
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -ETIMEDOUT);
	dt = (int)(k_uptime_get() - t0);
	zassert_true(dt >= CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS &&
			     dt < 4 * CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS,
		     "waited %d ms", dt);

	/* MCH_EN, CMD_CH0, CMD_SS, then CMD_ABORT and no burst */
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x00);
	ZASSERT_FRAME(E1, 3, 0x80, 0x20);
	zassert_equal(D1->aborts, 1);
	zassert_equal(D1->pulses, 0);
	ZASSERT_IDLE();

	D1->auto_drdy = true;
	D1->code[0] = 42;
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
	zassert_equal(buf[0], 42);
	zassert_equal(D1->aborts, 1);
}

#ifdef CONFIG_ADC_ASYNC
static int wait_signal(struct k_poll_signal *sig)
{
	struct k_poll_event ev =
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, sig);
	unsigned int signaled = 0;
	int result = -1;

	zassert_ok(k_poll(&ev, 1, K_MSEC(500)));
	k_poll_signal_check(sig, &signaled, &result);
	zassert_true(signaled != 0U);

	return result;
}

ZTEST(nafe13388_read, test_read_async)
{
	struct k_poll_signal sig;
	int32_t buf[2] = {0};
	const struct adc_sequence seq = {
		.channels = 0x3,
		.buffer = buf,
		.buffer_size = sizeof(buf),
		.resolution = 24,
	};

	k_poll_signal_init(&sig);
	D1->code[0] = 11;
	D1->code[1] = -22;

	zassert_ok(adc_read_async(NAFE1, &seq, &sig));
	zassert_equal(wait_signal(&sig), 0);
	zassert_equal(buf[0], 11);
	zassert_equal(buf[1], -22);
	ZASSERT_IDLE();
}

ZTEST(nafe13388_read, test_read_async_timeout)
{
	struct k_poll_signal sig;
	int32_t buf[1] = {0};
	const struct adc_sequence seq = {
		.channels = BIT(0),
		.buffer = buf,
		.buffer_size = sizeof(buf),
		.resolution = 24,
	};

	k_poll_signal_init(&sig);
	D1->auto_drdy = false;

	zassert_ok(adc_read_async(NAFE1, &seq, &sig));
	zassert_equal(wait_signal(&sig), -ETIMEDOUT);
	zassert_equal(D1->aborts, 1);
	ZASSERT_IDLE();
}
#endif /* CONFIG_ADC_ASYNC */

/*
 * Table 7, data rate code 28 with single-cycle settling is 1.5 sps to 3.75 sps, so one
 * conversion can take 667 ms, far more than CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS. The
 * wait for DRDY has to follow the data rate of the channel, not only the Kconfig value.
 */
ZTEST(nafe13388_read, test_drdy_timeout_follows_data_rate)
{
	int32_t buf[1] = {0};
	int64_t t0;
	int dt;

	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, NAFE13388_DATA_RATE_CODE_MAX, 0));
	D1->auto_drdy = false;
	nafe_emul_clear_log(E1);

	t0 = k_uptime_get();
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -ETIMEDOUT);
	dt = (int)(k_uptime_get() - t0);

	/* 5 * 133.334 ms + the Kconfig margin, rounded up to ticks */
	zassert_true(dt >= 5 * 133 && dt < 2000, "waited %d ms", dt);
	zassert_equal(D1->aborts, 1);
	ZASSERT_IDLE();
}

/*
 * The SPI_ADDR pin has an internal 100 kohm pulldown (data sheet pin description), so
 * the default of nxp,dev-addr is 0. nafe5 does not set the property; nafe1 is wired as
 * address 1 and sets it.
 */
BUILD_ASSERT(DT_PROP(DT_NODELABEL(nafe5), nxp_dev_addr) == 0, "the default of nxp,dev-addr is 0");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(nafe1), nxp_dev_addr) == 1, "nafe1 is set to address 1");

static enum adc_action sleep_cb(const struct device *dev, const struct adc_sequence *sequence,
				uint16_t sampling_index)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sequence);
	ARG_UNUSED(sampling_index);

	/* Long enough for several periods of a timer that is still running */
	k_msleep(100);

	return ADC_ACTION_CONTINUE;
}

/*
 * A read with interval_us that fails (no DRDY) must stop the sampling timer of the ADC
 * context. adc_context_on_sampling_done() stops it only when the sequence finishes
 * well. A timer that keeps running writes -EBUSY into the status of the next read
 * (adc_context_request_next_sampling() with a sampling that is not finished). The
 * interval is a multiple of the 10 ms tick of native_sim.
 */
ZTEST(nafe13388_read, test_failed_read_stops_interval_timer)
{
	const struct adc_sequence_options timed = {.interval_us = 20000, .extra_samplings = 1};
	const struct adc_sequence_options slow = {.callback = sleep_cb};
	int32_t buf[2] = {0};

	D1->auto_drdy = false;
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), &timed), -ETIMEDOUT);
	ZASSERT_IDLE();

	D1->auto_drdy = true;
	D1->code[0] = 7;
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), &slow), 0,
		      "a stale timer disturbed the read");
	zassert_equal(buf[0], 7);
}
