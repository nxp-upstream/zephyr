/*
 * Copyright (c) 2023, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx93_mediamix

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mediamix, CONFIG_MEDIAMIX_LOG_LEVEL);

#include <fsl_common.h>

struct mcux_mediamix_config {
	MEDIAMIX_BLK_CTRL_Type *base;
};

static void imx93_mediamix_set_qos_isi(MEDIAMIX_BLK_CTRL_Type *base)
{
	uint32_t reg = 0;

	reg |= MEDIAMIX_BLK_CTRL_ISI1_DEFAULT_QOS_V(0x3) |
	       MEDIAMIX_BLK_CTRL_ISI1_CFG_QOS_V(0x7) |
	       MEDIAMIX_BLK_CTRL_ISI1_DEFAULT_QOS_U(0x3) |
	       MEDIAMIX_BLK_CTRL_ISI1_CFG_QOS_U(0x7) |
	       MEDIAMIX_BLK_CTRL_ISI1_DEFAULT_QOS_Y_R(0x3) |
	       MEDIAMIX_BLK_CTRL_ISI1_CFG_QOS_Y_R(0x7) |
	       MEDIAMIX_BLK_CTRL_ISI1_DEFAULT_QOS_Y_W(0x3) |
	       MEDIAMIX_BLK_CTRL_ISI1_CFG_QOS_Y_W(0x7);
	base->BUS_CONTROL.ISI1 = reg;
}

static const struct mcux_mediamix_config mcux_mediamix_config_0 = {
	.base = (MEDIAMIX_BLK_CTRL_Type *)DT_INST_REG_ADDR(0),

};

static int mcux_mediamix_init_0(const struct device *dev)
{
	const struct mcux_mediamix_config *config = dev->config;
	int ret;

	imx93_mediamix_set_qos_isi(config->base);

	LOG_INF("%s init succeeded", dev->name);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, mcux_mediamix_init_0, NULL,
		    NULL, &mcux_mediamix_config_0, POST_KERNEL,
		    CONFIG_MEDIAMIX_BLK_CTRL_INIT_PRIORITY, NULL);
