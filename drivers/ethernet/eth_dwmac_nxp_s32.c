/*
 * Driver for Synopsys DesignWare MAC
 *
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NXP S32 specific glue.
 */

#define LOG_MODULE_NAME dwmac_plat
#define LOG_LEVEL       CONFIG_ETHERNET_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define DT_DRV_COMPAT nxp_s32_gmac

#include <zephyr/kernel.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/ethernet/eth_dwmac_priv.h>
#include <zephyr/irq.h>

#if defined(CONFIG_SOC_SERIES_S32K3)
#include <Clock_Ip.h>
#endif /* CONFIG_SOC_SERIES_S32K3 */

#include "eth.h"

/* NXP Organizational Unique Identifier */
#define NXP_OUI_BYTE_0 0xAC
#define NXP_OUI_BYTE_1 0x9A
#define NXP_OUI_BYTE_2 0x22

struct dwmac_config {
	mem_addr_t base;
	void (*instance_init)(struct dwmac_priv *p);
	const struct pinctrl_dev_config *pincfg;
	uint8_t mii_mode;
	bool random_mac_addr;
};

static int select_phy_interface(uint8_t mii_mode)
{
#if defined(CONFIG_SOC_SERIES_S32K3)
	uint32_t reg_val;

	switch (mii_mode) {
	case 0: /* mii */
		reg_val = DCM_GPR_DCMRWF1_EMAC_CONF_SEL(0U);
		break;
	case 1: /* rmii */
		reg_val = DCM_GPR_DCMRWF1_EMAC_CONF_SEL(2U);
		break;
	case 3: /* rgmii */
		reg_val = DCM_GPR_DCMRWF1_EMAC_CONF_SEL(1U);
		break;
	default:
		return -EINVAL;
	}

	IP_DCM_GPR->DCMRWF1 = (IP_DCM_GPR->DCMRWF1 & ~DCM_GPR_DCMRWF1_EMAC_CONF_SEL_MASK) | reg_val;
#else
#error "SoC not supported"
#endif /* CONFIG_SOC_SERIES_S32K3 */

	return 0;
}

int dwmac_bus_init(const struct device *dev)
{
	struct dwmac_priv *p = dev->data;
	const struct dwmac_config *cfg = dev->config;
	int ret;

	ret = pinctrl_apply_state(cfg->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Could not configure ethernet pins");
		return ret;
	}

#if defined(CONFIG_SOC_SERIES_S32K3)
// TODO: make it a config
#define DWMAC_NXP_S32_CLOCK_CONFIG_IDX 1U
	/*
	 * Currently, clock control shim driver does not support configuring clock
	 * muxes individually, so use the HAL directly.
	 */
	Clock_Ip_StatusType status;
	status = Clock_Ip_Init(&Clock_Ip_aClockConfig[DWMAC_NXP_S32_CLOCK_CONFIG_IDX]);
	if (status != CLOCK_IP_SUCCESS) {
		LOG_ERR("Failed to configure ethernet clocks (%d)", status);
		return -EIO;
	}
#endif /* CONFIG_SOC_SERIES_S32K3 */

	/*
	 * PHY mode selection must be done before the controller is reset,
	 * because the interface type is latched at controller's reset
	 */
	ret = select_phy_interface(cfg->mii_mode);
	if (ret < 0) {
		LOG_ERR("Failed to configure PHY interface (%d)", ret);
		return ret;
	}

	p->base_addr = cfg->base;
	return 0;
}

void dwmac_platform_init(const struct device *dev)
{
	struct dwmac_priv *p = dev->data;
	const struct dwmac_config *cfg = dev->config;
	uint32_t reg_val;

	/* Interrupts are level signals asserted on TX/RX packet transfer completion events */
	// REG_WRITE(DMA_MODE, FIELD_PREP(DMA_MODE_INTM, 1));

	/* AHB - Address-Aligned Beats */
	REG_WRITE(DMA_SYSBUS_MODE, DMA_SYSBUS_MODE_AAL);

	/* MAC configuration */
	reg_val = REG_READ(MAC_CONF);
	reg_val |= MAC_CONF_PS        /* 10/100 Mbps */
		   | MAC_CONF_FES     /* 100 Mbps */
		   | MAC_CONF_DM      /* full-duplex */
		   | MAC_CONF_ECRSFD; /* check CRS signal before transmitting in full-duplex mode */
	REG_WRITE(MAC_CONF, reg_val);

	if (cfg->random_mac_addr) {
		gen_random_mac(p->mac_addr, NXP_OUI_BYTE_0, NXP_OUI_BYTE_1, NXP_OUI_BYTE_2);
	}

	cfg->instance_init(p);
}

#define DWMAC_NXP_S32_INST_IRQ_INIT(n, name, isr)                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(n, name, irq), DT_INST_IRQ_BY_NAME(n, name, priority),     \
		    isr, DEVICE_DT_INST_GET(n),                                                    \
		    COND_CODE_1(DT_INST_IRQ_HAS_CELL(n, flags),                                    \
				(DT_INST_IRQ_BY_NAME(n, name, flags)), (0)));                      \
	irq_enable(DT_INST_IRQ_BY_NAME(n, name, irq));

#define DWMAC_NXP_S32_INST_INIT(n)                                                                 \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static void instance_init##n(struct dwmac_priv *p)                                         \
	{                                                                                          \
		DWMAC_NXP_S32_INST_IRQ_INIT(n, tx, dwmac_isr);                                     \
		DWMAC_NXP_S32_INST_IRQ_INIT(n, rx, dwmac_isr);                                     \
	}                                                                                          \
	static struct dwmac_dma_desc dwmac_tx_descs##n[NB_TX_DESCS] __nocache __aligned(4);        \
	static struct dwmac_dma_desc dwmac_rx_descs##n[NB_RX_DESCS] __nocache __aligned(4);        \
	static struct dwmac_priv dwmac_data##n = {                                                 \
		.tx_descs = dwmac_tx_descs##n,                                                     \
		.rx_descs = dwmac_rx_descs##n,                                                     \
		.mac_addr = DT_INST_PROP(n, local_mac_address),                                    \
	};                                                                                         \
	static const struct dwmac_config dwmac_config##n = {                                       \
		.base = (mem_addr_t)DT_INST_REG_ADDR(n),                                           \
		.instance_init = instance_init##n,                                                 \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.mii_mode = DT_INST_ENUM_IDX_OR(n, phy_connection_type, 0),                        \
		.random_mac_addr = DT_INST_PROP(n, zephyr_random_mac_address),                     \
	};                                                                                         \
	ETH_NET_DEVICE_DT_INST_DEFINE(n, dwmac_probe, NULL, &dwmac_data##n, &dwmac_config##n,      \
				      CONFIG_ETH_INIT_PRIORITY, &dwmac_api, NET_ETH_MTU);

DT_INST_FOREACH_STATUS_OKAY(DWMAC_NXP_S32_INST_INIT)
