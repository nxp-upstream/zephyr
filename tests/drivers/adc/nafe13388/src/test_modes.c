/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Extension API readings without a callback: start, stop, trigger, drdy_arm,
 * drdy_wait, read_burst, read_data, sync_pulse. Frame bytes: command word =
 * DEV_AD << 15 | RW_L << 14 | address << 1; CMD_SS..CMD_MS = 0xC000, 0xC002, 0xC004,
 * 0xC006, 0xC008, CMD_BURST_DATA = 0xC00A, CMD_ABORT = 0x8020, CMD_END = 0x8022.
 */

#include <errno.h>

#include "common.h"

ZTEST_SUITE(nafe13388_modes, NULL, NULL, nafe_before, nafe_after, NULL);

ZTEST(nafe13388_modes, test_ss_burst_and_data)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	int32_t raw[1] = {0};

	D1->code[0] = -1234;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	/* SYS_CONFIG0 is unchanged, so: MCH_EN, CMD_CH0, CMD_SS */
	zassert_equal(nafe_emul_frame_count(E1), 3);
	ZASSERT_FRAME(E1, 0, 0x80, 0x48, 0x00, 0x01);
	ZASSERT_FRAME(E1, 1, 0x80, 0x00);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x00);

	zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT));
	zassert_ok(nafe13388_read_burst(NAFE1, raw, 1));
	zassert_equal(raw[0], -1234);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x0A, 0x00, 0x00, 0x00);

	raw[0] = 0;
	zassert_ok(nafe13388_read_data(NAFE1, 0, raw));
	zassert_equal(raw[0], -1234, "sign extended from 24 bit");
	ZASSERT_FRAME(E1, 4, 0xC0, 0x80, 0x00, 0x00, 0x00);

	/* The device stays taken until stop(), also after a reading that ended itself */
	zassert_equal(nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE), 0);
	zassert_equal(nafe_emul_frame_count(E1), 5, "IDLE sends nothing");
	ZASSERT_IDLE();
}

ZTEST(nafe13388_modes, test_stop_abort_and_end)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);

	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT));
	ZASSERT_FRAME(E1, 3, 0x80, 0x20);
	zassert_equal(D1->aborts, 1);
	ZASSERT_IDLE();

	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_END));
	ZASSERT_FRAME(E1, 6, 0x80, 0x22);
	zassert_equal(D1->ends, 1);
	zassert_equal(nafe13388_stop(NAFE1, NAFE13388_STOP_END), -EALREADY);
}

/* stop(END) of an SC reading waits for the active bit, stop(ABORT) does not */
ZTEST(nafe13388_modes, test_stop_end_waits_for_idle)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SC, 24);

	D1->end_busy_reads = 2;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	D1->status_reads = 0;
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_END));
	zassert_equal(D1->status_reads, 3, "2 reads with the bit set, then one with it clear");
	ZASSERT_IDLE();

	zassert_ok(nafe13388_start(NAFE1, &seq));
	D1->status_reads = 0;
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT));
	zassert_equal(D1->status_reads, 0);

	D1->end_busy_reads = -1;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(nafe13388_stop(NAFE1, NAFE13388_STOP_END), -ETIMEDOUT);
	zassert_equal(D1->aborts, 2, "the stuck device is aborted");
	ZASSERT_IDLE();
}

/* MM with one DRDY per channel: SYS_CONFIG0.DRDY_PIN_EDGE is cleared */
ZTEST(nafe13388_modes, test_mm_drdy_per_channel)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MM, 24);
	int32_t raw[2] = {0};

	seq.drdy_per_channel = true;
	D1->code[0] = 7;
	D1->code[1] = -8;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	/* SYS_CONFIG0 = 0, MCH_EN, CMD_MM and no CMD_CHn */
	zassert_equal(nafe_emul_frame_count(E1), 3);
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x00, 0x00);
	ZASSERT_FRAME(E1, 1, 0x80, 0x48, 0x00, 0x03);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x04);
	zassert_equal(D1->pulses, 2, "one pulse per channel");

	zassert_ok(nafe13388_read_burst(NAFE1, raw, 2));
	zassert_equal(raw[0], 7);
	zassert_equal(raw[1], -8);
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE));

	/* The default (one DRDY per sequence) sets the bit again */
	seq.drdy_per_channel = false;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	/* 3 frames of the first start, the burst, then SYS_CONFIG0 (IDLE sends nothing) */
	ZASSERT_FRAME(E1, 4, 0x80, 0x60, 0x00, 0x10);
	zassert_equal(D1->sys_config0, 0x0010);
}

ZTEST(nafe13388_modes, test_mm_16bit_sign_extended)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MM, 16);
	int32_t raw[2] = {0};

	D1->code[0] = 0x7FFFFF;
	D1->code[1] = -0x800000;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_ok(nafe13388_read_burst(NAFE1, raw, 2));
	zassert_equal(raw[0], 32767, "sign extended to 32 bit");
	zassert_equal(raw[1], -32768);
	/* SYS_CONFIG0, MCH_EN, CMD_MM, then the burst */
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x0A, 0x00, 0x00, 0x00, 0x00);

	/* 4 byte frame (16 data bits): the upper 16 bit of the code, sign extended */
	zassert_ok(nafe13388_read_data(NAFE1, 0, &raw[0]));
	zassert_ok(nafe13388_read_data(NAFE1, 1, &raw[1]));
	zassert_equal(raw[0], 32767);
	zassert_equal(raw[1], -32768);
	ZASSERT_FRAME(E1, 4, 0xC0, 0x80, 0x00, 0x00);
	ZASSERT_FRAME(E1, 5, 0xC0, 0x82, 0x00, 0x00);
}

/* MS: every trigger converts the next channel */
ZTEST(nafe13388_modes, test_ms_trigger)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MS, 24);
	int32_t raw = 0;

	D1->code[0] = 111;
	D1->code[1] = 222;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	/* SYS_CONFIG0 (DRDY_PIN_EDGE = 0), MCH_EN, CMD_CH0 (lowest channel), CMD_MS */
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x00, 0x00);
	ZASSERT_FRAME(E1, 2, 0x80, 0x00);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x08);

	zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT));
	zassert_ok(nafe13388_read_data(NAFE1, 0, &raw));
	zassert_equal(raw, 111);

	/* The old DRDY is gone, the trigger sends CMD_MS again and a new DRDY comes */
	zassert_equal(nafe13388_drdy_wait(NAFE1, K_NO_WAIT), -ETIMEDOUT);
	zassert_ok(nafe13388_trigger(NAFE1));
	ZASSERT_FRAME(E1, 5, 0xC0, 0x08);
	zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT));
	zassert_ok(nafe13388_read_data(NAFE1, 1, &raw));
	zassert_equal(raw, 222);
	zassert_equal(D1->conv_cmds, 2);
}

/*
 * MS with three channels: one DRDY per trigger, also for the channels before the last one.
 * With DRDY_PIN_EDGE = 1 the device only signals after the last enabled channel, so the
 * driver clears the bit for MS.
 */
ZTEST(nafe13388_modes, test_ms_drdy_every_trigger)
{
	struct nafe13388_seq seq = nafe_seq(0x7, NAFE13388_MODE_MS, 24);
	const struct adc_channel_cfg ch2 = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 2,
		.differential = 1,
		.input_positive = 3,
		.input_negative = 7,
	};
	int32_t raw = 0;

	/* Channels 0 and 1 come from the devicetree */
	zassert_ok(adc_channel_setup(NAFE1, &ch2));
	D1->code[0] = 10;
	D1->code[1] = 20;
	D1->code[2] = 30;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(D1->sys_config0 & 0x0010U, 0U, "DRDY_PIN_EDGE is 0 in MS");

	for (unsigned int k = 0; k < 4U; k++) {
		if (k > 0U) {
			zassert_ok(nafe13388_trigger(NAFE1));
		}
		zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT), "DRDY after trigger %u", k);
		zassert_ok(nafe13388_read_data(NAFE1, k % 3U, &raw));
		zassert_equal(raw, 10 * (int32_t)(k % 3U + 1U));
	}
	zassert_equal(D1->conv_cmds, 4);

	/* The flag has no effect in MS */
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT));
	seq.drdy_per_channel = false;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(D1->sys_config0 & 0x0010U, 0U);
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT));

	/* Other modes keep the default of one DRDY per sequence */
	seq.mode = NAFE13388_MODE_MM;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(D1->sys_config0 & 0x0010U, 0x0010U);
}

/* trigger() forgets an old DRDY; drdy_arm() does it on request */
ZTEST(nafe13388_modes, test_trigger_and_arm)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);

	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_ok(nafe13388_drdy_arm(NAFE1));
	zassert_equal(nafe13388_drdy_wait(NAFE1, K_NO_WAIT), -ETIMEDOUT, "armed: old DRDY gone");

	zassert_ok(nafe13388_trigger(NAFE1));
	ZASSERT_FRAME(E1, 3, 0xC0, 0x00); /* CMD_SS again */
	zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT));
	zassert_equal(D1->conv_cmds, 2);
}

/* No DRDY: drdy_wait times out and does not abort, the caller stops */
ZTEST(nafe13388_modes, test_drdy_wait_timeout)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);

	D1->auto_drdy = false;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(nafe13388_drdy_wait(NAFE1, K_MSEC(5)), -ETIMEDOUT);
	zassert_equal(D1->aborts, 0);
	zassert_equal(nafe_emul_frame_count(E1), 3, "nothing sent on a timeout");

	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT));
	zassert_equal(D1->aborts, 1);
}

/* SYNC pin: the command only selects the mode, the pulse starts the conversion */
ZTEST(nafe13388_modes, test_sync_pin)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	int32_t raw = 0;

	seq.sync_pin = true;
	D1->code[0] = 55;

	zassert_ok(nafe13388_start(NAFE1, &seq));
	/* SYS_CONFIG0 = ADC_SYNC | DRDY_PIN_EDGE */
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x00, 0x30);
	zassert_equal(D1->convs, 0, "armed, not started");
	zassert_equal(nafe13388_drdy_wait(NAFE1, K_MSEC(2)), -ETIMEDOUT);

	zassert_ok(nafe13388_sync_pulse(NAFE1));
	zassert_equal(D1->sync_edges, 1);
	zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT));
	zassert_ok(nafe13388_read_data(NAFE1, 0, &raw));
	zassert_equal(raw, 55);

	/* trigger() pulses the pin instead of sending the command */
	D1->code[0] = 66;
	zassert_ok(nafe13388_trigger(NAFE1));
	zassert_equal(D1->sync_edges, 2);
	zassert_equal(nafe_emul_frame_count(E1), 5, "no command frame, only the burst/data read");
	zassert_ok(nafe13388_drdy_wait(NAFE1, K_NO_WAIT));
	zassert_ok(nafe13388_read_data(NAFE1, 0, &raw));
	zassert_equal(raw, 66);
	zassert_equal(D1->conv_cmds, 1);
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE));

	/* A reading without sync_pin clears ADC_SYNC again */
	seq.sync_pin = false;
	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(D1->sys_config0, 0x0010);
	zassert_equal(nafe13388_sync_pulse(NAFE1), -EINVAL, "reading without sync_pin");
}

ZTEST(nafe13388_modes, test_sync_without_pin)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);

	seq.sync_pin = true;
	zassert_equal(nafe13388_start(NAFE5, &seq), -ENOTSUP);
	zassert_equal(nafe13388_sync_pulse(NAFE5), -ENOTSUP);
	zassert_equal(nafe13388_sync_pulse(NAFE1), -EINVAL, "no reading started");
	zassert_equal(nafe_emul_frame_count(E1), 0);
}

ZTEST(nafe13388_modes, test_start_errors)
{
	struct nafe13388_seq seq;
	int32_t raw[2];

	zassert_equal(nafe13388_start(NAFE1, NULL), -EINVAL);

	seq = nafe_seq(0, NAFE13388_MODE_MM, 24);
	zassert_equal(nafe13388_start(NAFE1, &seq), -EINVAL, "no channel");
	seq = nafe_seq(BIT(0), NAFE13388_MODE_MM, 12);
	zassert_equal(nafe13388_start(NAFE1, &seq), -EINVAL, "12 bit");
	seq = nafe_seq(0x3, NAFE13388_MODE_SS, 24);
	zassert_equal(nafe13388_start(NAFE1, &seq), -EINVAL, "SS needs one channel");
	seq = nafe_seq(0x3, NAFE13388_MODE_SC, 24);
	zassert_equal(nafe13388_start(NAFE1, &seq), -EINVAL, "SC needs one channel");
	seq = nafe_seq(BIT(0), (enum nafe13388_mode)9, 24);
	zassert_equal(nafe13388_start(NAFE1, &seq), -EINVAL, "unknown mode");
	seq = nafe_seq(BIT(2), NAFE13388_MODE_SS, 24);
	zassert_equal(nafe13388_start(NAFE1, &seq), -EINVAL, "channel 2 is not set up");
	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused start sends nothing");
	ZASSERT_IDLE();

	/* Nothing is started: all the functions of a started reading refuse */
	zassert_equal(nafe13388_stop(NAFE1, NAFE13388_STOP_ABORT), -EALREADY);
	zassert_equal(nafe13388_trigger(NAFE1), -EINVAL);
	zassert_equal(nafe13388_drdy_arm(NAFE1), -EBUSY);
	zassert_equal(nafe13388_drdy_wait(NAFE1, K_NO_WAIT), -EBUSY);
	zassert_equal(nafe13388_read_burst(NAFE1, raw, 1), -EBUSY);
	zassert_equal(nafe13388_read_data(NAFE1, 0, raw), -EBUSY);
	zassert_equal(nafe_emul_frame_count(E1), 0);

	/* A started reading */
	seq = nafe_seq(0x3, NAFE13388_MODE_MM, 24);
	zassert_ok(nafe13388_start(NAFE1, &seq));
	zassert_equal(nafe13388_start(NAFE1, &seq), -EBUSY, "already started");
	zassert_equal(nafe13388_stop(NAFE1, (enum nafe13388_stop)9), -EINVAL);
	zassert_equal(nafe13388_read_burst(NAFE1, NULL, 2), -EINVAL);
	zassert_equal(nafe13388_read_burst(NAFE1, raw, 1), -EINVAL, "n must match exactly");
	zassert_equal(nafe13388_read_burst(NAFE1, raw, 3), -EINVAL, "n must match exactly");
	zassert_equal(nafe13388_read_data(NAFE1, 0, NULL), -EINVAL);
	zassert_equal(nafe13388_read_data(NAFE1, 16, raw), -EINVAL);
	zassert_ok(nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE));
}

/* A SPI error in start(): the device is not left taken */
ZTEST(nafe13388_modes, test_start_spi_error)
{
	struct nafe13388_seq seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);

	D1->fail_countdown = 2; /* MCH_EN works, CMD_CH0 fails */
	zassert_equal(nafe13388_start(NAFE1, &seq), -EIO);
	ZASSERT_IDLE();

	zassert_ok(nafe13388_start(NAFE1, &seq));
}
