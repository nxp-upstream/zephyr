/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lvds_panel_sequencer

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/ldb.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(lvds_panel_seq, CONFIG_DISPLAY_LOG_LEVEL);

struct lvds_panel_sequencer_config {
	struct gpio_dt_spec enable_gpio;
	struct gpio_dt_spec reset_gpio;

	uint32_t prepare_delay_ms;
	uint32_t reset_assert_us;
	uint32_t reset_deassert_ms;

	const struct device *ldb_dev; /* optional */
};

static int lvds_panel_sequencer_init(const struct device *dev)
{
	const struct lvds_panel_sequencer_config *cfg = dev->config;
	int ret;

	/* Enable GPIO */
	if (cfg->enable_gpio.port != NULL) {
		if (!device_is_ready(cfg->enable_gpio.port)) {
			LOG_ERR("enable-gpios controller not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("enable-gpios configure failed: %d", ret);
			return ret;
		}

		ret = gpio_pin_set_dt(&cfg->enable_gpio, 1);
		if (ret < 0) {
			LOG_ERR("enable-gpios set failed: %d", ret);
			return ret;
		}
	}

	if (cfg->prepare_delay_ms) {
		k_sleep(K_MSEC(cfg->prepare_delay_ms));
	}

	/* Reset sequence: assert -> deassert
	 * gpio_pin_set_dt() uses logical values (1=active, 0=inactive) and honors GPIO_ACTIVE_LOW/HIGH.
	 */
	if (cfg->reset_gpio.port != NULL) {
		if (!device_is_ready(cfg->reset_gpio.port)) {
			LOG_ERR("reset-gpios controller not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("reset-gpios configure failed: %d", ret);
			return ret;
		}

		/* assert */
		ret = gpio_pin_set_dt(&cfg->reset_gpio, 1);
		if (ret < 0) {
			LOG_ERR("reset-gpios assert failed: %d", ret);
			return ret;
		}

		if (cfg->reset_assert_us) {
			k_sleep(K_USEC(cfg->reset_assert_us));
		}

		/* deassert */
		ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
		if (ret < 0) {
			LOG_ERR("reset-gpios deassert failed: %d", ret);
			return ret;
		}

		if (cfg->reset_deassert_ms) {
			k_sleep(K_MSEC(cfg->reset_deassert_ms));
		}
	}

	/* Optional: enable LDB after panel is ready */
	if (cfg->ldb_dev != NULL) {
		if (!device_is_ready(cfg->ldb_dev)) {
			LOG_ERR("LDB device not ready");
			return -ENODEV;
		}

		ret = ldb_enable(cfg->ldb_dev);
		if (ret < 0 && ret != -ENOSYS) {
			LOG_ERR("ldb_enable failed: %d", ret);
			return ret;
		}
	}

	return 0;
}

#define LVDS_PANEL_SEQUENCER_DEFINE(inst)                                             \
	static const struct lvds_panel_sequencer_config cfg_##inst = {                \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}),     \
		.reset_gpio  = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios,  {0}),     \
		.prepare_delay_ms  = DT_INST_PROP_OR(inst, prepare_delay_ms, 0),      \
		.reset_assert_us   = DT_INST_PROP_OR(inst, reset_assert_us, 0),       \
		.reset_deassert_ms = DT_INST_PROP_OR(inst, reset_deassert_ms, 0),     \
		.ldb_dev = DT_INST_NODE_HAS_PROP(inst, ldb) ?                          \
			DEVICE_DT_GET(DT_INST_PHANDLE(inst, ldb)) : NULL,              \
	};                                                                              \
	DEVICE_DT_INST_DEFINE(inst, lvds_panel_sequencer_init, NULL,                   \
			      NULL, &cfg_##inst,                                       \
			      POST_KERNEL, CONFIG_NXP_LVDS_PANEL_SEQUENCER_INIT_PRIORITY, \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(LVDS_PANEL_SEQUENCER_DEFINE)
