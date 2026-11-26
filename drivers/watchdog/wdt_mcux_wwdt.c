/*
 * Copyright (c) 2020, Jiri Kerestes
 *
 * Based on wdt_mcux_wdog32.c, which is:
 * Copyright (c) 2019 Vestas Wind Systems A/S
 * Copyright 2018, 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lpc_wwdt

#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/mcux_lpc_syscon_clock.h>
#include <zephyr/irq.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>
#include <zephyr/pm/device.h>
#include <fsl_wwdt.h>
#include <fsl_clock.h>

#define LOG_LEVEL CONFIG_WDT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wdt_mcux_wwdt);

#define MIN_TIMEOUT 0xFF
#define MAX_TIMEOUT WWDT_TC_COUNT_MASK

struct mcux_wwdt_config {
	WWDT_Type *base;
	uint8_t clk_divider;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	void (*irq_config_func)(const struct device *dev);
	unsigned int irq;
};

struct mcux_wwdt_data {
	wdt_callback_t callback;
	wwdt_config_t wwdt_config;
	bool timeout_valid;
	bool active_before_sleep;
	bool started;
	bool disabled;
};

#ifdef CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT
static int mcux_wwdt_mcx_divider(const struct device *dev, clock_div_name_t *divider,
			       volatile uint32_t **control)
{
	const struct mcux_wwdt_config *config = dev->config;

	switch ((uint32_t)config->clock_subsys) {
	case MCUX_WWDT0_CLK:
#if defined(CONFIG_SOC_FAMILY_MCXA)
		*divider = kCLOCK_DivWWDT0;
		if (control != NULL) {
			*control = &MRCC0->MRCC_WWDT0_CLKDIV;
		}
#else
		*divider = kCLOCK_DivWdt0Clk;
		if (control != NULL) {
			*control = &SYSCON->WDT0CLKDIV;
		}
#endif
		return 0;
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(wwdt1))
	case MCUX_WWDT1_CLK:
#if defined(CONFIG_SOC_FAMILY_MCXA)
		*divider = kCLOCK_DivWWDT1;
		if (control != NULL) {
			*control = &MRCC0->MRCC_WWDT1_CLKDIV;
		}
#else
		*divider = kCLOCK_DivWdt1Clk;
		if (control != NULL) {
			*control = &SYSCON->WDT1CLKDIV;
		}
#endif
		return 0;
#endif
	default:
		return -ENOTSUP;
	}
}

static void mcux_wwdt_set_clock_divider(clock_div_name_t divider, uint32_t value)
{
#if defined(CONFIG_SOC_FAMILY_MCXA)
	CLOCK_SetClockDiv(divider, value);
#else
	CLOCK_SetClkDiv(divider, value);
#endif
}
#endif

static int mcux_wwdt_clock_halted(const struct device *dev, bool halt)
{
#ifdef CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT
	clock_div_name_t divider;
	int ret = mcux_wwdt_mcx_divider(dev, &divider, NULL);

	if (ret != 0) {
		return ret;
	}
#if defined(CONFIG_SOC_FAMILY_MCXA)
	if (halt) {
		CLOCK_HaltClockDiv(divider);
	}
	return CLOCK_GetClockDiv(divider) == 0U ? 0 : -EIO;
#else
	if (halt) {
		CLOCK_SetClkDiv(divider, 0U);
	}
	return CLOCK_GetClkDiv(divider) == 0U ? 0 : -EIO;
#endif
#else
	ARG_UNUSED(dev);
	ARG_UNUSED(halt);
	return -ENOTSUP;
#endif
}

#ifdef CONFIG_WDT_MCUX_WWDT_RESTART
static int mcux_wwdt_get_restart_clock_frequency(const struct device *dev, uint32_t *freq)
{
	const struct mcux_wwdt_config *config = dev->config;
	uint32_t source;

	if (config->clk_divider == 0U) {
		LOG_ERR("Invalid watchdog clock divider");
		return -EINVAL;
	}
#if defined(CONFIG_SOC_FAMILY_MCXN)
	if (config->clk_divider > SYSCON_WDT0CLKDIV_DIV_MASK + 1U) {
		LOG_ERR("Watchdog clock divider out of range");
		return -EINVAL;
	}
	/* Halting an MCXN divider clears DIV, so its reported rate is the source rate. */
	int ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, &source);

	if (ret != 0) {
		LOG_ERR("Failed to get watchdog source frequency: %d", ret);
		return ret;
	}
#else
	switch ((uint32_t)config->clock_subsys) {
	case MCUX_WWDT0_CLK:
		source = CLOCK_GetFreq(kCLOCK_Clk1M);
		break;
#if defined(CONFIG_SOC_MCXA577)
	case MCUX_WWDT1_CLK: {
		clock_name_t parent;

		switch (MRCC0->MRCC_WWDT1_CLKSEL) {
		case 0U:
			parent = kCLOCK_Clk16K1;
			break;
		case 1U:
			parent = kCLOCK_FroHfDiv;
			break;
		case 2U:
		case 3U:
			parent = kCLOCK_Clk1M;
			break;
		default:
			LOG_ERR("Invalid watchdog clock source");
			return -EINVAL;
		}
		source = CLOCK_GetFreq(parent);
		break;
	}
#endif
	default:
		LOG_ERR("Unsupported watchdog clock");
		return -ENOTSUP;
	}
#endif
	*freq = source / config->clk_divider;
	if (*freq == 0U) {
		LOG_ERR("Watchdog source clock is unavailable");
		return -EIO;
	}
	return 0;
}

static int mcux_wwdt_check_restart(const struct device *dev)
{
	const struct mcux_wwdt_config *config = dev->config;
	struct mcux_wwdt_data *data = dev->data;
	uint32_t mod = config->base->MOD;

	if ((mod & (WWDT_MOD_LOCK_MASK | WWDT_MOD_WDPROTECT_MASK)) != 0U) {
		LOG_ERR("Restart requires an unlocked, unprotected watchdog");
		return -ENOTSUP;
	}
	if ((mod & WWDT_MOD_WDRESET_MASK) != 0U && !data->wwdt_config.enableWatchdogReset) {
		LOG_ERR("Watchdog reset cannot be disabled without a reset");
		return -ENOTSUP;
	}
	return 0;
}

static int mcux_wwdt_restart(const struct device *dev)
{
	const struct mcux_wwdt_config *config = dev->config;
	struct mcux_wwdt_data *data = dev->data;
	WWDT_Type *base = config->base;
	clock_div_name_t divider;
	volatile uint32_t *divider_control;
	uint32_t mod = WWDT_MOD_WDEN_MASK;
	uint32_t delay_us;
	uint32_t first_tv;
	uint32_t second_tv;
	unsigned int key;
	int ret = mcux_wwdt_mcx_divider(dev, &divider, &divider_control);

	if (ret != 0) {
		LOG_ERR("Unsupported watchdog divider: %d", ret);
		return ret;
	}

	key = irq_lock();
	irq_disable(config->irq);
	ret = mcux_wwdt_check_restart(dev);
	if (ret != 0) {
		irq_unlock(key);
		return ret;
	}
	ret = mcux_wwdt_clock_halted(dev, true);
	if (ret != 0) {
		irq_unlock(key);
		LOG_ERR("Cannot halt watchdog before restart: %d", ret);
		return ret;
	}
	if (data->wwdt_config.enableWatchdogReset) {
		mod |= WWDT_MOD_WDRESET_MASK;
	}
	if ((base->MOD & WWDT_MOD_WDINT_MASK) != 0U) {
		mod |= WWDT_MOD_WDINT_MASK;
	}

	/* Feed against an open window before restoring the requested window. */
	base->TC = data->wwdt_config.timeoutValue;
	base->MOD = mod;
	base->WINDOW = MAX_TIMEOUT;
	base->WARNINT = data->wwdt_config.warningValue;
	WWDT_Refresh(base);
	mcux_wwdt_set_clock_divider(divider, config->clk_divider);
	irq_unlock(key);

	/* Synchronization and startup must be waited for after clearing HALT. */
	delay_us = DIV_ROUND_UP(32U * USEC_PER_SEC, data->wwdt_config.clockFreq_Hz) + 1U;
	if (!WAIT_FOR((*divider_control & BIT(31)) == 0U, delay_us, k_busy_wait(1))) {
		LOG_ERR("Watchdog divider clock did not stabilize");
		ret = -ETIMEDOUT;
		goto failed;
	}
	delay_us = DIV_ROUND_UP(4U * USEC_PER_SEC, data->wwdt_config.clockFreq_Hz) + 1U;
	first_tv = base->TV;
	second_tv = first_tv;
	ret = -ETIMEDOUT;
	/* TC and TV can still expose the old period while the clock domains synchronize. */
	for (uint32_t attempt = 0U; attempt < 8U; attempt++) {
		SDK_DelayAtLeastUs(delay_us, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
		second_tv = base->TV;
		if ((base->MOD & WWDT_MOD_WDTOF_MASK) != 0U) {
			ret = -EIO;
			break;
		}
		if ((base->MOD & WWDT_MOD_WDEN_MASK) != 0U && second_tv != 0U &&
		    second_tv < first_tv && first_tv <= data->wwdt_config.timeoutValue) {
			ret = 0;
			break;
		}
		first_tv = second_tv;
	}

	key = irq_lock();
	if (ret != 0) {
		LOG_ERR("Restart failed: MOD=%08x TV=%u -> %u TC=%u clock=%u",
			base->MOD, first_tv, second_tv, data->wwdt_config.timeoutValue,
			data->wwdt_config.clockFreq_Hz);
		irq_unlock(key);
		LOG_ERR("Watchdog did not start counting after restart");
		goto failed;
	}

	base->WINDOW = data->wwdt_config.windowValue;
	if (second_tv > data->wwdt_config.warningValue) {
		WWDT_ClearStatusFlags(base, WWDT_MOD_WDINT_MASK);
		k_irq_clear_pending(config->irq);
	}
	data->disabled = false;
	data->started = true;
	irq_enable(config->irq);
	irq_unlock(key);
	return 0;

failed:
	key = irq_lock();
	int halt_ret = mcux_wwdt_clock_halted(dev, true);

	if (halt_ret != 0) {
		LOG_ERR("Failed to halt watchdog after restart failure: %d", halt_ret);
	}
	irq_unlock(key);
	return ret;
}
#endif

static inline int mcux_wwdt_get_clock_frequency(const struct device *dev, uint32_t *freq)
{
	const struct mcux_wwdt_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

#ifdef CONFIG_WDT_MCUX_WWDT_RESTART
	struct mcux_wwdt_data *data = dev->data;

	if (data->disabled) {
		return mcux_wwdt_get_restart_clock_frequency(dev, freq);
	}
#endif

#ifdef CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT
	clock_div_name_t divider;

	ret = mcux_wwdt_mcx_divider(dev, &divider, NULL);
	if (ret != 0) {
		LOG_ERR("Unsupported watchdog divider: %d", ret);
		return ret;
	}
	mcux_wwdt_set_clock_divider(divider, config->clk_divider);
#else
	switch ((uint32_t)config->clock_subsys) {
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(wwdt0)) || DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(wwdt))
	case MCUX_WWDT0_CLK:
#if defined(CONFIG_SOC_SERIES_MCXW2XX) || defined(CONFIG_SOC_SERIES_LPC55XXX) ||                   \
	defined(CONFIG_SOC_SERIES_LPC51U68) || defined(CONFIG_SOC_SERIES_LPC11U6X)
		CLOCK_SetClkDiv(kCLOCK_DivWdtClk, config->clk_divider, true);
#elif defined(CONFIG_SOC_FAMILY_MCXL)
		CLOCK_SetClockDiv(kCLOCK_DivWWDT0, config->clk_divider);
#endif
		break;
#endif
	default:
		break;
	}
#endif

	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, freq);
	if (ret) {
		LOG_ERR("Failed to get clock frequency: %d", ret);
		return ret;
	}

	return 0;
}

static int mcux_wwdt_setup(const struct device *dev, uint8_t options)
{
	const struct mcux_wwdt_config *config = dev->config;
	struct mcux_wwdt_data *data = dev->data;
	WWDT_Type *base = config->base;

	if (data->started) {
		LOG_ERR("Cannot set up watchdog: already started");
		return -EBUSY;
	}

	if (IS_ENABLED(CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT) &&
	    !IS_ENABLED(CONFIG_WDT_MCUX_WWDT_RESTART) && data->disabled) {
		LOG_ERR("Cannot restart a disabled watchdog");
		return -ENOTSUP;
	}

	if (!data->timeout_valid) {
		LOG_ERR("No valid timeouts installed");
		return -EINVAL;
	}

#ifdef CONFIG_WDT_MCUX_WWDT_RESTART
	if (data->disabled) {
		int ret = mcux_wwdt_restart(dev);

		if (ret != 0) {
			return ret;
		}
		LOG_DBG("Restarted the watchdog");
		return 0;
	}
#endif

	WWDT_Init(base, &data->wwdt_config);
	data->started = true;
	LOG_DBG("Setup the watchdog");
	return 0;
}

static int mcux_wwdt_disable(const struct device *dev)
{
	const struct mcux_wwdt_config *config = dev->config;
	struct mcux_wwdt_data *data = dev->data;
	WWDT_Type *base = config->base;

	if (!data->started) {
		LOG_ERR("Cannot disable watchdog: not started");
		return -EFAULT;
	}

	if (IS_ENABLED(CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT)) {
		unsigned int key = irq_lock();
		bool irq_enabled = irq_is_enabled(config->irq) != 0;

		irq_disable(config->irq);
		int ret = mcux_wwdt_clock_halted(dev, true);

		if (ret != 0) {
			if (irq_enabled) {
				irq_enable(config->irq);
			}
			irq_unlock(key);
			LOG_ERR("Cannot halt watchdog: %d", ret);
			return ret;
		}
		data->disabled = true;
		irq_unlock(key);
	} else {
		WWDT_Deinit(base);
	}
	data->timeout_valid = false;
	data->callback = NULL;
	data->active_before_sleep = false;
	data->started = false;
	LOG_DBG("Disabled the watchdog");

	return 0;
}

/*
 * LPC55xxx WWDT has a fixed divide-by-4 clock prescaler.
 * This prescaler is different from the clock divider specified in Device Tree.
 */
#define MSEC_TO_WWDT_TICKS(clock_freq, msec) \
	((uint32_t)((clock_freq / MSEC_PER_SEC) * msec) / 4)

static int mcux_wwdt_install_timeout(const struct device *dev,
				     const struct wdt_timeout_cfg *cfg)
{
	struct mcux_wwdt_data *data = dev->data;
	uint32_t clock_freq;
	int ret;

	if (IS_ENABLED(CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT) &&
	    !IS_ENABLED(CONFIG_WDT_MCUX_WWDT_RESTART) && data->disabled) {
		LOG_ERR("Cannot restart a disabled watchdog");
		return -ENOTSUP;
	}

	if (data->timeout_valid) {
		LOG_ERR("No more timeouts can be installed");
		return -ENOMEM;
	}

	/*
	 * The window value is derived by subtracting the lower window bound from
	 * the timeout, so reject an inverted window before it underflows.
	 */
	if (cfg->window.min > cfg->window.max) {
		LOG_ERR("Invalid window: min %u is above max %u", cfg->window.min,
			cfg->window.max);
		return -EINVAL;
	}

	ret = mcux_wwdt_get_clock_frequency(dev, &clock_freq);
	if (ret) {
		return ret;
	}

	WWDT_GetDefaultConfig(&data->wwdt_config);

	data->wwdt_config.clockFreq_Hz = clock_freq;

	data->wwdt_config.timeoutValue =
		MSEC_TO_WWDT_TICKS(clock_freq, cfg->window.max);

	if (data->wwdt_config.timeoutValue > MAX_TIMEOUT ||
	    data->wwdt_config.timeoutValue < MIN_TIMEOUT) {
		LOG_ERR("Timeout value %d out of range %d - %d", data->wwdt_config.timeoutValue,
			MIN_TIMEOUT, MAX_TIMEOUT);
		return -EINVAL;
	}

	/*
	 * WWDT uses a count-down counter.
	 * Window value specifies the highest timer value in which a watchdog
	 * feed can occur. Therefore, calculate the window value as:
	 * windowValue = timeoutValue - min_window_ticks (maybe 0)
	 */
	data->wwdt_config.windowValue =
		data->wwdt_config.timeoutValue - MSEC_TO_WWDT_TICKS(clock_freq, cfg->window.min);

	if (cfg->flags & WDT_FLAG_RESET_SOC) {
		data->wwdt_config.enableWatchdogReset = true;
		LOG_DBG("Enabling SoC reset");
	}

	/*
	 * A non-zero warning configuration triggers the callback before expiry.
	 * For WDT_FLAG_RESET_NONE, leaving warningValue at 0 preserves the
	 * callback-at-expiry behavior used by callback-only flows.
	 * Other reset modes still require an early warning callback.
	 */
	if (cfg->callback != NULL) {
		if (CONFIG_WDT_MCUX_WWDT_WARNING_INTERRUPT_CFG > 0) {
			data->wwdt_config.warningValue =
				CONFIG_WDT_MCUX_WWDT_WARNING_INTERRUPT_CFG;
		} else if ((cfg->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_NONE) {
			LOG_ERR("Callback without warning requires WDT_FLAG_RESET_NONE or "
				"CONFIG_WDT_MCUX_WWDT_WARNING_INTERRUPT_CFG > 0");
			return -ENOTSUP;
		}
	}

#ifdef CONFIG_WDT_MCUX_WWDT_RESTART
	if (data->disabled) {
		ret = mcux_wwdt_check_restart(dev);
		if (ret != 0) {
			return ret;
		}
	}
#endif

	data->callback = cfg->callback;

	data->timeout_valid = true;
	LOG_DBG("Installed timeout (timeoutValue = %d)",
		data->wwdt_config.timeoutValue);

	return 0;
}

static int mcux_wwdt_feed(const struct device *dev, int channel_id)
{
	const struct mcux_wwdt_config *config = dev->config;
	WWDT_Type *base = config->base;

	if (channel_id != 0) {
		LOG_ERR("Invalid channel id");
		return -EINVAL;
	}

	WWDT_Refresh(base);
	LOG_DBG("Fed the watchdog");

	return 0;
}

static void mcux_wwdt_isr(const struct device *dev)
{
	const struct mcux_wwdt_config *config = dev->config;
	struct mcux_wwdt_data *data = dev->data;
	WWDT_Type *base = config->base;
	uint32_t flags;

	flags = WWDT_GetStatusFlags(base);
	WWDT_ClearStatusFlags(base, flags);

	if (!data->started) {
		return;
	}

	if (data->callback) {
		data->callback(dev, 0);
	}
}

/*
 * Power Management:
 * When the system enters sleep mode, this driver maintains awareness
 * of whether the WWDT was active. After wake up, the WDT counter
 * resets to its reload value.
 */
static int mcux_wwdt_driver_pm_action(const struct device *dev,
				      enum pm_device_action action)
{
	const struct mcux_wwdt_config *config = dev->config;
	struct mcux_wwdt_data *data = dev->data;
	int err = 0;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		if (data->active_before_sleep) {
			err = mcux_wwdt_setup(dev, 0);
			data->active_before_sleep = false;
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		data->started = false;
		if (IS_ENABLED(CONFIG_WDT_MCUX_WWDT_HAS_CLOCK_HALT) && data->disabled) {
			data->active_before_sleep = false;
			break;
		}
		if (config->base->MOD & WWDT_MOD_WDEN_MASK) {
			data->active_before_sleep = true;
		}
		break;
	default:
		err = -ENOTSUP;
	}

	return err;
}

static int mcux_wwdt_init(const struct device *dev)
{
	const struct mcux_wwdt_config *config = dev->config;
	int ret;

	ret = clock_control_configure(config->clock_dev, config->clock_subsys, NULL);
	if (ret && ret != -ENOSYS) {
		/* Real error occurred */
		LOG_ERR("Failed to configure clock: %d", ret);
		return ret;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret) {
		LOG_ERR("Failed to enable clock: %d", ret);
		return ret;
	}

	/* The rest of the device init is done from the
	 * PM_DEVICE_ACTION_TURN_ON in the pm callback
	 * which is invoked by pm_device_driver_init.
	 */
	config->irq_config_func(dev);
	return pm_device_driver_init(dev, mcux_wwdt_driver_pm_action);
}

static DEVICE_API(wdt, mcux_wwdt_api) = {
	.setup = mcux_wwdt_setup,
	.disable = mcux_wwdt_disable,
	.install_timeout = mcux_wwdt_install_timeout,
	.feed = mcux_wwdt_feed,
};

#define MCUX_WWDT_INIT_CONFIG(id)                                                                  \
	static void mcux_wwdt_config_func_##id(const struct device *dev);                          \
                                                                                                   \
	static const struct mcux_wwdt_config mcux_wwdt_config_##id = {                             \
		.base = (WWDT_Type *)DT_INST_REG_ADDR(id),                                         \
		.clk_divider = DT_INST_PROP(id, clk_divider),                                      \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(id)),                               \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(id, name),             \
		.irq_config_func = mcux_wwdt_config_func_##id,                                     \
		.irq = DT_INST_IRQN(id),                                                           \
	};                                                                                         \
                                                                                                   \
	static struct mcux_wwdt_data mcux_wwdt_data_##id;                                          \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(id, mcux_wwdt_driver_pm_action);                                  \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(id, &mcux_wwdt_init, PM_DEVICE_DT_INST_GET(id),                      \
			      &mcux_wwdt_data_##id, &mcux_wwdt_config_##id, POST_KERNEL,           \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mcux_wwdt_api);                 \
                                                                                                   \
	static void mcux_wwdt_config_func_##id(const struct device *dev)                           \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(id), DT_INST_IRQ(id, priority), mcux_wwdt_isr,            \
			    DEVICE_DT_INST_GET(id), 0);                                            \
		/* Defensive: clear any peripheral status and NVIC pending */                      \
		WWDT_ClearStatusFlags((WWDT_Type *)DT_INST_REG_ADDR(id),                           \
			      WWDT_GetStatusFlags((WWDT_Type *)DT_INST_REG_ADDR(id)));             \
		k_irq_clear_pending(DT_INST_IRQN(id));                                            \
		irq_enable(DT_INST_IRQN(id));                                                      \
	}

DT_INST_FOREACH_STATUS_OKAY(MCUX_WWDT_INIT_CONFIG)
