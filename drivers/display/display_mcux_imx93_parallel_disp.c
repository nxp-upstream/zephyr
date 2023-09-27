/*
 * Copyright (c) 2019, Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx93_pdi
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(imx93_parallel_disp_fmt, CONFIG_DISPLAY_LOG_LEVEL);

enum imx93_parallel_disp_fmt {
	MEDIA_BUS_FMT_RGB565_1X16 = BIT(0),
	MEDIA_BUS_FMT_RGB666_1X18 = BIT(1),
	MEDIA_BUS_FMT_RGB888_1X24 = BIT(2),
};

struct parallel_disp_fmt_config {
	MEDIAMIX_BLK_CTRL_Type *base;
	enum imx93_parallel_disp_fmt bus_format;
	const struct gpio_dt_spec enable_gpio;
	const struct pinctrl_dev_config *pincfg;
};

static int parallel_disp_fmt_init(const struct device *dev)
{
	const struct parallel_disp_fmt_config *cfg = dev->config;
	int err = 0;

	err = pinctrl_apply_state(cfg->pincfg, PINCTRL_STATE_DEFAULT);
	if (err) {
		return err;
	}
	gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&cfg->enable_gpio, 1);

	cfg->base->GASKET.DISPLAY_MUX &= ~MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT_MASK;
	if(cfg->bus_format == MEDIA_BUS_FMT_RGB565_1X16)
	{
		cfg->base->GASKET.DISPLAY_MUX |= MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT(2);
		LOG_INF("DISPLAY_MUX: RGB565_TO_RGB565");
	}
	else if(cfg->bus_format == MEDIA_BUS_FMT_RGB666_1X18)
	{
		cfg->base->GASKET.DISPLAY_MUX |= MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT(1);
		LOG_INF("DISPLAY_MUX: RGB888_TO_RGB666");
	}
	else if(cfg->bus_format == MEDIA_BUS_FMT_RGB888_1X24)
	{
		cfg->base->GASKET.DISPLAY_MUX |= MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT(0);
		LOG_INF("DISPLAY_MUX: RGB888_TO_RGB888");
	}
	else
	{
		LOG_ERR("DISPLAY_MUX: unknow\n");
		err = -1;
	}

	if(!err)
		LOG_INF("%s init succeeded\n", dev->name);

	return err;

}

#define GET_MEDIA_BUS_FMT(id)									\
(												\
	(DT_INST_ENUM_IDX(id, interface_pix_fmt) == 0) ? MEDIA_BUS_FMT_RGB565_1X16 :		\
	((DT_INST_ENUM_IDX(id, interface_pix_fmt) == 1) ? MEDIA_BUS_FMT_RGB666_1X18 :		\
	 MEDIA_BUS_FMT_RGB888_1X24)								\
)												\


#define DISPLAY_MCUX_PARALLEL_DISPLAY_FMT_INIT(id)					\
	PINCTRL_DT_INST_DEFINE(id);							\
	static const struct parallel_disp_fmt_config parallel_disp_fmt_config_##id = {	\
		.base = (MEDIAMIX_BLK_CTRL_Type *)DT_REG_ADDR(DT_INST_PARENT(id)),	\
		.enable_gpio = GPIO_DT_SPEC_INST_GET(id, enable_gpios),			\
		.bus_format = GET_MEDIA_BUS_FMT(id),					\
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(id),				\
	};										\
											\
	DEVICE_DT_INST_DEFINE(id, parallel_disp_fmt_init, NULL,				\
			    NULL, &parallel_disp_fmt_config_##id,			\
			    POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,			\
			    NULL);							\
											\

DT_INST_FOREACH_STATUS_OKAY(DISPLAY_MCUX_PARALLEL_DISPLAY_FMT_INIT)
