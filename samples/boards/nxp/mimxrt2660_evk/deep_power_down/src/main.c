/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/wuc.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <fsl_vbatcon.h>

/*
 * A deep power down wake is a power-on reset, so the cycle count lives in
 * VBATCON GPR[0]: the VBAT domain stays powered in DPD1 and DPD2. The magic in
 * the high half tells a count written by this sample from a stale value.
 */
#define DPD_COUNT_GPR         0U
#define DPD_COUNT_MAGIC       0xD9D0U
#define DPD_COUNT_MAGIC_FIELD GENMASK(31, 16)
#define DPD_COUNT_FIELD       GENMASK(15, 0)

#define DPD_ALARM_CHANNEL 0U

#if defined(CONFIG_SAMPLE_DPD_MODE_DPD1)
#define DPD_MODE_NAME "DPD1"
#define DPD_CPU_ID    0U
#else
#define DPD_MODE_NAME "DPD2"
#endif

#define DPD_WAKEUP_NAME (IS_ENABLED(CONFIG_SAMPLE_DPD_WAKEUP_BUTTON) ? "button" : "timer")

static const struct device *const wakeup_timer = DEVICE_DT_GET(DT_NODELABEL(vbat_lptmr));
static const struct gpio_dt_spec wakeup_button =
	GPIO_DT_SPEC_GET(DT_NODELABEL(wakeup_button), gpios);
static const struct wuc_dt_spec wakeup_button_wuc = WUC_DT_SPEC_GET(DT_NODELABEL(wakeup_button));

static int read_wake_reason(bool *woke)
{
	uint32_t cause = 0U;
	int ret;

	ret = hwinfo_get_reset_cause(&cause);
	if (ret != 0) {
		return ret;
	}

	/* The reset cause is sticky: clear it so a later reset reports only its own. */
	ret = hwinfo_clear_reset_cause();
	if (ret != 0) {
		return ret;
	}

	/* A wake may also report RESET_POR; only RESET_LOW_POWER_WAKE marks it. */
	*woke = (cause & RESET_LOW_POWER_WAKE) != 0U;

	return 0;
}

static uint16_t dpd_count_get(void)
{
	uint32_t gpr = VBATCON_ReadGPR(VBAT__VBATCON, DPD_COUNT_GPR);

	if (FIELD_GET(DPD_COUNT_MAGIC_FIELD, gpr) != DPD_COUNT_MAGIC) {
		return 0U;
	}

	return (uint16_t)FIELD_GET(DPD_COUNT_FIELD, gpr);
}

static void dpd_count_set(uint16_t count)
{
	VBATCON_WriteGPR(VBAT__VBATCON, DPD_COUNT_GPR,
			 FIELD_PREP(DPD_COUNT_MAGIC_FIELD, DPD_COUNT_MAGIC) |
				 FIELD_PREP(DPD_COUNT_FIELD, count));
}

/* The counter API requires a callback; the alarm only has to end the power down. */
static void wakeup_alarm_handler(const struct device *dev, uint8_t chan_id, uint32_t ticks,
				 void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);
}

static int arm_timer_wakeup(void)
{
	struct counter_alarm_cfg alarm = {
		.callback = wakeup_alarm_handler,
	};
	int ret;

	if (!device_is_ready(wakeup_timer)) {
		return -ENODEV;
	}

	ret = counter_start(wakeup_timer);
	if (ret != 0) {
		return ret;
	}

	/* counter_mcux_lptmr.c enables the VBAT LPTMR wakeup source with the alarm. */
	alarm.ticks = counter_us_to_ticks(wakeup_timer,
					  (uint64_t)CONFIG_SAMPLE_DPD_WAKEUP_SECONDS * USEC_PER_SEC);

	return counter_set_channel_alarm(wakeup_timer, DPD_ALARM_CHANNEL, &alarm);
}

static int arm_button_wakeup(void)
{
	int ret;

	if (!gpio_is_ready_dt(&wakeup_button) || !device_is_ready(wakeup_button_wuc.dev)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&wakeup_button, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&wakeup_button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return ret;
	}

	return wuc_enable_wakeup_source_dt(&wakeup_button_wuc);
}

static int arm_wakeup(void)
{
	if (IS_ENABLED(CONFIG_SAMPLE_DPD_WAKEUP_BUTTON)) {
		return arm_button_wakeup();
	}

	if (IS_ENABLED(CONFIG_SAMPLE_DPD_MODE_DPD1)) {
		/*
		 * With CONFIG_PM the VBAT LPTMR is the system timer's low-power
		 * companion, and the kernel arms its alarm with the sleep timeout.
		 */
		return 0;
	}

	return arm_timer_wakeup();
}

#if defined(CONFIG_SAMPLE_DPD_MODE_DPD1)
/* The SoC maps PM_STATE_SUSPEND_TO_DISK, which has no substates, to DPD1. */
static const struct pm_state_info dpd1_state = {
	.state = PM_STATE_SUSPEND_TO_DISK,
	.substate_id = 0U,
};

static int enter_deep_power_down(void)
{
	if (!pm_state_force(DPD_CPU_ID, &dpd1_state)) {
		return -ENOTSUP;
	}

	/* The idle thread enters the forced state as soon as the main thread sleeps. */
	if (IS_ENABLED(CONFIG_SAMPLE_DPD_WAKEUP_BUTTON)) {
		k_sleep(K_FOREVER);
	} else {
		k_sleep(K_SECONDS(CONFIG_SAMPLE_DPD_WAKEUP_SECONDS));
	}

	return -EIO;
}
#else
static int enter_deep_power_down(void)
{
	sys_poweroff();
}
#endif

int main(void)
{
	bool woke;
	uint16_t count;
	int ret;

	printk("%s deep power down sample (%s, %s wakeup)\n", CONFIG_BOARD, DPD_MODE_NAME,
	       DPD_WAKEUP_NAME);

	ret = read_wake_reason(&woke);
	if (ret != 0) {
		printk("Could not read the reset cause (%d)\n", ret);
		return 0;
	}

	count = woke ? dpd_count_get() : 0U;
	if (woke) {
		printk("Woke from deep power down, cycle %u of %u\n", count,
		       CONFIG_SAMPLE_DPD_CYCLES);
	}

	if (count >= CONFIG_SAMPLE_DPD_CYCLES) {
		printk("Done, staying awake\n");
		return 0;
	}

	ret = arm_wakeup();
	if (ret != 0) {
		printk("Could not arm the %s wakeup (%d)\n", DPD_WAKEUP_NAME, ret);
		return 0;
	}

	count++;
	dpd_count_set(count);
	printk("Entering deep power down, cycle %u of %u\n", count, CONFIG_SAMPLE_DPD_CYCLES);

	ret = enter_deep_power_down();
	printk("ERROR: deep power down returned (%d)\n", ret);

	return 0;
}
