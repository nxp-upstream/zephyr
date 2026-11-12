/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/timer/system_timer_lpm.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

#define TEST_COUNTER_NODE DT_NODELABEL(test_counter)
#define TEST_COUNTER_TOP 999U

struct test_counter_data {
	struct counter_alarm_cfg alarm;
	uint32_t value;
	uint32_t start_calls;
	uint32_t cancel_calls;
	uint32_t alarm_calls;
	int start_error;
	int cancel_error;
	int alarm_error;
	int read_error;
	bool running;
	bool alarm_active;
	bool cancel_stops;
	bool alarm_reloads;
	bool pending;
};

static struct test_counter_data counter_data;

static int test_counter_start(const struct device *dev)
{
	struct test_counter_data *data = dev->data;

	data->start_calls++;
	zassert_false(data->alarm_active, "Counter must start before arming the alarm");

	if (data->start_error != 0) {
		return data->start_error;
	}

	if (data->running) {
		return -EALREADY;
	}

	data->running = true;
	return 0;
}

static int test_counter_stop(const struct device *dev)
{
	struct test_counter_data *data = dev->data;

	data->running = false;
	data->value = 0;
	return 0;
}

static int test_counter_get_value(const struct device *dev, uint32_t *ticks)
{
	struct test_counter_data *data = dev->data;

	if (data->read_error != 0) {
		return data->read_error;
	}

	*ticks = data->value;
	return 0;
}

static int test_counter_set_alarm(const struct device *dev, uint8_t chan_id,
				  const struct counter_alarm_cfg *cfg)
{
	struct test_counter_data *data = dev->data;

	ARG_UNUSED(chan_id);
	data->alarm_calls++;

	if (data->alarm_error != 0) {
		return data->alarm_error;
	}

	if (!data->running) {
		return -EINVAL;
	}

	if (data->alarm_active) {
		return -EBUSY;
	}

	if (data->alarm_reloads) {
		data->value = 0;
	}

	data->alarm = *cfg;
	data->alarm_active = true;
	return 0;
}

static int test_counter_cancel_alarm(const struct device *dev, uint8_t chan_id)
{
	struct test_counter_data *data = dev->data;

	ARG_UNUSED(chan_id);
	data->cancel_calls++;

	if (data->cancel_error != 0) {
		return data->cancel_error;
	}

	data->alarm_active = false;
	data->pending = false;

	if (data->cancel_stops) {
		data->running = false;
		data->value = 0;
	}

	return 0;
}

static uint32_t test_counter_get_top_value(const struct device *dev)
{
	ARG_UNUSED(dev);
	return TEST_COUNTER_TOP;
}

static uint32_t test_counter_get_pending_int(const struct device *dev)
{
	struct test_counter_data *data = dev->data;

	return data->pending;
}

static int __maybe_unused test_counter_pm_action(const struct device *dev,
						enum pm_device_action action)
{
	ARG_UNUSED(dev);

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_RESUME:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct counter_config_info counter_info = {
	.max_top_value = TEST_COUNTER_TOP,
	.freq = 1000000U,
	.flags = COUNTER_CONFIG_INFO_COUNT_UP,
	.channels = 1,
};

static DEVICE_API(counter, test_counter_api) = {
	.start = test_counter_start,
	.stop = test_counter_stop,
	.get_value = test_counter_get_value,
	.set_alarm = test_counter_set_alarm,
	.cancel_alarm = test_counter_cancel_alarm,
	.get_top_value = test_counter_get_top_value,
	.get_pending_int = test_counter_get_pending_int,
};

PM_DEVICE_DT_DEFINE(TEST_COUNTER_NODE, test_counter_pm_action);
DEVICE_DT_DEFINE(TEST_COUNTER_NODE, NULL, PM_DEVICE_DT_GET(TEST_COUNTER_NODE), &counter_data,
		 &counter_info, POST_KERNEL, CONFIG_COUNTER_INIT_PRIORITY, &test_counter_api);

static void before_test(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(&counter_data, 0, sizeof(counter_data));
	counter_data.value = 100U;
	z_sys_clock_lpm_init();
}

void ztest_post_fatal_error_hook(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	ztest_test_pass();
}

ZTEST(lpm_companion_counter, test_init_does_not_start_counter)
{
	zassert_equal(counter_data.start_calls, 0);
	zassert_equal(counter_data.cancel_calls, 0);
	zassert_false(counter_data.running);
}

ZTEST(lpm_companion_counter, test_stopped_counter_starts_before_alarm)
{
	z_sys_clock_lpm_enter(50U);

	zassert_true(counter_data.running);
	zassert_equal(counter_data.start_calls, 1);
	zassert_equal(counter_data.cancel_calls, 1);
	zassert_equal(counter_data.alarm_calls, 1);
	zassert_equal(counter_data.alarm.ticks, 50U);
	zassert_not_null(counter_data.alarm.callback);
	zassert_equal(counter_data.alarm.flags, 0);

	counter_data.value += 50U;
	zassert_equal(z_sys_clock_lpm_exit(), 50U);
	zassert_true(counter_data.running);
}

ZTEST(lpm_companion_counter, test_cancel_stops_counter)
{
	counter_data.running = true;
	counter_data.alarm_active = true;
	counter_data.cancel_stops = true;

	z_sys_clock_lpm_enter(50U);

	zassert_true(counter_data.running);
	counter_data.value = 50U;
	zassert_equal(z_sys_clock_lpm_exit(), 50U);
}

ZTEST(lpm_companion_counter, test_repeated_entry_on_running_counter)
{
	z_sys_clock_lpm_enter(50U);
	counter_data.value += 50U;
	zassert_equal(z_sys_clock_lpm_exit(), 50U);

	z_sys_clock_lpm_enter(80U);
	counter_data.value += 80U;
	zassert_equal(z_sys_clock_lpm_exit(), 80U);
	zassert_equal(counter_data.start_calls, 2);
}

ZTEST(lpm_companion_counter, test_alarm_reloads_before_entry_snapshot)
{
	counter_data.alarm_reloads = true;
	z_sys_clock_lpm_enter(50U);

	zassert_equal(counter_data.value, 0);
	counter_data.value = 50U;
	counter_data.pending = true;
	zassert_equal(z_sys_clock_lpm_exit(), 50U);
}

ZTEST(lpm_companion_counter, test_early_wakeup)
{
	z_sys_clock_lpm_enter(50U);
	counter_data.value += 20U;
	zassert_equal(z_sys_clock_lpm_exit(), 20U);
}

ZTEST(lpm_companion_counter, test_wraparound)
{
	counter_data.value = 900U;
	z_sys_clock_lpm_enter(200U);
	counter_data.value = 100U;
	counter_data.pending = true;
	zassert_equal(z_sys_clock_lpm_exit(), 200U);
}

ZTEST(lpm_companion_counter, test_cancel_unsupported_before_start)
{
	counter_data.cancel_error = -ENOTSUP;
	z_sys_clock_lpm_enter(50U);

	zassert_true(counter_data.running);
	zassert_true(counter_data.alarm_active);
}

ZTEST(lpm_companion_counter, test_always_running_counter)
{
	counter_data.running = true;
	counter_data.start_error = -ENOTSUP;
	z_sys_clock_lpm_enter(50U);

	zassert_true(counter_data.alarm_active);
	counter_data.value += 50U;
	zassert_equal(z_sys_clock_lpm_exit(), 50U);
}

ZTEST(lpm_companion_counter, test_start_failure)
{
	counter_data.start_error = -EIO;
	ztest_set_fault_valid(true);
	z_sys_clock_lpm_enter(50U);
	zassert_unreachable("Counter start failure must prevent low-power entry");
}

ZTEST(lpm_companion_counter, test_cancel_failure)
{
	counter_data.cancel_error = -EIO;
	ztest_set_fault_valid(true);
	z_sys_clock_lpm_enter(50U);
	zassert_unreachable("Alarm cancellation failure must prevent low-power entry");
}

ZTEST(lpm_companion_counter, test_alarm_failure)
{
	counter_data.alarm_error = -EIO;
	ztest_set_fault_valid(true);
	z_sys_clock_lpm_enter(50U);
	zassert_unreachable("Alarm configuration failure must prevent low-power entry");
}

ZTEST(lpm_companion_counter, test_entry_read_failure)
{
	counter_data.read_error = -EIO;
	ztest_set_fault_valid(true);
	z_sys_clock_lpm_enter(50U);
	zassert_unreachable("Counter read failure must prevent low-power entry");
}

ZTEST(lpm_companion_counter, test_exit_read_failure)
{
	z_sys_clock_lpm_enter(50U);
	counter_data.read_error = -EIO;
	ztest_set_fault_valid(true);
	(void)z_sys_clock_lpm_exit();
	zassert_unreachable("Counter read failure must not return an elapsed time");
}

ZTEST_SUITE(lpm_companion_counter, NULL, NULL, before_test, NULL, NULL);
