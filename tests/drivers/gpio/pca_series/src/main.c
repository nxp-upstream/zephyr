/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <zephyr/dt-bindings/gpio/pca-series-gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "pcal6524_emul.h"

#define PCAL6524_NODE DT_ALIAS(gpio_expander)

#define PCAL6524_OUTPUT_PORT_REG 0x04
#define PCAL6524_OUTPUT_DRIVE_STRENGTH_REG 0x40
#define PCAL6524_OUTPUT_CONFIG_REG 0x70
#define PCA_REG_TYPE_1B_OUTPUT_PORT_TEST 1U
#define PCA_REG_INVALID_TEST 0xffU

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(PCAL6524_NODE), "gpio-expander alias is required");
BUILD_ASSERT(PCA_SERIES_GPIO_DRIVE_STRENGTH_CONFIG(PCA_SERIES_GPIO_DRIVE_STRENGTH_X1) == 0U);
BUILD_ASSERT(PCA_SERIES_GPIO_DRIVE_STRENGTH_CONFIG(PCA_SERIES_GPIO_DRIVE_STRENGTH_X2) == 1U);
BUILD_ASSERT(PCA_SERIES_GPIO_DRIVE_STRENGTH_CONFIG(PCA_SERIES_GPIO_DRIVE_STRENGTH_X3) == 2U);
BUILD_ASSERT(PCA_SERIES_GPIO_DRIVE_STRENGTH_CONFIG(PCA_SERIES_GPIO_DRIVE_STRENGTH_X4) == 3U);
BUILD_ASSERT(PCA_SERIES_GPIO_DRIVE_STRENGTH_ENABLE(PCA_SERIES_GPIO_DRIVE_STRENGTH_X1) == 1U);
BUILD_ASSERT(PCA_SERIES_GPIO_DRIVE_STRENGTH_ENABLE(PCA_SERIES_GPIO_DRIVE_STRENGTH_X4) == 1U);

struct gpio_pca_series_data_test {
	struct gpio_driver_data common;
	struct k_sem lock;
	void *cache;
};

struct gpio_pca_series_part_config_test {
	uint8_t port_no;
	uint8_t flags;
	const uint8_t *regs;
#ifdef CONFIG_GPIO_PCA_SERIES_CACHE_ALL
#ifdef GPIO_NXP_PCA_SERIES_DEBUG
	uint8_t cache_size;
#endif
	const uint8_t *cache_map;
#endif
};

struct gpio_pca_series_config_test {
	struct gpio_driver_config common;
	struct i2c_dt_spec i2c;
	const struct gpio_pca_series_part_config_test *part_cfg;
	struct gpio_dt_spec gpio_rst;
};

static const struct device *const pcal6524_dev = DEVICE_DT_GET(PCAL6524_NODE);
static const struct emul *const pcal6524_emul = EMUL_DT_GET(PCAL6524_NODE);

static void read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
	zassert_ok(pcal6524_emul_get_reg(pcal6524_emul, reg, buf, len));
}

#if !defined(CONFIG_GPIO_PCA_SERIES_CACHE_ALL)
ZTEST(pca_series, test_mini_cache_pointer_uses_dedicated_buffer)
{
	struct gpio_pca_series_data_test *data =
		(struct gpio_pca_series_data_test *)pcal6524_dev->data;
	uintptr_t cache_ptr = (uintptr_t)data->cache;
	uint8_t output_reg[3];

	zassert_true(device_is_ready(pcal6524_dev));
	zassert_not_null(data->cache, "mini-cache pointer was corrupted during init");

	zassert_ok(gpio_pin_configure(pcal6524_dev, 0, GPIO_OUTPUT_LOW));
	zassert_ok(gpio_port_set_bits_raw(pcal6524_dev, BIT(0)));

	zassert_equal(cache_ptr, (uintptr_t)data->cache,
		      "mini-cache write should not alias runtime struct fields");

	read_reg(PCAL6524_OUTPUT_PORT_REG, output_reg, sizeof(output_reg));
	zassert_equal(output_reg[0] & BIT(0), BIT(0));
}
#endif

ZTEST(pca_series, test_open_source_is_rejected)
{
	uint8_t output_cfg[3];

	zassert_ok(gpio_pin_configure(pcal6524_dev, 1, GPIO_OUTPUT_LOW));
	zassert_equal(gpio_pin_configure(pcal6524_dev, 1, GPIO_OUTPUT | GPIO_OPEN_SOURCE),
		      -ENOTSUP);

	read_reg(PCAL6524_OUTPUT_CONFIG_REG, output_cfg, sizeof(output_cfg));
	zassert_equal(output_cfg[0] & BIT(1), 0U);
}

ZTEST(pca_series, test_open_drain_sets_output_config)
{
	uint8_t output_cfg[3];

	zassert_ok(gpio_pin_configure(pcal6524_dev, 2, GPIO_OUTPUT_LOW));
	zassert_ok(gpio_pin_configure(pcal6524_dev, 2, GPIO_OUTPUT | GPIO_OPEN_DRAIN));

	read_reg(PCAL6524_OUTPUT_CONFIG_REG, output_cfg, sizeof(output_cfg));
	zassert_equal(output_cfg[0] & BIT(2), BIT(2));
}

#ifdef CONFIG_GPIO_PCA_SERIES_CACHE_ALL
ZTEST(pca_series, test_full_cache_port_write_path)
{
	uint8_t output_reg[3];

	zassert_ok(gpio_pin_configure(pcal6524_dev, 3, GPIO_OUTPUT_LOW));
	zassert_ok(gpio_port_set_bits_raw(pcal6524_dev, BIT(3)));

	read_reg(PCAL6524_OUTPUT_PORT_REG, output_reg, sizeof(output_reg));
	zassert_equal(output_reg[0] & BIT(3), BIT(3));
}
#endif

ZTEST(pca_series, test_drive_strength_flags_program_register)
{
	uint8_t drive_strength_reg[6];

	zassert_ok(gpio_pin_configure(pcal6524_dev, 0,
				      GPIO_OUTPUT_LOW | PCA_SERIES_GPIO_DRIVE_STRENGTH_X2));

	read_reg(PCAL6524_OUTPUT_DRIVE_STRENGTH_REG,
		 drive_strength_reg, sizeof(drive_strength_reg));
	zassert_equal(drive_strength_reg[0] & 0x3U, 0x1U);
}

#if defined(CONFIG_GPIO_PCA_SERIES_CACHE_ALL) && defined(GPIO_NXP_PCA_SERIES_DEBUG)
ZTEST(pca_series, test_port_write_error_path_releases_lock)
{
	struct gpio_pca_series_data_test *data =
		(struct gpio_pca_series_data_test *)pcal6524_dev->data;
	const struct gpio_pca_series_config_test *real_cfg = pcal6524_dev->config;
	struct gpio_pca_series_part_config_test shadow_part = {
		.port_no = real_cfg->part_cfg->port_no,
		.flags = real_cfg->part_cfg->flags,
		.regs = real_cfg->part_cfg->regs,
#ifdef GPIO_NXP_PCA_SERIES_DEBUG
		.cache_size = real_cfg->part_cfg->cache_size,
#endif
	};
	uint8_t fault_cache_map[16] = { 0U };
	struct gpio_pca_series_config_test shadow_cfg = *real_cfg;
	struct device shadow_dev = *pcal6524_dev;
	int ret;

	fault_cache_map[PCA_REG_TYPE_1B_OUTPUT_PORT_TEST] = PCA_REG_INVALID_TEST;
	shadow_part.cache_map = fault_cache_map;
	shadow_cfg.part_cfg = &shadow_part;
	shadow_dev.config = &shadow_cfg;

	ret = gpio_port_set_bits_raw(&shadow_dev, BIT(4));
	zassert_equal(ret, -EINVAL, "unexpected return %d", ret);
	zassert_ok(k_sem_take(&data->lock, K_NO_WAIT),
		   "device lock should be released on error");
	k_sem_give(&data->lock);
	zassert_ok(gpio_port_set_bits_raw(pcal6524_dev, BIT(4)));
}
#endif

ZTEST_SUITE(pca_series, NULL, NULL, NULL, NULL, NULL);
