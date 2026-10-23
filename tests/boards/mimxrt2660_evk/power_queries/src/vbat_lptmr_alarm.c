/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define ALARM_CHANNEL      0U
#define CONSECUTIVE_ALARMS 2U
#define ALARM_DELAY_MS     1000

/*
 * The VBAT LPTMR counts FRO32K, which is not a crystal, while the uptime it is
 * compared against comes from SysTick. The bound only has to tell an alarm on time
 * from one that fires at once or not at all.
 */
#define ALARM_TOLERANCE_MS 100

/* The devicetree rate of the timer, which soc.c sets up from FRO32K */
#define VBAT_LPTMR_HZ 32768U

static const struct device *const vbat_lptmr = DEVICE_DT_GET(DT_NODELABEL(vbat_lptmr));

struct alarm_record {
	struct k_sem expired;
	int64_t expired_at_ms;
};

static struct alarm_record record;

static void alarm_expired(const struct device *dev, uint8_t chan_id, uint32_t ticks,
			  void *user_data)
{
	struct alarm_record *expiry = user_data;

	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);
	ARG_UNUSED(ticks);

	expiry->expired_at_ms = k_uptime_get();
	k_sem_give(&expiry->expired);
}

static void assert_alarm_expires_on_time(uint32_t alarm_number)
{
	const struct counter_alarm_cfg alarm = {
		.callback = alarm_expired,
		.ticks = counter_us_to_ticks(vbat_lptmr, ALARM_DELAY_MS * USEC_PER_MSEC),
		.user_data = &record,
		.flags = 0U,
	};
	int64_t armed_at_ms = k_uptime_get();
	int32_t elapsed_ms;

	zassert_ok(counter_set_channel_alarm(vbat_lptmr, ALARM_CHANNEL, &alarm),
		   "Cannot set alarm %u", alarm_number);
	zassert_ok(k_sem_take(&record.expired, K_MSEC(ALARM_DELAY_MS + ALARM_TOLERANCE_MS)),
		   "Alarm %u did not expire", alarm_number);

	elapsed_ms = (int32_t)(record.expired_at_ms - armed_at_ms);
	zassert_between_inclusive(elapsed_ms, ALARM_DELAY_MS - ALARM_TOLERANCE_MS,
				  ALARM_DELAY_MS + ALARM_TOLERANCE_MS,
				  "Alarm %u expired after %d ms, set for %d ms", alarm_number,
				  elapsed_ms, ALARM_DELAY_MS);
}

ZTEST(vbat_lptmr_alarm, test_counts_at_the_devicetree_rate)
{
	zassert_equal(counter_get_frequency(vbat_lptmr), VBAT_LPTMR_HZ, "vbat_lptmr reports %u Hz",
		      counter_get_frequency(vbat_lptmr));
}

/* The second alarm starts the timer again after the first one stopped it. */
ZTEST(vbat_lptmr_alarm, test_consecutive_alarms_expire_on_time)
{
	for (uint32_t alarm_number = 1U; alarm_number <= CONSECUTIVE_ALARMS; alarm_number++) {
		assert_alarm_expires_on_time(alarm_number);
	}
}

static void *vbat_lptmr_alarm_setup(void)
{
	zassert_true(device_is_ready(vbat_lptmr), "vbat_lptmr is not ready");
	k_sem_init(&record.expired, 0, 1);

	return NULL;
}

/* A failed test can leave its alarm armed. */
static void vbat_lptmr_alarm_after(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(counter_cancel_channel_alarm(vbat_lptmr, ALARM_CHANNEL),
		   "Cannot cancel the alarm");
	k_sem_reset(&record.expired);
}

ZTEST_SUITE(vbat_lptmr_alarm, NULL, vbat_lptmr_alarm_setup, NULL, vbat_lptmr_alarm_after, NULL);
