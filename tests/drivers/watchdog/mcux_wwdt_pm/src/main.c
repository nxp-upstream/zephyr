/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/pm.h>
#include <zephyr/ztest.h>
#include "device_system_managed.h"

/* No production DT instances: the test devices below use RAM register blocks. */
#include "wdt_mcux_wwdt.c"

static uint32_t dividers[2] = {1U, 1U};
static uint32_t divider_writes;
static uint32_t init_calls;
static uint32_t feed_calls;
static uint32_t deinit_calls;
static uint32_t callbacks;
static bool gates[2];
static int on_error;
static int off_error;
static void (*init_hook)(void);

static int clock_index(clock_control_subsys_t subsys)
{
	if ((uint32_t)subsys == MCUX_WWDT0_CLK) {
		return 0;
	}
	if ((uint32_t)subsys == MCUX_WWDT1_CLK) {
		return 1;
	}
	return -EINVAL;
}

static int clock_on(const struct device *dev, clock_control_subsys_t subsys)
{
	int index = clock_index(subsys);

	ARG_UNUSED(dev);
	if (on_error != 0) {
		return on_error;
	}
	if (index < 0) {
		return index;
	}
	gates[index] = true;
	return 0;
}

static int clock_off(const struct device *dev, clock_control_subsys_t subsys)
{
	int index = clock_index(subsys);

	ARG_UNUSED(dev);
	if (off_error != 0) {
		return off_error;
	}
	if (index < 0) {
		return index;
	}
	gates[index] = false;
	return 0;
}

static int clock_rate(const struct device *dev, clock_control_subsys_t subsys, uint32_t *rate)
{
	int index = clock_index(subsys);

	ARG_UNUSED(dev);
	if (index < 0) {
		return index;
	}
	if (dividers[index] == 0U) {
		return -EINVAL;
	}
	*rate = 1000000U / dividers[index];
	return 0;
}

static DEVICE_API(clock_control, clock_api) = {
	.on = clock_on,
	.off = clock_off,
	.get_rate = clock_rate,
};

DEVICE_DEFINE(test_clock, "test_clock", NULL, NULL, NULL, NULL, POST_KERNEL, 60, &clock_api);

void CLOCK_SetClkDiv(enum clock_div_name divider, uint32_t value)
{
	dividers[divider] = value;
	divider_writes++;
}

void WWDT_GetDefaultConfig(wwdt_config_t *config)
{
	*config = (wwdt_config_t){
		.enableWwdt = true,
		.timeoutValue = 0xFFFFFFU,
		.windowValue = 0xFFFFFFU,
	};
}

void WWDT_Init(WWDT_Type *base, const wwdt_config_t *config)
{
	init_calls++;
	if (init_hook != NULL) {
		init_hook();
	}
	base->MOD = WWDT_MOD_WDEN_MASK | (config->enableWatchdogReset ? WWDT_MOD_WDRESET_MASK : 0U);
	base->TC = config->timeoutValue;
	base->WINDOW = config->windowValue;
	base->WARNINT = config->warningValue;
	base->TV = base->TC;
}

void WWDT_Deinit(WWDT_Type *base)
{
	ARG_UNUSED(base);
	deinit_calls++;
}

void WWDT_Refresh(WWDT_Type *base)
{
	feed_calls++;
	base->TV = base->TC;
}

uint32_t WWDT_GetStatusFlags(WWDT_Type *base)
{
	return base->MOD & (WWDT_MOD_WDTOF_MASK | WWDT_MOD_WDINT_MASK);
}

void WWDT_ClearStatusFlags(WWDT_Type *base, uint32_t flags)
{
	base->MOD &= ~flags;
}

static void test_irq_config(const struct device *dev)
{
	const struct mcux_wwdt_config *config = dev->config;

	irq_disable(config->irq);
}

#define TEST_WWDT(n, clock_id)                                                                     \
	static WWDT_Type registers_##n;                                                            \
	static struct mcux_wwdt_data data_##n;                                                     \
	static const struct mcux_wwdt_config config_##n = {                                        \
		.base = &registers_##n,                                                            \
		.clk_divider = 4U,                                                                 \
		.clock_dev = DEVICE_GET(test_clock),                                               \
		.clock_subsys = (clock_control_subsys_t)clock_id,                                  \
		.irq_config_func = test_irq_config,                                                \
		.irq = 40U + n,                                                                    \
	};                                                                                         \
	PM_DEVICE_DT_DEFINE(DT_NODELABEL(wwdt##n), mcux_wwdt_driver_pm_action);                    \
	DEVICE_DT_DEFINE(DT_NODELABEL(wwdt##n), mcux_wwdt_init,                                    \
			 PM_DEVICE_DT_GET(DT_NODELABEL(wwdt##n)), &data_##n, &config_##n,          \
			 POST_KERNEL, 80, &mcux_wwdt_api)

TEST_WWDT(0, MCUX_WWDT0_CLK);
TEST_WWDT(1, MCUX_WWDT1_CLK);

static const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(wwdt0));
static const struct device *const other = DEVICE_DT_GET(DT_NODELABEL(wwdt1));

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
}

static void callback(const struct device *wdt, int channel)
{
	zassert_equal(wdt, dev);
	zassert_equal(channel, 0);
	callbacks++;
}

static const struct wdt_timeout_cfg timeout = {
	.window = {.min = 200U, .max = 1000U},
	.flags = WDT_FLAG_RESET_NONE,
	.callback = callback,
};

static void assert_state(const struct device *wdt, enum pm_device_state expected)
{
	enum pm_device_state state;

	zassert_ok(pm_device_state_get(wdt, &state));
	zassert_equal(state, expected);
}

static void assert_constraints(bool held)
{
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE, 0U), held);
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_STANDBY, 0U), held);
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_RAM, 0U), held);
	zassert_false(pm_policy_state_lock_is_active(PM_STATE_RUNTIME_IDLE, 0U));
}

static void reset_instance(const struct device *wdt)
{
	struct mcux_wwdt_data *data = wdt->data;
	const struct mcux_wwdt_config *config = wdt->config;

	/* Model a cold reset, not a supported software stop of a running WWDT. */
	data->started = false;
	data->starting = false;
	data->faulted = false;
	if (data->power_locked) {
		pm_policy_device_power_lock_put(wdt);
		data->power_locked = false;
	}
	zassert_ok(pm_device_runtime_disable(wdt));
	memset(data, 0, sizeof(*data));
	*config->base = (WWDT_Type){.TC = 0xFFU, .TV = 0xFFU, .WINDOW = 0xFFFFFFU};
	wdt->pm_base->state = PM_DEVICE_STATE_ACTIVE;
	atomic_clear_bit(&wdt->pm_base->flags, PM_DEVICE_FLAG_TURN_ON_FAILED);
	zassert_ok(pm_device_action_run(wdt, PM_DEVICE_ACTION_SUSPEND));
	zassert_ok(pm_device_action_run(wdt, PM_DEVICE_ACTION_RESUME));
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	on_error = 0;
	off_error = 0;
	init_hook = NULL;
	reset_instance(dev);
	reset_instance(other);
	assert_constraints(false);
	init_calls = 0U;
	feed_calls = 0U;
	deinit_calls = 0U;
	divider_writes = 0U;
	callbacks = 0U;
}

static void start(const struct device *wdt)
{
	zassert_equal(wdt_install_timeout(wdt, &timeout), 0);
	zassert_ok(wdt_setup(wdt, 0));
}

ZTEST(wwdt_pm, test_options_and_error_precedence)
{
	const uint8_t options[] = {
		WDT_OPT_PAUSE_IN_SLEEP,
		WDT_OPT_PAUSE_HALTED_BY_DBG,
		BIT(7),
	};
	WWDT_Type initial = registers_0;

	zassert_equal(wdt_setup(dev, WDT_OPT_PAUSE_IN_SLEEP), -EINVAL);
	zassert_equal(wdt_install_timeout(dev, &timeout), 0);
	for (size_t i = 0U; i < ARRAY_SIZE(options); i++) {
		zassert_equal(wdt_setup(dev, options[i]), -ENOTSUP);
	}
	zassert_mem_equal(&registers_0, &initial, sizeof(initial));
	zassert_equal(init_calls, 0U);
	assert_constraints(false);
	zassert_ok(wdt_setup(dev, 0));
}

ZTEST(wwdt_pm, test_setup_owns_exactly_one_constraint)
{
	start(dev);
	assert_constraints(true);
	zassert_equal(init_calls, 1U);
	zassert_equal(registers_0.TC, 62500U);
	zassert_equal(registers_0.WINDOW, 50000U);
	registers_0.TV = 10000U;
	uint32_t writes = divider_writes;

	zassert_equal(wdt_setup(dev, 0), -EBUSY);
	zassert_equal(wdt_install_timeout(dev, &timeout), -EBUSY);
	zassert_equal(registers_0.TV, 10000U);
	zassert_equal(divider_writes, writes);
	zassert_equal(init_calls, 1U);
	zassert_ok(wdt_feed(dev, 0));
	zassert_equal(feed_calls, 1U);
	zassert_equal(registers_0.TV, registers_0.TC);
	reset_instance(dev);
	assert_constraints(false);
}

ZTEST(wwdt_pm, test_policy_excludes_only_incompatible_states)
{
	const struct pm_state_info *next = pm_policy_next_state(0U, K_TICKS_FOREVER);

	zassert_not_null(next);
	zassert_equal(next->state, PM_STATE_SUSPEND_TO_RAM);
	start(dev);
	next = pm_policy_next_state(0U, K_TICKS_FOREVER);
	zassert_not_null(next);
	zassert_equal(next->state, PM_STATE_RUNTIME_IDLE);
}

ZTEST(wwdt_pm, test_running_disable_and_suspend_preserve_state)
{
	start(dev);
	registers_0.TV = 10000U;
	zassert_equal(wdt_disable(dev), -EPERM);
	zassert_equal(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND), -ENOTSUP);
	assert_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_true(gates[0]);
	zassert_true(data_0.timeout_valid);
	zassert_equal(data_0.callback, callback);
	zassert_equal(registers_0.TV, 10000U);
	zassert_equal(deinit_calls, 0U);
	assert_constraints(true);
}

ZTEST(wwdt_pm, test_runtime_failed_put_keeps_ownership)
{
	zassert_ok(pm_device_runtime_enable(dev));
	assert_state(dev, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(gates[0]);
	zassert_equal(wdt_install_timeout(dev, &timeout), 0);
	zassert_equal(wdt_setup(dev, 0), -EBUSY);
	zassert_equal(init_calls, 0U);
	zassert_ok(pm_device_runtime_get(dev));
	zassert_ok(wdt_setup(dev, 0));
	zassert_equal(pm_device_runtime_put(dev), -ENOTSUP);
	assert_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_equal(pm_device_runtime_usage(dev), 1);
	zassert_ok(wdt_feed(dev, 0));
	assert_constraints(true);
}

ZTEST(wwdt_pm, test_rejected_option_can_release_runtime_reference)
{
	zassert_ok(pm_device_runtime_enable(dev));
	zassert_ok(pm_device_runtime_get(dev));
	zassert_equal(wdt_install_timeout(dev, &timeout), 0);
	zassert_equal(wdt_setup(dev, WDT_OPT_PAUSE_IN_SLEEP), -ENOTSUP);
	zassert_ok(pm_device_runtime_put(dev));
	assert_state(dev, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(gates[0]);
	assert_constraints(false);
}

ZTEST(wwdt_pm, test_idle_feed_never_touches_hardware)
{
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(wdt_feed(dev, 0), -EINVAL);
	zassert_equal(wdt_feed(dev, 1), -EINVAL);
	zassert_equal(wdt_disable(dev), -EFAULT);
	zassert_false(gates[0]);
	zassert_equal(feed_calls, 0U);
	zassert_equal(deinit_calls, 0U);
}

ZTEST(wwdt_pm, test_idle_power_cycle_preserves_installed_request)
{
	zassert_equal(wdt_install_timeout(dev, &timeout), 0);
	wwdt_config_t saved = data_0.wwdt_config;

	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF));
	registers_0 = (WWDT_Type){.TC = 0xFFU, .TV = 0xFFU};
	dividers[0] = 1U;
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
	assert_state(dev, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(gates[0]);
	zassert_equal(registers_0.MOD, 0U);
	zassert_equal(init_calls, 0U);
	zassert_equal(dividers[0], 4U);
	zassert_mem_equal(&data_0.wwdt_config, &saved, sizeof(saved));
	zassert_equal(data_0.callback, callback);
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
	zassert_ok(wdt_setup(dev, 0));
	zassert_equal(registers_0.TC, saved.timeoutValue);
}

ZTEST(wwdt_pm, test_resume_and_suspend_clock_errors)
{
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	on_error = -EIO;
	zassert_equal(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME), -EIO);
	assert_state(dev, PM_DEVICE_STATE_SUSPENDED);
	zassert_false(gates[0]);
	on_error = 0;
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
	off_error = -EIO;
	zassert_equal(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND), -EIO);
	assert_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_true(gates[0]);
	zassert_true(irq_is_enabled(config_0.irq));
	off_error = 0;
}

ZTEST(wwdt_pm, test_instances_own_overlapping_locks_independently)
{
	start(dev);
	start(other);
	zassert_equal(init_calls, 2U);
	zassert_equal(wdt_disable(dev), -EPERM);
	reset_instance(dev);
	assert_constraints(true);
	zassert_true(data_1.started);
	reset_instance(other);
	assert_constraints(false);
}

static void check_start_in_progress(void)
{
	zassert_false(irq_is_enabled(config_0.irq));
	assert_constraints(true);
	zassert_equal(wdt_setup(dev, 0), -EBUSY);
	zassert_equal(wdt_install_timeout(dev, &timeout), -EBUSY);
	zassert_equal(wdt_feed(dev, 0), -EAGAIN);
	zassert_equal(wdt_disable(dev), -EBUSY);
	zassert_equal(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND), -EBUSY);
	zassert_true(gates[0]);
}

ZTEST(wwdt_pm, test_start_serializes_control_and_pm)
{
	init_hook = check_start_in_progress;
	start(dev);
	zassert_true(irq_is_enabled(config_0.irq));
	zassert_false(data_0.starting);
	zassert_equal(feed_calls, 0U);
}

ZTEST(wwdt_pm, test_isr_preserves_callback_without_implicit_feed)
{
	start(dev);
	registers_0.MOD |= WWDT_MOD_WDINT_MASK;
	mcux_wwdt_isr(dev);
	zassert_equal(callbacks, 1U);
	zassert_equal(feed_calls, 0U);
	zassert_equal(registers_0.MOD & WWDT_MOD_WDINT_MASK, 0U);
	zassert_equal(data_0.callback, callback);
}

ZTEST(wwdt_pm, test_state_loss_is_fault_not_restart)
{
	start(dev);
	/* Inject an unsupported physical loss while the software ownership survives. */
	registers_0.MOD = 0U;
	dev->pm_base->state = PM_DEVICE_STATE_OFF;
	zassert_equal(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON), -EIO);
	zassert_equal(init_calls, 1U);
	zassert_equal(wdt_feed(dev, 0), -EIO);
	zassert_equal(wdt_setup(dev, 0), -EIO);
	zassert_equal(wdt_disable(dev), -EIO);
	zassert_false(gates[0]);
	assert_constraints(true);
}

ZTEST(wwdt_pm, test_bootloader_owned_watchdog_is_not_gated)
{
	registers_0.MOD = WWDT_MOD_WDEN_MASK;
	zassert_equal(mcux_wwdt_init(dev), -EIO);
	zassert_true(gates[0]);
	zassert_true(data_0.faulted);
	zassert_equal(init_calls, 0U);
	assert_constraints(false);
}

ZTEST(wwdt_pm, test_system_sweep_skips_running_watchdog)
{
	start(dev);
	zassert_true(pm_suspend_devices());
	assert_state(dev, PM_DEVICE_STATE_ACTIVE);
	zassert_true(gates[0]);
	pm_resume_devices();
	zassert_equal(init_calls, 1U);
	zassert_equal(feed_calls, 0U);
	assert_constraints(true);
}

ZTEST_SUITE(wwdt_pm, NULL, NULL, before, NULL, NULL);
