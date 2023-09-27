/*
 * Copyright (c) 2019, Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lvds_panel
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lvds_panel, CONFIG_DISPLAY_LOG_LEVEL);

struct lvds_panel_config {
	const struct gpio_dt_spec lvds_blt_en_gpio;
	const struct gpio_dt_spec lvds_blt_pwm_gpio;
};

static int lvds_panel_init(const struct device *dev)
{
	const struct lvds_panel_config *cfg = dev->config;

	gpio_pin_configure_dt(&cfg->lvds_blt_en_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&cfg->lvds_blt_en_gpio, 1);
	gpio_pin_configure_dt(&cfg->lvds_blt_pwm_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&cfg->lvds_blt_pwm_gpio, 1);

	LOG_INF("%s init succeeded", dev->name);

	return 0;

}

#define GET_MEDIA_BUS_FMT(id)									\
(												\
	(DT_INST_ENUM_IDX(id, bus_format) == 0) ? MEDIA_BUS_FMT_RGB666_1X7X3_SPWG :		\
	((DT_INST_ENUM_IDX(id, bus_format) == 1) ? MEDIA_BUS_FMT_RGB888_1X7X4_SPWG :		\
	 MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA)							\
)												\


#define DISPLAY_MCUX_LVDS_PANEL_INIT(id)						\
	static const struct lvds_panel_config lvds_panel_config_##id = {		\
		.lvds_blt_en_gpio = GPIO_DT_SPEC_INST_GET(id, lvds_blt_en_gpios),	\
		.lvds_blt_pwm_gpio = GPIO_DT_SPEC_INST_GET(id, lvds_blt_pwm_gpios),	\
	};										\
											\
	DEVICE_DT_INST_DEFINE(id, lvds_panel_init, NULL,				\
			    NULL, &lvds_panel_config_##id,				\
			    POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,			\
			    NULL);							\
											\

DT_INST_FOREACH_STATUS_OKAY(DISPLAY_MCUX_LVDS_PANEL_INIT)
