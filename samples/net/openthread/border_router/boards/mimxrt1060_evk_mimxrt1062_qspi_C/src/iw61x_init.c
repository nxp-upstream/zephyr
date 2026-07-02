/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/init.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(iw61x_init, LOG_LEVEL_INF);

static K_SEM_DEFINE(bt_ready_sem, 0, 1);
static atomic_t bt_init_result = ATOMIC_INIT(-EAGAIN);

#define IW61x_STABILITY_WAIT 1

static void bt_ready_cb(int err)
{
	atomic_set(&bt_init_result, err);

	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		k_sem_give(&bt_ready_sem);
		return;
	}

	LOG_INF("IW61X BT+15.4 firmware loaded via UART H4");
	k_sem_give(&bt_ready_sem);
}

static int ioexp_enable_spi_m2(void)
{
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(lpi2c1));
	uint8_t buf[2] = {0x03, 0xFE};

	if (!device_is_ready(i2c)) {
		LOG_ERR("I2C not ready");
		return -ENODEV;
	}

	int ret = i2c_write(i2c, buf, sizeof(buf), 0x20);

	if (ret < 0) {
		LOG_ERR("IO Expander config failed: %d", ret);
		return ret;
	}

	LOG_INF("IO Expander configured — IW61X SPI M.2 active");
	return 0;
}

static int iw61x_init_fn(void)
{
	int err;

	LOG_INF("IW61X bringup started (bt_enable)...");

	err = bt_enable(bt_ready_cb);
	if (err) {
		LOG_ERR("bt_enable failed: %d", err);
		atomic_set(&bt_init_result, err);
		k_sem_give(&bt_ready_sem);
		return err;
	}

	/* Return immediately — iw61x_wait_ready() blocks in main() */
	return 0;
}

int iw61x_wait_ready(k_timeout_t timeout)
{
	int err;

	/* Wait for bt_ready_cb: BT+15.4 firmware uploaded */
	if (k_sem_take(&bt_ready_sem, timeout)) {
		LOG_ERR("IW61X BT init timeout");
		return -ETIMEDOUT;
	}

	err = (int)atomic_get(&bt_init_result);
	if (err) {
		return err;
	}

	/* Wait for 15.4 co-processor to be operational */
	LOG_INF("Waiting for IW61X 15.4 co-processor...");
	k_sleep(K_SECONDS(IW61x_STABILITY_WAIT));

	/* IO Expander: enable physical SPI routing on M.2 connector */
	err = ioexp_enable_spi_m2();
	if (err) {
		LOG_ERR("IO Expander failed: %d", err);
		return err;
	}

	LOG_INF("IW61X fully ready — BT + 15.4 SPI operational");
	return 0;
}

/*
 * APPLICATION/89: before network stack (APPLICATION/90)
 */
SYS_INIT(iw61x_init_fn, APPLICATION, 89);
