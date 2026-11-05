/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Init sequence of the driver. nafe2, nafe3 and nafe4 have "zephyr,deferred-init"
 * and are initialised here, once each (a device can be initialised only once).
 * The expected frames are computed from the data sheet: command word = DEV_AD << 15
 * | RW_L << 14 | address << 1.
 */

#include <errno.h>
#include <string.h>

#include "common.h"

#define NAFE2 DEVICE_DT_GET(DT_NODELABEL(nafe2))
#define NAFE3 DEVICE_DT_GET(DT_NODELABEL(nafe3))
#define NAFE4 DEVICE_DT_GET(DT_NODELABEL(nafe4))
#define E2    NAFE_EMUL(nafe2)
#define E3    NAFE_EMUL(nafe3)
#define E4    NAFE_EMUL(nafe4)

ZTEST_SUITE(nafe13388_init, NULL, NULL, NULL, NULL, NULL);

/* nafe1 is not deferred and has been initialised at boot */
ZTEST(nafe13388_init, test_boot)
{
	zassert_true(device_is_ready(NAFE1));
	zassert_false(device_is_ready(NAFE2));
}

/*
 * SPI_ADDR = 0, external clock, REF_SEL = 2, wide DRDY pulse, one channel. The
 * chip needs 3 reads of SYS_STATUS0 to become ready.
 */
ZTEST(nafe13388_init, test_init_sequence)
{
	const struct emul *e = E2;
	struct nafe_emul_data *d = e->data;

	nafe_emul_defaults(e);
	d->reset_not_ready = 2;

	zassert_false(device_is_ready(NAFE2));
	zassert_ok(device_init(NAFE2));
	zassert_true(device_is_ready(NAFE2));

	zassert_equal(d->resets, 1, "one RSTB pulse");
	zassert_true(d->rstb_low_us >= 100, "RSTB was low for %u us", d->rstb_low_us);
	zassert_equal(d->status_reads, 3);
	zassert_equal(nafe_emul_frame_count(e), 13);

	/* SYS_STATUS0 until CHIP_READY (0x31) */
	ZASSERT_FRAME(e, 0, 0x40, 0x62, 0x00, 0x00);
	ZASSERT_FRAME(e, 1, 0x40, 0x62, 0x00, 0x00);
	ZASSERT_FRAME(e, 2, 0x40, 0x62, 0x00, 0x00);
	/* PN2 (0x7C), PN0 (0x7E), SERIAL1 (0xAE), SERIAL0 (0xAF) */
	ZASSERT_FRAME(e, 3, 0x40, 0xF8, 0x00, 0x00);
	ZASSERT_FRAME(e, 4, 0x40, 0xFC, 0x00, 0x00);
	ZASSERT_FRAME(e, 5, 0x41, 0x5C, 0x00, 0x00, 0x00);
	ZASSERT_FRAME(e, 6, 0x41, 0x5E, 0x00, 0x00, 0x00);
	/*
	 * SYS_CONFIG0 (0x30) = DRDY_PWDT 0x8000 | REF_SEL 2 << 10 | CK_SRC_SEL 2 << 8 |
	 * DRDY_PIN_EDGE 0x10 = 0x8A10
	 */
	ZASSERT_FRAME(e, 7, 0x00, 0x60, 0x8A, 0x10);
	/*
	 * Channel 3: CMD_CH3, then CH_CONFIG0..3. CONFIG0 = HV_AIP 2 << 12 | HV_AIN 3 << 8 |
	 * gain 2x (code 4) << 5 | HV_SEL 0x10 = 0x2390, CONFIG1 = rate 7 << 3 = 0x38.
	 */
	ZASSERT_FRAME(e, 8, 0x00, 0x06);
	ZASSERT_FRAME(e, 9, 0x00, 0x40, 0x23, 0x90);
	ZASSERT_FRAME(e, 10, 0x00, 0x42, 0x00, 0x38);
	ZASSERT_FRAME(e, 11, 0x00, 0x44, 0x00, 0x00);
	ZASSERT_FRAME(e, 12, 0x00, 0x46, 0x00, 0x00);

	zassert_equal(d->sys_config0, 0x8A10);
	zassert_equal(d->ch[3][0], 0x2390);
	zassert_equal(d->ch[3][1], 0x0038);
	zassert_equal(d->mch_en, 0);
	zassert_equal(d->pulses, 0);
	zassert_equal(d->errors, 0);

	zassert_equal(device_init(NAFE2), -EALREADY);
}

/* A part number other than 0x1338 is refused, nothing is configured */
ZTEST(nafe13388_init, test_init_wrong_part)
{
	const struct emul *e = E3;
	struct nafe_emul_data *d = e->data;

	nafe_emul_defaults(e);
	d->pn2 = 0x1234;

	zassert_equal(device_init(NAFE3), -ENODEV);
	zassert_false(device_is_ready(NAFE3));

	zassert_equal(nafe_emul_frame_count(e), 2);
	ZASSERT_FRAME(e, 0, 0xC0, 0x62, 0x00, 0x00);
	ZASSERT_FRAME(e, 1, 0xC0, 0xF8, 0x00, 0x00);
	zassert_equal(d->errors, 0);
}

/* CHIP_READY never comes: the driver polls for CONFIG_..._CHIP_READY_TIMEOUT_MS milliseconds */
ZTEST(nafe13388_init, test_init_chip_not_ready)
{
	const struct emul *e = E4;
	struct nafe_emul_data *d = e->data;
	int64_t t0;
	int dt;

	nafe_emul_defaults(e);
	d->reset_not_ready = -1;

	t0 = k_uptime_get();
	zassert_equal(device_init(NAFE4), -ETIMEDOUT);
	dt = (int)(k_uptime_get() - t0);
	zassert_false(device_is_ready(NAFE4));

	/* The number of polls depends on the tick, the time does not (review finding F6) */
	zassert_true(dt >= CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS &&
			     dt < 4 * CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS,
		     "waited %d ms", dt);
	zassert_true(d->status_reads >= 2, "polled %u times", d->status_reads);
	zassert_equal(nafe_emul_frame_count(e), d->status_reads);
	zassert_equal(d->errors, 0);
}

struct thread_search {
	const char *name;
	int found;
};

static void thread_cb(const struct k_thread *thread, void *user_data)
{
	struct thread_search *search = user_data;
	const char *name = k_thread_name_get((k_tid_t)thread);

	if (name != NULL && strcmp(name, search->name) == 0) {
		search->found++;
	}
}

/* Every initialised instance has an acquisition thread that is named after its device */
ZTEST(nafe13388_init, test_thread_name_per_instance)
{
	struct thread_search first = {.name = NAFE1->name};
	struct thread_search second = {.name = NAFE5->name};

	zassert_true(strcmp(NAFE1->name, NAFE5->name) != 0);

	k_thread_foreach(thread_cb, &first);
	k_thread_foreach(thread_cb, &second);

	zassert_equal(first.found, 1, "thread of %s", NAFE1->name);
	zassert_equal(second.found, 1, "thread of %s", NAFE5->name);
}
