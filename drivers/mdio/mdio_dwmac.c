/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_designware_ethernet_mdio

#define LOG_MODULE_NAME dwmac_mdio
#define LOG_LEVEL       CONFIG_MDIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/mdio.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/ethernet/eth_dwmac_priv.h>

struct dwmac_mdio_config {
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	bool suppress_preamble;
};

struct dwmac_mdio_priv {
	mem_addr_t base_addr;
	struct k_mutex bus_lock;
	uint8_t csr_clock_range;
};

struct dwmac_mdio_transfer {
	enum mdio_opcode op;
	bool c45;
	union {
		uint16_t out;
		uint16_t *in;
	} data;
	uint8_t physaddr;
	uint8_t devaddr;
	uint16_t regaddr;
};

static inline int dwmac_mdio_wait_for(const struct device *dev, uint32_t regaddr, uint32_t mask)
{
	struct dwmac_mdio_priv *p = dev->data;
	bool ret;

	ret = WAIT_FOR(!(REG_READ(regaddr) & mask), CONFIG_MDIO_DWMAC_TIMEOUT, k_busy_wait(1));

	return !ret ? -ETIMEDOUT : 0;
}

static int dwmac_mdio_transfer(const struct device *dev, struct dwmac_mdio_transfer *mdio)
{
	const struct dwmac_mdio_config *const cfg = dev->config;
	struct dwmac_mdio_priv *p = dev->data;
	int ret;
	uint32_t regval;

	if ((mdio->op == MDIO_OP_C22_READ) || (mdio->op == MDIO_OP_C45_READ)) {
		if (mdio->data.in == NULL) {
			return -EINVAL;
		}
	}

	k_mutex_lock(&p->bus_lock, K_FOREVER);

	regval = FIELD_PREP(MAC_MDIO_DATA_GD, mdio->data.out) |
		 FIELD_PREP(MAC_MDIO_DATA_RA, mdio->c45 ? mdio->regaddr : 0U);
	REG_WRITE(MAC_MDIO_DATA, regval);
	LOG_DBG("%s: MAC_MDIO_DATA=0x%x", dev->name, regval);

	regval = MAC_MDIO_ADDRESS_GOC_GB | FIELD_PREP(MAC_MDIO_ADDRESS_GOC_C45E, mdio->c45) |
		 MAC_MDIO_ADDRESS_GOC_0 |
		 FIELD_PREP(MAC_MDIO_ADDRESS_GOC_1,
			    ((mdio->op == MDIO_OP_C22_READ) || (mdio->op == MDIO_OP_C45_READ))) |
		 FIELD_PREP(MAC_MDIO_ADDRESS_CR, p->csr_clock_range) |
		 FIELD_PREP(MAC_MDIO_ADDRESS_RDA, mdio->c45 ? mdio->devaddr : mdio->regaddr) |
		 FIELD_PREP(MAC_MDIO_ADDRESS_PA, mdio->physaddr) |
		 FIELD_PREP(MAC_MDIO_ADDRESS_PSE, cfg->suppress_preamble);
	REG_WRITE(MAC_MDIO_ADDRESS, regval);
	LOG_DBG("%s: MAC_MDIO_ADDRESS=0x%x", dev->name, regval);

	ret = dwmac_mdio_wait_for(dev, MAC_MDIO_ADDRESS, MAC_MDIO_ADDRESS_GOC_GB);
	if (ret) {
		LOG_ERR("%s: transfer timedout", dev->name);
		goto done;
	}

	if ((mdio->op == MDIO_OP_C22_READ) || (mdio->op == MDIO_OP_C45_READ)) {
		*mdio->data.in = REG_READ(MAC_MDIO_DATA) & MAC_MDIO_DATA_GD;
	}

done:
	k_mutex_unlock(&p->bus_lock);

	return ret;
}

static int dwmac_mdio_read_c45(const struct device *dev, uint8_t prtad, uint8_t devad,
			       uint16_t regad, uint16_t *regval)
{
	struct dwmac_mdio_transfer mdio = {
		.op = MDIO_OP_C45_READ,
		.c45 = true,
		.physaddr = prtad,
		.devaddr = devad,
		.regaddr = regad,
		.data.in = regval,
	};

	return dwmac_mdio_transfer(dev, &mdio);
}

static int dwmac_mdio_write_c45(const struct device *dev, uint8_t prtad, uint8_t devad,
				uint16_t regad, uint16_t regval)
{
	struct dwmac_mdio_transfer mdio = {
		.op = MDIO_OP_C45_WRITE,
		.c45 = true,
		.physaddr = prtad,
		.devaddr = devad,
		.regaddr = regad,
		.data.out = regval,
	};

	return dwmac_mdio_transfer(dev, &mdio);
}

static int dwmac_mdio_read_c22(const struct device *dev, uint8_t prtad, uint8_t regad,
			       uint16_t *regval)
{
	struct dwmac_mdio_transfer mdio = {
		.op = MDIO_OP_C22_READ,
		.c45 = false,
		.physaddr = prtad,
		.regaddr = regad,
		.data.in = regval,
	};

	return dwmac_mdio_transfer(dev, &mdio);
}

static int dwmac_mdio_write_c22(const struct device *dev, uint8_t prtad, uint8_t regad,
				uint16_t regval)
{
	struct dwmac_mdio_transfer mdio = {
		.op = MDIO_OP_C22_WRITE,
		.c45 = false,
		.physaddr = prtad,
		.regaddr = regad,
		.data.out = regval,
	};

	return dwmac_mdio_transfer(dev, &mdio);
}

static int dwmac_mdio_get_csr_clock_range(uint32_t csr_freq, uint8_t *clock_range)
{
	/*
	 * The range of CSR clock frequency applicable for each value ensures that the MDC clock is
	 * approximately between 1.0 MHz to 2.5 MHz frequency range, as specified in the IEEE 802.3.
	 */
	csr_freq /= MHZ(1);
	if (csr_freq >= 20 && csr_freq < 35) {
		*clock_range = 2;
	} else if (csr_freq < 60) {
		*clock_range = 3;
	} else if (csr_freq < 100) {
		*clock_range = 0;
	} else if (csr_freq < 150) {
		*clock_range = 1;
	} else if (csr_freq < 250) {
		*clock_range = 4;
	} else if (csr_freq < 300) {
		*clock_range = 5;
	} else if (csr_freq < 500) {
		*clock_range = 6;
	} else if (csr_freq < 800) {
		*clock_range = 7;
	} else {
		LOG_ERR("MDIO clock frequency out of range");
		return -ENOTSUP;
	}

	return 0;
}

static int dwmac_mdio_init(const struct device *dev)
{
	const struct dwmac_mdio_config *const cfg = dev->config;
	struct dwmac_mdio_priv *p = dev->data;
	uint32_t csr_freq;
	int err;

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("MDIO clock control device not ready");
		return -ENODEV;
	}

	err = clock_control_get_rate(cfg->clock_dev, cfg->clock_subsys, &csr_freq);
	if (err != 0) {
		LOG_ERR("Failed to get MDIO clock frequency (%d)", err);
		return err;
	}

	err = dwmac_mdio_get_csr_clock_range(csr_freq, &p->csr_clock_range);
	if (err != 0) {
		return err;
	}

	if (cfg->pincfg) {
		err = pinctrl_apply_state(cfg->pincfg, PINCTRL_STATE_DEFAULT);
		if (err != 0) {
			LOG_ERR("Failed to initialize MDIO pins (%d)", err);
			return err;
		}
	}

	k_mutex_init(&p->bus_lock);

	return 0;
}

static const struct mdio_driver_api dwmac_mdio_api = {
	.read = dwmac_mdio_read_c22,
	.write = dwmac_mdio_write_c22,
	.read_c45 = dwmac_mdio_read_c45,
	.write_c45 = dwmac_mdio_write_c45,
};

#define DWMAC_MDIO_INST_INIT(n)                                                                    \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static struct dwmac_mdio_priv dwmac_mdio_priv##n = {                                       \
		.base_addr = DT_INST_REG_ADDR(n),                                                  \
	};                                                                                         \
	static const struct dwmac_mdio_config dwmac_mdio_config##n = {                             \
		.clock_dev = DEVICE_DT_GET_OR_NULL(DT_INST_CLOCKS_CTLR(n)),                        \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, name),              \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.suppress_preamble = (bool)DT_INST_PROP(n, suppress_preamble),                     \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, &dwmac_mdio_init, NULL, &dwmac_mdio_priv##n,                      \
			      &dwmac_mdio_config##n, POST_KERNEL, CONFIG_MDIO_INIT_PRIORITY,       \
			      &dwmac_mdio_api);

DT_INST_FOREACH_STATUS_OKAY(DWMAC_MDIO_INST_INIT)
