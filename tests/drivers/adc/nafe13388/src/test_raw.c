/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Raw register access: address checks, frames, shadowed registers */

#include <errno.h>

#include "common.h"

ZTEST_SUITE(nafe13388_raw, NULL, NULL, nafe_before, nafe_after, NULL);

ZTEST(nafe13388_raw, test_read16)
{
	uint16_t val = 0;

	zassert_ok(nafe13388_read_reg16(NAFE1, 0x7C, &val));
	zassert_equal(val, 0x1338, "PN2");
	zassert_equal(nafe_emul_frame_count(E1), 1);
	ZASSERT_FRAME(E1, 0, 0xC0, 0xF8, 0x00, 0x00);

	zassert_ok(nafe13388_read_reg16(NAFE1, 0x7D, &val));
	zassert_equal(val, 0x8B40, "PN1");

	zassert_ok(nafe13388_read_reg16(NAFE1, 0x31, &val));
	zassert_true((val & BIT(13)) != 0U, "CHIP_READY");

	/* 0x20..0x23 are the registers of the channel that was selected last */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 1, 2, 1));
	zassert_ok(nafe13388_read_reg16(NAFE1, 0x20, &val));
	zassert_equal(val, 0x2770, "CH_CONFIG0 of channel 1");
	zassert_ok(nafe13388_read_reg16(NAFE1, 0x24, &val));
	zassert_equal(val, 0x0000, "MCH_EN");
}

ZTEST(nafe13388_raw, test_read16_range)
{
	static const uint8_t bad[] = {0x00, 0x1F, 0x28, 0x38, 0x7B, 0x80, 0xFF};
	uint16_t val;

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(nafe13388_read_reg16(NAFE1, bad[i], &val), -EINVAL, "0x%02x", bad[i]);
	}
	zassert_equal(nafe13388_read_reg16(NAFE1, 0x7C, NULL), -EINVAL);
	zassert_equal(nafe_emul_frame_count(E1), 0);
}

ZTEST(nafe13388_raw, test_write16)
{
	uint16_t val = 0;

	zassert_ok(nafe13388_write_reg16(NAFE1, 0x32, 0x1234)); /* GLOBAL_ALARM_ENABLE */

	zassert_equal(nafe_emul_frame_count(E1), 1);
	ZASSERT_FRAME(E1, 0, 0x80, 0x64, 0x12, 0x34);
	zassert_equal(D1->reg16[0x32], 0x1234);

	zassert_ok(nafe13388_read_reg16(NAFE1, 0x32, &val));
	zassert_equal(val, 0x1234);
}

/* The registers the driver keeps a copy of cannot be written */
ZTEST(nafe13388_raw, test_write16_shadowed)
{
	static const uint8_t shadowed[] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x30};

	for (size_t i = 0; i < ARRAY_SIZE(shadowed); i++) {
		zassert_equal(nafe13388_write_reg16(NAFE1, shadowed[i], 0x1234), -EINVAL, "0x%02x",
			      shadowed[i]);
	}
	zassert_equal(nafe13388_write_reg16(NAFE1, 0x1F, 0), -EINVAL);
	zassert_equal(nafe13388_write_reg16(NAFE1, 0x28, 0), -EINVAL, "no register at 0x28");
	zassert_equal(nafe13388_write_reg16(NAFE1, 0x38, 0), -EINVAL);
	zassert_equal(nafe13388_write_reg16(NAFE1, 0x7B, 0), -EINVAL);
	zassert_equal(nafe_emul_frame_count(E1), 0);
}

ZTEST(nafe13388_raw, test_read24)
{
	uint32_t val = 0;

	D1->reg24[0x40] = 0x123456U;
	zassert_ok(nafe13388_read_reg24(NAFE1, 0x40, &val));
	zassert_equal(val, 0x123456U);
	ZASSERT_FRAME(E1, 0, 0xC0, 0x80, 0x00, 0x00, 0x00);

	/* The value is returned unsigned, not sign extended */
	D1->reg24[0x41] = 0xFFFFFEU;
	zassert_ok(nafe13388_read_reg24(NAFE1, 0x41, &val));
	zassert_equal(val, 0xFFFFFEU);

	zassert_ok(nafe13388_read_reg24(NAFE1, 0xAE, &val));
	zassert_equal(val, 0x00A5A5U, "SERIAL1");
	zassert_ok(nafe13388_read_reg24(NAFE1, 0xAF, &val));
	zassert_equal(val, 0x5A5A5AU, "SERIAL0");
}

ZTEST(nafe13388_raw, test_write24)
{
	uint32_t val = 0;

	/* Only the lowest 24 bits are used */
	zassert_ok(nafe13388_write_reg24(NAFE1, 0x80, 0xFFABCDEFU));

	zassert_equal(nafe_emul_frame_count(E1), 1);
	ZASSERT_FRAME(E1, 0, 0x81, 0x00, 0xAB, 0xCD, 0xEF);
	zassert_equal(D1->reg24[0x80], 0xABCDEFU);

	zassert_ok(nafe13388_read_reg24(NAFE1, 0x80, &val));
	zassert_equal(val, 0xABCDEFU);
}

ZTEST(nafe13388_raw, test_24bit_range)
{
	static const uint8_t bad[] = {0x00, 0x20, 0x3F, 0x70, 0x7F, 0xB0, 0xFF};
	uint32_t val;

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(nafe13388_read_reg24(NAFE1, bad[i], &val), -EINVAL, "0x%02x", bad[i]);
		zassert_equal(nafe13388_write_reg24(NAFE1, bad[i], 0), -EINVAL, "0x%02x", bad[i]);
	}
	zassert_equal(nafe13388_read_reg24(NAFE1, 0x40, NULL), -EINVAL);
	zassert_equal(nafe_emul_frame_count(E1), 0);

	/* The edges of the two ranges are accepted */
	zassert_ok(nafe13388_read_reg24(NAFE1, 0x40, &val));
	zassert_ok(nafe13388_read_reg24(NAFE1, 0x6F, &val));
	zassert_ok(nafe13388_read_reg24(NAFE1, 0x80, &val));
	zassert_ok(nafe13388_read_reg24(NAFE1, 0xAF, &val));
}
