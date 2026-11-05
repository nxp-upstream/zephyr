/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Device commands. After RESET and CLEAR_REG the user registers are back at their
 * defaults, so the driver restores what it knows (MCH_EN, SYS_CONFIG0 and the
 * channels of the devicetree) and forgets the channels set up at runtime. After
 * RELOAD it keeps its copies. Command words: CMD_CLEAR_ALARM 0x8024,
 * CMD_CLEAR_DATA 0x8026, CMD_RESET 0x8028, CMD_CLEAR_REG 0x802A, CMD_RELOAD 0x802C.
 */

#include <errno.h>

#include "common.h"

ZTEST_SUITE(nafe13388_commands, NULL, NULL, nafe_before, nafe_after, NULL);

static int32_t two[2];

static void setup_runtime_channel(void)
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
	int32_t buf[1];

	zassert_ok(adc_channel_setup(NAFE1, &cfg));
	zassert_ok(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL));
	zassert_equal(D1->ch[4][0], 0x1770);
	zassert_equal(D1->mch_en, BIT(4));
}

/* The state after the init, from the point of view of the emulator */
static void check_state_after_init(void)
{
	zassert_equal(D1->ch[0][0], 0x1770);
	zassert_equal(D1->ch[0][1], 0x0000);
	zassert_equal(D1->ch[1][0], 0x2770);
	zassert_equal(D1->ch[4][0], 0x0000, "the runtime channel is gone from the device");
	zassert_equal(D1->mch_en, 0);
	zassert_equal(D1->sys_config0, 0x0010);
}

static void check_restore_frames(unsigned int first)
{
	/* MCH_EN = 0, SYS_CONFIG0, channel 0 and channel 1 (CMD_CHn and CH_CONFIG0..3) */
	ZASSERT_FRAME(E1, first + 0, 0x80, 0x48, 0x00, 0x00);
	ZASSERT_FRAME(E1, first + 1, 0x80, 0x60, 0x00, 0x10);
	ZASSERT_FRAME(E1, first + 2, 0x80, 0x00);
	ZASSERT_FRAME(E1, first + 3, 0x80, 0x40, 0x17, 0x70);
	ZASSERT_FRAME(E1, first + 4, 0x80, 0x42, 0x00, 0x00);
	ZASSERT_FRAME(E1, first + 5, 0x80, 0x44, 0x00, 0x00);
	ZASSERT_FRAME(E1, first + 6, 0x80, 0x46, 0x00, 0x00);
	ZASSERT_FRAME(E1, first + 7, 0x80, 0x02);
	ZASSERT_FRAME(E1, first + 8, 0x80, 0x40, 0x27, 0x70);
	ZASSERT_FRAME(E1, first + 9, 0x80, 0x42, 0x00, 0x00);
	ZASSERT_FRAME(E1, first + 10, 0x80, 0x44, 0x00, 0x00);
	ZASSERT_FRAME(E1, first + 11, 0x80, 0x46, 0x00, 0x00);
}

ZTEST(nafe13388_commands, test_clear_alarm)
{
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_CLEAR_ALARM));

	zassert_equal(nafe_emul_frame_count(E1), 1);
	ZASSERT_FRAME(E1, 0, 0x80, 0x24);
}

ZTEST(nafe13388_commands, test_clear_data)
{
	uint32_t val = 1;

	D1->reg24[0x40] = 0x123456;
	setup_runtime_channel();
	nafe_emul_clear_log(E1);

	zassert_ok(nafe13388_command(NAFE1, NAFE13388_CLEAR_DATA));
	zassert_equal(nafe_emul_frame_count(E1), 1, "no restore after CLEAR_DATA");
	ZASSERT_FRAME(E1, 0, 0x80, 0x26);

	zassert_ok(nafe13388_read_reg24(NAFE1, 0x40, &val));
	zassert_equal(val, 0);
	zassert_equal(D1->mch_en, BIT(4), "the registers are untouched");
}

ZTEST(nafe13388_commands, test_reset)
{
	int32_t buf[1];

	setup_runtime_channel();
	nafe_emul_clear_log(E1);
	D1->reset_not_ready = 2;

	zassert_ok(nafe13388_command(NAFE1, NAFE13388_RESET));

	/* CMD_RESET, 3 reads of SYS_STATUS0, then the restore */
	zassert_equal(nafe_emul_frame_count(E1), 16);
	ZASSERT_FRAME(E1, 0, 0x80, 0x28);
	ZASSERT_FRAME(E1, 1, 0xC0, 0x62, 0x00, 0x00);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x62, 0x00, 0x00);
	check_restore_frames(4);
	zassert_equal(D1->status_reads, 3);
	check_state_after_init();

	/* The channels of the devicetree work, the runtime one has to be set up again */
	zassert_ok(nafe_read(BIT(0) | BIT(1), 24, two, sizeof(two), NULL));
	zassert_equal(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL), -EINVAL);
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 0, 0), "shadow of the rate is back to 0");
}

/*
 * CLEAR_REG: the chip sets the gain coefficients to 0x400000 and clears the thresholds
 * so the driver follows it with RELOAD to get the factory coefficients back.
 */
ZTEST(nafe13388_commands, test_clear_reg)
{
	int32_t buf[1];

	setup_runtime_channel();
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 5, 0));
	zassert_ok(nafe13388_write_reg24(NAFE1, 0x50, 0x123456));
	zassert_equal(D1->reg24[0x80], 0x404808U, "factory coefficient after the init");
	nafe_emul_clear_log(E1);

	zassert_ok(nafe13388_command(NAFE1, NAFE13388_CLEAR_REG));

	/* CMD_CLEAR_REG, SYS_STATUS0, CMD_RELOAD, SYS_STATUS0 (ready at once), the restore */
	zassert_equal(nafe_emul_frame_count(E1), 16);
	ZASSERT_FRAME(E1, 0, 0x80, 0x2A);
	ZASSERT_FRAME(E1, 1, 0xC0, 0x62, 0x00, 0x00);
	ZASSERT_FRAME(E1, 2, 0x80, 0x2C);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x62, 0x00, 0x00);
	check_restore_frames(4);
	check_state_after_init();
	zassert_equal(D1->reg24[0x80], 0x404808U, "RELOAD brought the factory gain back");
	zassert_equal(D1->reg24[0x50], 0U, "the thresholds are cleared");

	zassert_equal(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL), -EINVAL);
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
	/* The rate of channel 0 is the one of the devicetree again */
	zassert_equal(D1->ch[0][1], 0x0000);
}

/* RELOAD keeps the user registers, so the driver keeps its copies */
ZTEST(nafe13388_commands, test_reload)
{
	int32_t buf[1];

	setup_runtime_channel();
	nafe_emul_clear_log(E1);
	D1->reset_not_ready = 1;

	zassert_ok(nafe13388_command(NAFE1, NAFE13388_RELOAD));

	/* CMD_RELOAD and the wait for CHIP_READY: 2 reads, nothing is written */
	zassert_equal(nafe_emul_frame_count(E1), 3);
	ZASSERT_FRAME(E1, 0, 0x80, 0x2C);
	ZASSERT_FRAME(E1, 1, 0xC0, 0x62, 0x00, 0x00);
	zassert_equal(D1->status_reads, 2);

	zassert_equal(D1->ch[4][0], 0x1770);
	zassert_ok(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL));
	/* No second MCH_EN write: the copy is still right */
	zassert_equal(nafe_emul_frame_count(E1), 3 + 3);
}

ZTEST(nafe13388_commands, test_reset_chip_not_ready)
{
	int32_t buf[1];
	int64_t t0;
	int dt;

	D1->reset_not_ready = -1;
	nafe_emul_clear_log(E1);

	t0 = k_uptime_get();
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RESET), -ETIMEDOUT);
	dt = (int)(k_uptime_get() - t0);
	/* The wait is a time (review finding F6), the number of polls depends on the tick */
	zassert_true(dt >= CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS &&
			     dt < 4 * CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS,
		     "waited %d ms", dt);
	zassert_true(D1->status_reads >= 2, "polled %u times", D1->status_reads);
	zassert_equal(nafe_emul_frame_count(E1), 1 + D1->status_reads,
		      "no restore after the timeout");

	/* The copies are void: nothing can be read until the next successful reset */
	zassert_equal(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL), -EINVAL);
	D1->reset_not_ready = 0;
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_RESET));
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
}

/*
 * A RESET that fails (the chip does not become ready) leaves the registers of the device at
 * their defaults, so the driver must not keep its copy of SYS_CONFIG0 from before: the next
 * read has to write SYS_CONFIG0 again (DRDY_PIN_EDGE etc.).
 */
ZTEST(nafe13388_commands, test_failed_reset_voids_sys_config0_copy)
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
	int32_t buf[1];

	zassert_not_equal(D1->sys_config0, 0, "init wrote SYS_CONFIG0");

	D1->reset_not_ready = -1;
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RESET), -ETIMEDOUT);
	zassert_equal(D1->sys_config0, 0, "the chip is at its defaults");

	/* The chip becomes ready late, without another reset, with its registers at the defaults */
	D1->reset_not_ready = 0;
	D1->not_ready_reads = 0;
	zassert_ok(adc_channel_setup(NAFE1, &cfg));
	zassert_ok(nafe_read(BIT(4), 24, buf, sizeof(buf), NULL));
	zassert_not_equal(D1->sys_config0, 0, "the read wrote SYS_CONFIG0 again");
}

/*
 * The first SYS_STATUS0 words after a reset can be 0xffff. Such a word has CHIP_READY set
 * but also both conversion active bits, so it is not "ready": the driver reads on. Whether
 * a real chip does this because of a not yet ready chip is an assumption, the emulator
 * only mimics the word.
 */
ZTEST(nafe13388_commands, test_reset_status_all_ones)
{
	int32_t buf[1];

	D1->reset_ones = 3;
	nafe_emul_clear_log(E1);

	zassert_ok(nafe13388_command(NAFE1, NAFE13388_RESET));
	zassert_equal(D1->status_reads, 4, "three words of 0xffff, then the ready word");
	zassert_equal(D1->errors, 0);
	check_state_after_init();
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
}

/* The bus never gets better: the same timeout as for a chip that never gets ready */
ZTEST(nafe13388_commands, test_reset_status_all_ones_forever)
{
	int64_t t0;
	int dt;

	D1->reset_ones = -1;
	nafe_emul_clear_log(E1);

	t0 = k_uptime_get();
	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RESET), -ETIMEDOUT);
	dt = (int)(k_uptime_get() - t0);
	zassert_true(dt >= CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS &&
			     dt < 4 * CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS,
		     "waited %d ms", dt);
	zassert_true(D1->status_reads >= 2, "polled %u times", D1->status_reads);
	zassert_equal(nafe_emul_frame_count(E1), 1 + D1->status_reads);

	D1->reset_ones = 0;
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_RESET));
}

/* A SPI error on the command itself leaves the copies alone */
ZTEST(nafe13388_commands, test_reset_spi_error)
{
	int32_t buf[1];

	D1->fail_countdown = 1;

	zassert_equal(nafe13388_command(NAFE1, NAFE13388_RESET), -EIO);
	zassert_ok(nafe_read(BIT(0), 24, buf, sizeof(buf), NULL));
}

ZTEST(nafe13388_commands, test_unknown_command)
{
	zassert_equal(nafe13388_command(NAFE1, (enum nafe13388_command_id)9), -EINVAL);
	zassert_equal(nafe_emul_frame_count(E1), 0);
}
