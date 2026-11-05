/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Continuous readings (CMD_SC, CMD_MC) with the callback. The emulator only selects
 * the mode on these commands; the test sends the DRDY pulses with nafe_emul_tick().
 * The callback runs in the acquisition thread of the driver.
 *
 * -EOVERFLOW is produced by a DRDY pulse that is sent from inside the callback: the
 * event is then already pending when the driver goes back to wait for the next one.
 */

#include <errno.h>
#include <string.h>

#include "common.h"

ZTEST_SUITE(nafe13388_continuous, NULL, NULL, nafe_before, nafe_after, NULL);

#define MAX_CALLS 8

struct cont_ctx {
	struct k_sem got;
	int calls;
	int status[MAX_CALLS];
	unsigned int n[MAX_CALLS];
	int32_t raw[MAX_CALLS][2];
	bool raw_null[MAX_CALLS];
	/* What the callback does in its first call */
	bool tick_in_cb;
	bool stop_in_cb;
	int stop_ret;
	/* cfg_rate of channel 0 to rate code 8, delay 0x12, in the first call */
	bool rate_in_cb;
	int rate_ret;
	/* Index of the call that returns non-zero, -1 for never */
	int nonzero_at;
};

static void ctx_init(struct cont_ctx *c)
{
	memset(c, 0, sizeof(*c));
	k_sem_init(&c->got, 0, MAX_CALLS);
	c->nonzero_at = -1;
}

static int cont_cb(const struct device *dev, const int32_t *raw, unsigned int n, int status,
		   void *user_data)
{
	struct cont_ctx *c = user_data;
	int i = c->calls;

	if (i < MAX_CALLS) {
		c->status[i] = status;
		c->n[i] = n;
		c->raw_null[i] = (raw == NULL);
		if (raw != NULL && n <= 2U) {
			memcpy(c->raw[i], raw, n * sizeof(int32_t));
		}
	}
	c->calls++;

	if (c->tick_in_cb && i == 0) {
		(void)nafe_emul_tick(E1);
	}
	if (c->rate_in_cb && i == 0) {
		c->rate_ret = nafe13388_cfg_rate(dev, 0, 8, 0x12);
	}
	if (c->stop_in_cb && i == 0) {
		c->stop_ret = nafe13388_continuous_stop(dev, NAFE13388_STOP_ABORT);
	}

	k_sem_give(&c->got);

	return (i == c->nonzero_at) ? -ECANCELED : 0;
}

/*
 * Wait for a callback. The test thread has a higher priority than the driver
 * thread, so it preempts the driver as soon as the callback gives the semaphore,
 * before the driver is back at its DRDY wait. Sleep a little so that the driver
 * gets there; else the next DRDY pulse would find the previous one still counted
 * as pending and be reported as -EOVERFLOW.
 */
static void wait_cb(struct cont_ctx *c)
{
	zassert_ok(k_sem_take(&c->got, K_MSEC(500)), "no callback");
	NAFE_SETTLE();
}

ZTEST(nafe13388_continuous, test_mc_callback)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	D1->code[0] = 1000;
	D1->code[1] = -2000;

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	/* MCH_EN and CMD_MC; SYS_CONFIG0 is already right, no CMD_CHn for MC */
	zassert_equal(nafe_emul_frame_count(E1), 2);
	ZASSERT_FRAME(E1, 0, 0x80, 0x48, 0x00, 0x03);
	ZASSERT_FRAME(E1, 1, 0xC0, 0x06);
	NAFE_SETTLE();
	zassert_equal(c.calls, 0, "nothing converted yet");

	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_equal(c.calls, 1);
	zassert_equal(c.status[0], 0);
	zassert_equal(c.n[0], 2);
	zassert_equal(c.raw[0][0], 1000);
	zassert_equal(c.raw[0][1], -2000);
	ZASSERT_FRAME(E1, 2, 0xC0, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

	D1->code[0] = 5;
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_equal(c.calls, 2);
	zassert_equal(c.status[1], 0);
	zassert_equal(c.raw[1][0], 5);
	zassert_equal(D1->pulses, 2, "one DRDY per sequence");

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_END));
	zassert_equal(D1->ends, 1);
	ZASSERT_FRAME(E1, 4, 0x80, 0x22);
	ZASSERT_IDLE();
	zassert_equal(nafe_emul_tick(E1), -EINVAL, "the emulator stopped converting");
	zassert_equal(c.calls, 2, "no callback after the stop");
}

ZTEST(nafe13388_continuous, test_sc_16bit)
{
	struct nafe13388_seq seq = nafe_seq(BIT(1), NAFE13388_MODE_SC, 16);
	struct cont_ctx c;

	ctx_init(&c);
	D1->code[1] = -0x1000;

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	/* SYS_CONFIG0 = ADC_DATA_OUT_16BIT | DRDY_PIN_EDGE, MCH_EN, CMD_CH1, CMD_SC */
	zassert_equal(nafe_emul_frame_count(E1), 4);
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x40, 0x10);
	ZASSERT_FRAME(E1, 1, 0x80, 0x48, 0x00, 0x02);
	ZASSERT_FRAME(E1, 2, 0x80, 0x02);
	ZASSERT_FRAME(E1, 3, 0xC0, 0x02);
	NAFE_SETTLE();

	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_equal(c.status[0], 0);
	zassert_equal(c.n[0], 1);
	zassert_equal(c.raw[0][0], -16, "sign extended 16 bit value");
	ZASSERT_FRAME(E1, 4, 0xC0, 0x0A, 0x00, 0x00);

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	zassert_equal(D1->aborts, 1);
	ZASSERT_IDLE();
}

/* A pending DRDY when the driver is about to wait: the sequence was too slow */
ZTEST(nafe13388_continuous, test_overflow)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	c.tick_in_cb = true;

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();

	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	wait_cb(&c);
	zassert_equal(c.calls, 2, "calls %d, status %d %d %d, pulses %u", c.calls, c.status[0],
		      c.status[1], c.status[2], D1->pulses);
	zassert_equal(c.status[0], 0);
	zassert_equal(c.status[1], -EOVERFLOW);
	zassert_equal(c.n[1], 2, "the data is still delivered");
	zassert_equal(D1->pulses, 2);

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	ZASSERT_IDLE();
}

/* No DRDY for CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS: fatal, the device is aborted */
ZTEST(nafe13388_continuous, test_fatal_timeout)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;
	int64_t t0;
	int dt;

	ctx_init(&c);
	t0 = k_uptime_get();

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	wait_cb(&c);
	dt = (int)(k_uptime_get() - t0);

	zassert_true(dt >= CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS &&
			     dt < 4 * CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS,
		     "waited %d ms", dt);
	zassert_equal(c.calls, 1);
	zassert_equal(c.status[0], -ETIMEDOUT);
	zassert_equal(c.n[0], 0);
	zassert_true(c.raw_null[0]);

	nafe_wait_idle();
	zassert_equal(D1->aborts, 1);
	ZASSERT_FRAME(E1, 2, 0x80, 0x20);
	zassert_equal(c.calls, 1, "the loop ended");
	zassert_equal(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT), -EALREADY);
}

/* A SPI error in the burst read is fatal as well */
ZTEST(nafe13388_continuous, test_fatal_spi_error)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();

	D1->fail_countdown = 1; /* the burst frame */
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);

	zassert_equal(c.status[0], -EIO);
	zassert_equal(c.n[0], 0);
	nafe_wait_idle();
	zassert_equal(D1->aborts, 1);
}

/* continuous_stop() called from the callback does not wait for itself */
ZTEST(nafe13388_continuous, test_stop_from_callback)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	c.stop_in_cb = true;
	c.stop_ret = -1;

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();

	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	nafe_wait_idle();

	zassert_equal(c.stop_ret, 0);
	zassert_equal(c.calls, 1, "no callback after the stop");
	zassert_equal(D1->aborts, 1, "one ABORT only");
	zassert_equal(nafe_emul_tick(E1), -EINVAL);
}

/*
 * cfg_rate() around a continuous reading. The call is refused with -EBUSY while the
 * reading runs (a change from the callback can be lost by the chip, and the driver
 * would have updated its copy anyway). Before the start and after a stop it works, and a restart
 * sends only CMD_MC (MCH_EN and the rate are kept).
 */
ZTEST(nafe13388_continuous, test_cfg_rate_before_in_callback_and_restart)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	/* Before the start: CMD_CH0, CH_CONFIG1 (8 << 3), CH_CONFIG2 (0x12 << 10) */
	ctx_init(&c);
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 8, 0x12));
	zassert_equal(nafe_emul_frame_count(E1), 3);
	ZASSERT_FRAME(E1, 0, 0x80, 0x00);
	ZASSERT_FRAME(E1, 1, 0x80, 0x42, 0x00, 0x40);
	ZASSERT_FRAME(E1, 2, 0x80, 0x44, 0x48, 0x00);
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	zassert_equal(nafe_emul_frame_count(E1), 5);
	ZASSERT_FRAME(E1, 3, 0x80, 0x48, 0x00, 0x03); /* MCH_EN */
	ZASSERT_FRAME(E1, 4, 0xC0, 0x06);             /* CMD_MC */
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));

	/* In the callback of the first call: refused, nothing is sent, the copy is kept */
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 4, 0));
	nafe_emul_clear_log(E1);
	ctx_init(&c);
	c.rate_in_cb = true;
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_equal(c.rate_ret, -EBUSY);
	/* start: MCH_EN is kept, CMD_MC; callback: burst only */
	zassert_equal(nafe_emul_frame_count(E1), 2);
	ZASSERT_FRAME(E1, 0, 0xC0, 0x06);
	zassert_equal(D1->ch[0][1], 0x0020, "the rate is still 4");
	zassert_equal(D1->ch[0][2], 0x0000);

	/* The reading goes on, after the stop the same call works again (copy and chip agree) */
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_equal(c.calls, 2);
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	nafe_emul_clear_log(E1);
	zassert_ok(nafe13388_cfg_rate(NAFE1, 0, 8, 0x12));
	zassert_equal(nafe_emul_frame_count(E1), 3, "the rate change after the stop is written");
	zassert_equal(D1->ch[0][1], 0x0040);
	zassert_equal(D1->ch[0][2], 0x4800);

	/* A restart sends only CMD_MC */
	nafe_emul_clear_log(E1);
	ctx_init(&c);
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	zassert_equal(nafe_emul_frame_count(E1), 1);
	ZASSERT_FRAME(E1, 0, 0xC0, 0x06);
	zassert_equal(D1->ch[0][1], 0x0040, "the rate is still the new one");
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
}

/*
 * END lets the running conversion finish: SYS_STATUS0 keeps the MC active bit for a while
 * after END. stop(END) polls until the bit is clear, ABORT does not poll.
 */
ZTEST(nafe13388_continuous, test_stop_end_waits_for_idle)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	D1->end_busy_reads = 3;
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();
	D1->status_reads = 0;

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_END));
	zassert_equal(D1->ends, 1);
	zassert_equal(D1->status_reads, 4, "3 reads with the bit set, then one with it clear");
	zassert_equal(D1->aborts, 0);
	ZASSERT_IDLE();

	/* ABORT does not poll */
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();
	D1->status_reads = 0;
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	zassert_equal(D1->status_reads, 0);
	zassert_equal(D1->aborts, 1);
	ZASSERT_IDLE();
}

/* The device never gets idle after END: the driver aborts it and reports the timeout */
ZTEST(nafe13388_continuous, test_stop_end_stuck)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	D1->end_busy_reads = -1;
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();
	D1->status_reads = 0;

	zassert_equal(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_END), -ETIMEDOUT);
	zassert_equal(D1->aborts, 1, "the device is aborted");
	zassert_true(D1->status_reads > 1);
	ZASSERT_IDLE();

	/* The device can be used again */
	D1->end_busy_reads = 0;
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
}

/* continuous_stop() from another thread while the loop waits for DRDY */
ZTEST(nafe13388_continuous, test_stop_from_other_thread)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_END));
	ZASSERT_IDLE();
	zassert_equal(D1->ends, 1);
	zassert_equal(c.calls, 1, "the wake-up of the loop is not a callback");
	zassert_equal(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_END), -EALREADY);

	/* The device can be used again */
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	ZASSERT_IDLE();
}

/* A callback that returns non-zero ends the reading with an ABORT */
ZTEST(nafe13388_continuous, test_callback_returns_nonzero)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);
	c.nonzero_at = 1;

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	NAFE_SETTLE();
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	nafe_wait_idle();

	zassert_equal(c.calls, 2);
	zassert_equal(D1->aborts, 1);
	zassert_equal(nafe_emul_tick(E1), -EINVAL);
}

/* The SYNC pin starts the conversions: the first pulse has to come in time */
ZTEST(nafe13388_continuous, test_sync_pin)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	seq.sync_pin = true;
	ctx_init(&c);

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	/* SYS_CONFIG0 = ADC_SYNC | DRDY_PIN_EDGE, MCH_EN, CMD_MC */
	ZASSERT_FRAME(E1, 0, 0x80, 0x60, 0x00, 0x30);
	NAFE_SETTLE();
	zassert_ok(nafe13388_sync_pulse(NAFE1));
	zassert_equal(D1->sync_edges, 1);
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);
	zassert_equal(c.status[0], 0);
	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));

	/* No pulse: the same -ETIMEDOUT as for a missing DRDY */
	ctx_init(&c);
	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	wait_cb(&c);
	zassert_equal(c.status[0], -ETIMEDOUT);
	nafe_wait_idle();
}

ZTEST(nafe13388_continuous, test_start_errors)
{
	struct nafe13388_seq seq;
	struct cont_ctx c;

	ctx_init(&c);

	seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, NULL, &c), -EINVAL, "no callback");
	zassert_equal(nafe13388_continuous_start(NAFE1, NULL, cont_cb, &c), -EINVAL);

	seq = nafe_seq(BIT(0), NAFE13388_MODE_SS, 24);
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c), -EINVAL, "SS");
	seq = nafe_seq(0x3, NAFE13388_MODE_MM, 24);
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c), -EINVAL, "MM");
	seq = nafe_seq(0x3, NAFE13388_MODE_MS, 24);
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c), -EINVAL, "MS");
	seq = nafe_seq(0x3, NAFE13388_MODE_SC, 24);
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c), -EINVAL, "SC needs 1");

	/* One burst per DRDY: DRDY per channel is refused for several channels */
	seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	seq.drdy_per_channel = true;
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c), -EINVAL);

	seq = nafe_seq(BIT(2), NAFE13388_MODE_MC, 24);
	zassert_equal(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c), -EINVAL, "not set up");

	seq = nafe_seq(BIT(0), NAFE13388_MODE_MC, 24);
	seq.sync_pin = true;
	zassert_equal(nafe13388_continuous_start(NAFE5, &seq, cont_cb, &c), -ENOTSUP, "no SYNC");

	zassert_equal(nafe_emul_frame_count(E1), 0, "a refused start sends nothing");
	ZASSERT_IDLE();

	zassert_equal(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT), -EALREADY);
	zassert_equal(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_IDLE), -EINVAL);
	zassert_equal(nafe_emul_frame_count(E1), 0);
}

/*
 * The first DRDY can arrive before the driver thread has run at all (the test thread does
 * not sleep after the start). That is not a late sequence: nothing came before it, so the
 * first callback must not carry -EOVERFLOW. Flushing the semaphore instead would lose this
 * DRDY and end in a timeout.
 */
ZTEST(nafe13388_continuous, test_first_callback_not_late)
{
	struct nafe13388_seq seq = nafe_seq(0x3, NAFE13388_MODE_MC, 24);
	struct cont_ctx c;

	ctx_init(&c);

	zassert_ok(nafe13388_continuous_start(NAFE1, &seq, cont_cb, &c));
	zassert_ok(nafe_emul_tick(E1));
	wait_cb(&c);

	zassert_equal(c.calls, 1);
	zassert_equal(c.status[0], 0, "the first callback is not late");
	zassert_equal(c.n[0], 2);

	zassert_ok(nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT));
	ZASSERT_IDLE();
}
