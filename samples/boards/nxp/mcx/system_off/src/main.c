/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/drivers/wuc.h>

#include <zephyr/console/console.h>

#if defined(CONFIG_SYSTEM_OFF_WAKEUP_TIMER)
#include <zephyr/drivers/counter.h>

#define WAKEUP_DELAY_S 5U
static const struct device *const lptmr = DEVICE_DT_GET(DT_NODELABEL(lptmr0));
/* LPTMR0 reaches the core as a WUU internal-module wakeup source. */
static const struct wuc_dt_spec wakeup = WUC_DT_SPEC_GET(DT_NODELABEL(lptmr0));
#else
/* The wakeup button (a WUU external pin) is described per board and aliased as
 * "wakeup-button".
 */
static const struct wuc_dt_spec wakeup = WUC_DT_SPEC_GET(DT_ALIAS(wakeup_button));
#endif

#if defined(CONFIG_SYSTEM_OFF_WAKEUP_TIMER)
static void wakeup_alarm_cb(const struct device *dev, uint8_t chan_id, uint32_t ticks,
			    void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);
}

static int arm_timer_wakeup(void)
{
	struct counter_alarm_cfg cfg = {
		.callback = wakeup_alarm_cb,
		.ticks = counter_us_to_ticks(lptmr, (uint64_t)WAKEUP_DELAY_S * USEC_PER_SEC),
		.flags = 0,
	};

	(void)counter_start(lptmr);

	return counter_set_channel_alarm(lptmr, 0, &cfg);
}
#endif

int main(void)
{
	int ret;

	printk("%s system off demo\n", CONFIG_BOARD);

	if (!device_is_ready(wakeup.dev)) {
		printk("WUC device %s not ready\n", wakeup.dev->name);
		return 0;
	}

#if defined(CONFIG_SYSTEM_OFF_WAKEUP_TIMER)
	if (!device_is_ready(lptmr)) {
		printk("LPTMR counter device not ready\n");
		return 0;
	}
#endif

	/* Wait for a console key press before entering system off. Until a key
	 * arrives the SoC stays awake and debuggable, so a freshly programmed
	 * board never bricks its debug port by powering off on its own before
	 * it can be re-flashed. The wakeup then resets the board, which re-runs
	 * main() and prompts again - one key press per system-off cycle.
	 */
	console_init();
	printk("Press any key to enter system off\n");
	(void)console_getchar();

#if defined(CONFIG_SYSTEM_OFF_WAKEUP_TIMER)
	ret = arm_timer_wakeup();
	if (ret < 0) {
		printk("Failed to arm LPTMR wakeup (%d)\n", ret);
		return 0;
	}
#endif

	ret = wuc_enable_wakeup_source_dt(&wakeup);
	if (ret < 0) {
		printk("Failed to enable wakeup source (%d)\n", ret);
		return 0;
	}

#if defined(CONFIG_SYSTEM_OFF_WAKEUP_TIMER)
	printk("Entering system off; the timer wakes it in %u s\r\n", WAKEUP_DELAY_S);
#else
	printk("Entering system off; trigger the wakeup pin to restart\r\n");
#endif

	sys_poweroff();

	/* Deep Power Down wakes through the reset routine, so this is never
	 * reached on a working system off.
	 */
	printk("ERROR: System off failed\n");

	return 0;
}
