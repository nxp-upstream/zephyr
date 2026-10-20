/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#define CONFIG_TASK_WDT_HW_FALLBACK       1
#define CONFIG_TASK_WDT_CHANNELS          2
#define CONFIG_TASK_WDT_MIN_TIMEOUT       10000
#define CONFIG_TASK_WDT_HW_FALLBACK_DELAY 20

#include "task_wdt.c"

static int install_result;
static int setup_result;
static int resume_result;
static int suspend_result;
static uint32_t install_count;
static uint32_t setup_count;
static uint32_t resume_count;

static int backend_setup(const struct device *dev, uint8_t options)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(options);
	setup_count++;
	return setup_result;
}

static int backend_install(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	ARG_UNUSED(dev);
	zassert_equal(cfg->flags, WDT_FLAG_RESET_SOC);
	install_count++;
	return install_result;
}

static int backend_feed(const struct device *dev, int channel)
{
	ARG_UNUSED(dev);
	zassert_equal(channel, 0);
	return 0;
}

static int backend_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -EPERM;
}

static int backend_pm(const struct device *dev, enum pm_device_action action)
{
	ARG_UNUSED(dev);
	if (action == PM_DEVICE_ACTION_RESUME) {
		resume_count++;
		return resume_result;
	}
	if (action == PM_DEVICE_ACTION_SUSPEND) {
		return suspend_result;
	}
	return 0;
}

static DEVICE_API(wdt, backend_api) = {
	.setup = backend_setup,
	.install_timeout = backend_install,
	.feed = backend_feed,
	.disable = backend_disable,
};
PM_DEVICE_DEFINE(backend_pm_data, backend_pm);
DEVICE_DEFINE(backend, "backend", NULL, PM_DEVICE_GET(backend_pm_data), NULL, NULL, POST_KERNEL, 80,
	      &backend_api);

static const struct device *const backend_dev = DEVICE_GET(backend);

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	resume_result = 0;
	suspend_result = 0;
	if (task_wdt_initialized) {
		k_timer_stop(&timer);
	}
	if (hw_wdt_ref_dev != NULL) {
		zassert_ok(pm_device_runtime_put(hw_wdt_ref_dev));
	}
	hw_wdt_dev = NULL;
	hw_wdt_ref_dev = NULL;
	hw_wdt_started = false;
	task_wdt_initialized = false;
	memset(channels, 0, sizeof(channels));
	zassert_ok(pm_device_runtime_disable(backend_dev));
	zassert_ok(pm_device_runtime_enable(backend_dev));
	install_result = 0;
	setup_result = 0;
	install_count = 0U;
	setup_count = 0U;
	resume_count = 0U;
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (task_wdt_initialized) {
		k_timer_stop(&timer);
	}
}

ZTEST(task_wdt_pm, test_setup_failure_rolls_back_channel)
{
	zassert_ok(task_wdt_init(backend_dev));
	setup_result = -ENOTSUP;
	zassert_equal(task_wdt_add(10000U, NULL, NULL), -ENOTSUP);
	zassert_false(hw_wdt_started);
	zassert_equal(channels[0].reload_period, 0U);
	zassert_equal(pm_device_runtime_usage(backend_dev), 1);
	setup_result = 0;
	zassert_equal(task_wdt_add(10000U, NULL, NULL), 0);
	zassert_true(hw_wdt_started);
	zassert_equal(setup_count, 2U);
	zassert_equal(resume_count, 1U);
}

ZTEST(task_wdt_pm, test_install_failure_releases_reference)
{
	install_result = -ENOMEM;
	zassert_equal(task_wdt_init(backend_dev), -ENOMEM);
	zassert_false(task_wdt_initialized);
	zassert_is_null(hw_wdt_ref_dev);
	zassert_is_null(hw_wdt_dev);
	zassert_equal(pm_device_runtime_usage(backend_dev), 0);
	install_result = 0;
	zassert_ok(task_wdt_init(backend_dev));
	zassert_equal(install_count, 2U);
	zassert_equal(pm_device_runtime_usage(backend_dev), 1);
}

ZTEST(task_wdt_pm, test_failed_cleanup_does_not_duplicate_reference)
{
	install_result = -ENOMEM;
	suspend_result = -EIO;
	zassert_equal(task_wdt_init(backend_dev), -ENOMEM);
	zassert_equal(hw_wdt_ref_dev, backend_dev);
	zassert_equal(pm_device_runtime_usage(backend_dev), 1);
	zassert_equal(task_wdt_init(NULL), -EBUSY);
	install_result = 0;
	suspend_result = 0;
	zassert_ok(task_wdt_init(backend_dev));
	zassert_equal(pm_device_runtime_usage(backend_dev), 1);
	zassert_equal(resume_count, 1U);
}

ZTEST(task_wdt_pm, test_repeated_init_keeps_one_reference)
{
	zassert_ok(task_wdt_init(backend_dev));
	zassert_equal(task_wdt_init(backend_dev), -EALREADY);
	zassert_equal(pm_device_runtime_usage(backend_dev), 1);
	zassert_equal(install_count, 1U);
	zassert_equal(resume_count, 1U);
}

ZTEST(task_wdt_pm, test_resume_failure_is_propagated)
{
	resume_result = -EIO;
	zassert_equal(task_wdt_init(backend_dev), -EIO);
	zassert_false(task_wdt_initialized);
	zassert_is_null(hw_wdt_ref_dev);
	zassert_equal(install_count, 0U);
	zassert_equal(pm_device_runtime_usage(backend_dev), 0);
	resume_result = 0;
}

ZTEST(task_wdt_pm, test_software_only_still_works)
{
	zassert_ok(task_wdt_init(NULL));
	zassert_equal(task_wdt_add(10000U, NULL, NULL), 0);
	zassert_equal(task_wdt_add(10000U, NULL, NULL), 1);
	zassert_equal(task_wdt_add(10000U, NULL, NULL), -ENOMEM);
	zassert_equal(install_count, 0U);
	zassert_equal(setup_count, 0U);
}

ZTEST_SUITE(task_wdt_pm, NULL, NULL, before, after, NULL);
