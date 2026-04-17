/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_pcal6524

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#include "pcal6524_emul.h"

LOG_MODULE_REGISTER(pcal6524_emul, CONFIG_GPIO_LOG_LEVEL);

struct pcal6524_emul_cfg {
	uint16_t addr;
};

struct pcal6524_emul_data {
	uint8_t regs[256];
};

static int pcal6524_emul_check_range(uint8_t reg, size_t len)
{
	if ((size_t)reg + len > 256U) {
		return -EIO;
	}

	return 0;
}

int pcal6524_emul_get_reg(const struct emul *target, uint8_t reg, uint8_t *buf, size_t len)
{
	struct pcal6524_emul_data *data = target->data;
	int ret;

	if (buf == NULL) {
		return -EINVAL;
	}

	ret = pcal6524_emul_check_range(reg, len);
	if (ret != 0) {
		return ret;
	}

	memcpy(buf, &data->regs[reg], len);

	return 0;
}

int pcal6524_emul_set_reg(const struct emul *target, uint8_t reg, const uint8_t *buf, size_t len)
{
	struct pcal6524_emul_data *data = target->data;
	int ret;

	if (buf == NULL) {
		return -EINVAL;
	}

	ret = pcal6524_emul_check_range(reg, len);
	if (ret != 0) {
		return ret;
	}

	memcpy(&data->regs[reg], buf, len);

	return 0;
}

static int pcal6524_emul_transfer_i2c(const struct emul *target, struct i2c_msg msgs[], int num_msgs,
				      int addr)
{
	const struct pcal6524_emul_cfg *cfg = target->cfg;
	uint8_t reg;
	int ret;

	if ((msgs == NULL) || (num_msgs != 2)) {
		return -EIO;
	}

	if (addr != cfg->addr) {
		return -EIO;
	}

	if ((msgs[0].flags & I2C_MSG_READ) != 0U || msgs[0].len != 1U) {
		return -EIO;
	}

	reg = msgs[0].buf[0];

	if ((msgs[1].flags & I2C_MSG_READ) != 0U) {
		ret = pcal6524_emul_get_reg(target, reg, msgs[1].buf, msgs[1].len);
		if (ret == 0) {
			LOG_DBG("read reg 0x%02x len %u", reg, msgs[1].len);
		}
		return ret;
	}

	ret = pcal6524_emul_set_reg(target, reg, msgs[1].buf, msgs[1].len);
	if (ret == 0) {
		LOG_DBG("write reg 0x%02x len %u", reg, msgs[1].len);
	}

	return ret;
}

static int pcal6524_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);

	return 0;
}

static const struct i2c_emul_api pcal6524_emul_api_i2c = {
	.transfer = pcal6524_emul_transfer_i2c,
};

#define PCAL6524_EMUL(n)                                                                     \
	static const struct pcal6524_emul_cfg pcal6524_emul_cfg_##n = {                     \
		.addr = DT_INST_REG_ADDR(n),                                                 \
	};                                                                                 \
	static struct pcal6524_emul_data pcal6524_emul_data_##n;                           \
	EMUL_DT_INST_DEFINE(n, pcal6524_emul_init, &pcal6524_emul_data_##n,                \
			    &pcal6524_emul_cfg_##n, &pcal6524_emul_api_i2c, NULL)

DT_INST_FOREACH_STATUS_OKAY(PCAL6524_EMUL)
