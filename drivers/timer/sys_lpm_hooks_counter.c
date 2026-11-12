/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of the system timer low-power companion interface
 * using the standard Counter API.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/timer/system_timer_lpm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(sys_lpm_hooks_counter);

BUILD_ASSERT(DT_PROP(SYSTEM_TIMER_COMPANION_NODE, wakeup_source),
	"The Low-Power Companion counter (chosen `zephyr,system-timer-companion` at path: "
	DT_NODE_PATH(SYSTEM_TIMER_COMPANION_NODE) ") must be marked as wake-up capable! "
	"(hint: add the `wakeup-source` property to the node)");

static const struct device *lpm_counter = DEVICE_DT_GET(SYSTEM_TIMER_COMPANION_NODE);

/* Companion counter's value before entry in low-power state. */
static uint32_t counter_pre_lpm_ticks;

/*
 * How many ticks the system was expected to remain in low-power state.
 * Used to determine if the counter overflowed or not.
 */
static uint32_t counter_scheduled_lpm_ticks;

/* Channel ID of the alarm that should wake up the system */
#define LPM_ALARM_CHANNEL_ID 0U

static void check_counter_result(int err, const char *operation)
{
	if (err != 0) {
		LOG_ERR("Companion counter %s failed (%d)", operation, err);
		k_panic();
	}
}

void z_sys_clock_lpm_init(void)
{
	__ASSERT(!IS_ENABLED(CONFIG_PM_DEVICE) || pm_device_wakeup_is_capable(lpm_counter),
		"Low-Power Companion counter not marked as wakeup-source!");

	/*
	 * Enable low-power companion counter as wake-up source.
	 *
	 * This cannot be done from z_sys_clock_lpm_enter() because this runs
	 * after all devices have been suspended, which is what enabling the
	 * counter as wake-up source is supposed to prevent.
	 *
	 * NOTE: it is assumed that no other code will change this configuration.
	 * Doing so is an application error and will result in undefined behavior.
	 */
	pm_device_wakeup_enable(lpm_counter, true);
}

/* Stub to satisfy the Counter API (`callback` cannot be NULL) */
static void stub_alarm_callback(const struct device *dev, uint8_t chan_id,
				      uint32_t ticks, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);
}

void z_sys_clock_lpm_enter(uint64_t max_lpm_time_us)
{
	int err;

	check_counter_result(device_is_ready(lpm_counter) ? 0 : -ENODEV, "initialization");

	/* Catch misuse of the device by external code */
	__ASSERT(!IS_ENABLED(CONFIG_PM_DEVICE) || pm_device_wakeup_is_enabled(lpm_counter),
		"External code must not disable Low-Power Companion wake-up source!");

	counter_scheduled_lpm_ticks = counter_us_to_ticks(lpm_counter, max_lpm_time_us);

	struct counter_alarm_cfg cfg = {
		.callback = stub_alarm_callback,
		.ticks = counter_scheduled_lpm_ticks,
		.user_data = NULL,
		.flags = 0,
	};

	/* Disable the counter alarm in case it was already running */
	err = counter_cancel_channel_alarm(lpm_counter, LPM_ALARM_CHANNEL_ID);
	/* Cancellation may be unsupported until the counter has been started. */
	if (err != -ENOTSUP) {
		check_counter_result(err, "alarm cancellation");
	}

	/*
	 * Setting an alarm does not necessarily start its counter. Start after
	 * cancellation, as some drivers stop the counter when cancelling an alarm.
	 */
	err = counter_start(lpm_counter);
	/* Always-running counters may not support an explicit start operation. */
	if ((err != -EALREADY) && (err != -ENOTSUP)) {
		check_counter_result(err, "start");
	}

	/* Configure the alarm to wake up the system as requested */
	check_counter_result(counter_set_channel_alarm(lpm_counter, LPM_ALARM_CHANNEL_ID, &cfg),
			     "alarm configuration");

	/*
	 * Store the current value of the counter to allow computing
	 * how much time was really spent in low-power state, as the
	 * system may be awoken earlier than scheduled.
	 */
	check_counter_result(counter_get_value(lpm_counter, &counter_pre_lpm_ticks), "entry read");
}

uint64_t z_sys_clock_lpm_exit(void)
{
	/* Compute how much time elapsed according to the counter */
	uint32_t counter_top, post_lpm_ticks, ticks_elapsed;
	bool counter_int_pending, wraparound_occurred;

	counter_int_pending = !!counter_get_pending_int(lpm_counter);
	counter_top = counter_get_top_value(lpm_counter);
	check_counter_result(counter_get_value(lpm_counter, &post_lpm_ticks), "exit read");

	/* Check for counter overflow/wraparound */
	/* TODO: this works only for upcounting timers! */
	if (counter_pre_lpm_ticks > post_lpm_ticks) {
		/* Pre > Post: counter wrapped around (overflow occurred) */
		wraparound_occurred = true;
	} else if (counter_pre_lpm_ticks == post_lpm_ticks) {
		/* Pre == Post: counter wrapped around if interrupt is pending */
		wraparound_occurred = counter_int_pending;
	} else {
		/* Pre < Post: usually means no wraparound; however, if interrupt
		 * is pending and an overflow was expected to occur based on how
		 * many ticks we scheduled, then treat this as wraparound.
		 */
		wraparound_occurred = counter_int_pending &&
			((uint64_t)counter_pre_lpm_ticks + counter_scheduled_lpm_ticks
				>= counter_top);
	}

	if (wraparound_occurred) {
		ticks_elapsed = (counter_top - counter_pre_lpm_ticks) + post_lpm_ticks + 1;
	} else {
		ticks_elapsed = post_lpm_ticks - counter_pre_lpm_ticks;
	}

	return (uint64_t)counter_ticks_to_us(lpm_counter, ticks_elapsed);
}
