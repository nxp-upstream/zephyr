/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cache.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <fsl_wwdt.h>

#define WDT_NODE DT_ALIAS(watchdog0)
#define RESTART_MAGIC 0x52535457U
#define RESTART_IDLE 0U
#define RESTART_ACTIVE 1U
#define RESTART_EXPECT_RESET 2U

static const struct device *const wdt = DEVICE_DT_GET(WDT_NODE);
static volatile uint32_t restart_magic
	__attribute__((section(CONFIG_TEST_WDT_NOINIT_SECTION)));
static volatile uint32_t restart_stage
	__attribute__((section(CONFIG_TEST_WDT_NOINIT_SECTION)));
static volatile uint32_t callback_seen
	__attribute__((section(CONFIG_TEST_WDT_NOINIT_SECTION)));
static volatile int callback_disable_result;

static void commit_state(void)
{
	sys_cache_data_flush_range((void *)(uintptr_t)&restart_magic, sizeof(restart_magic));
	sys_cache_data_flush_range((void *)(uintptr_t)&restart_stage, sizeof(restart_stage));
	sys_cache_data_flush_range((void *)(uintptr_t)&callback_seen, sizeof(callback_seen));
}

static void disable_callback(const struct device *dev, int channel)
{
	ARG_UNUSED(channel);
	callback_disable_result = wdt_disable(dev);
}

static void mark_callback(const struct device *dev, int channel)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	callback_seen = 1U;
	commit_state();
}

static int install_and_setup(const struct wdt_timeout_cfg *cfg, uint32_t delay_ms)
{
	int channel = wdt_install_timeout(wdt, cfg);

	zassert_true(channel >= 0, "Timeout installation failed: %d", channel);
	k_sleep(K_MSEC(delay_ms));
	zassert_ok(wdt_setup(wdt, 0), "Watchdog setup failed");
	return channel;
}

static void restart_after(void *fixture)
{
	ARG_UNUSED(fixture);

	if (device_is_ready(wdt)) {
		int ret = wdt_disable(wdt);

		zassert_true(ret == 0 || ret == -EFAULT, "Watchdog cleanup failed: %d", ret);
	}
	restart_magic = 0U;
	restart_stage = RESTART_IDLE;
	callback_seen = 0U;
	commit_state();
}

ZTEST(wdt_basic_test_suite, test_wdt)
{
	struct wdt_timeout_cfg cfg = {
		.window = {.min = 0U, .max = 200U},
		.flags = WDT_FLAG_RESET_SOC,
	};
	WWDT_Type *base = (WWDT_Type *)DT_REG_ADDR(WDT_NODE);
	uint32_t stopped_tv;
	int channel;

	zassert_true(device_is_ready(wdt), "Watchdog is not ready");
	if (restart_magic != RESTART_MAGIC) {
		restart_magic = RESTART_MAGIC;
		restart_stage = RESTART_IDLE;
		callback_seen = 0U;
	}
	if (restart_stage == RESTART_EXPECT_RESET) {
		zassert_equal(callback_seen, 1U, "Reinstalled callback was not delivered");
		return;
	}
	/* Any reset before the final expiry is a failure, not a successful restart. */
	zassert_equal(restart_stage, RESTART_IDLE, "Unexpected reset during watchdog restart");
	restart_stage = RESTART_ACTIVE;
	commit_state();

	channel = install_and_setup(&cfg, 0U);
	k_sleep(K_MSEC(10));
	zassert_ok(wdt_disable(wdt), "Initial disable failed");

	cfg.window.max = 100U;
	cfg.flags = WDT_FLAG_RESET_NONE;
	zassert_equal(wdt_install_timeout(wdt, &cfg), -ENOTSUP,
		      "A latched reset-enable bit must not be silently cleared");
	cfg.flags = WDT_FLAG_RESET_SOC;
	channel = install_and_setup(&cfg, 250U);
	k_sleep(K_MSEC(10));
	zassert_ok(wdt_feed(wdt, channel), "Feed after shortened timeout failed");
	zassert_ok(wdt_disable(wdt), "Disable after shortened timeout failed");

	cfg.window.min = 50U;
	channel = install_and_setup(&cfg, 150U);
	for (int i = 0; i < 2; i++) {
		k_sleep(K_MSEC(60));
		zassert_ok(wdt_feed(wdt, channel), "Feed after windowed restart failed");
	}

	zassert_true(WAIT_FOR(base->TV <= 0xFFU, 150000U, k_busy_wait(1)),
		     "Watchdog counter did not advance");
	zassert_ok(wdt_disable(wdt), "Disable near the initial TV value failed");
	stopped_tv = base->TV;
	cfg.window.min = 0U;
	channel = install_and_setup(&cfg, 20U);
	zassert_true(base->TV > stopped_tv, "Setup resumed old TV instead of reloading TC");
	zassert_ok(wdt_feed(wdt, channel), "Feed after low-TV restart failed");
	zassert_ok(wdt_disable(wdt), "Disable after low-TV restart failed");

	callback_disable_result = -EINPROGRESS;
	cfg.callback = disable_callback;
	channel = install_and_setup(&cfg, 20U);
	k_sleep(K_MSEC(120));
	zassert_ok(callback_disable_result, "Disable from warning callback failed");

	callback_seen = 0U;
	commit_state();
	cfg.callback = mark_callback;
	channel = install_and_setup(&cfg, 150U);
	k_sleep(K_MSEC(10));
	zassert_equal(callback_seen, 0U, "Restart delivered a stale warning interrupt");
	zassert_ok(wdt_feed(wdt, channel), "Feed after callback reinstall failed");

	restart_stage = RESTART_EXPECT_RESET;
	commit_state();
	k_sleep(K_MSEC(150));
	zassert_unreachable("Reinstalled watchdog did not reset the board");
}

ZTEST_SUITE(wdt_basic_test_suite, NULL, NULL, NULL, restart_after, NULL);
