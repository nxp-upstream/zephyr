/*
 * Copyright (c) 2024 Microchip Technology Inc.
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_t1s_phy

#include <zephyr/kernel.h>
#include <zephyr/net/phy.h>
#include "fsl_device_registers.h"

#define LOG_MODULE_NAME phy_nxp_t1s
#define LOG_LEVEL       CONFIG_PHY_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define NXP_MODECTRL_LINKUP       0x0002
#define NXP_MODESTAT_LINKUP       0x0003
#define OATC14_IDM                0x0a00

struct nxp_t1s_plca_config {
	bool enable;
	uint8_t node_id;
	uint8_t node_count;
	uint8_t burst_count;
	uint8_t burst_timer;
	uint8_t to_timer;
};

struct nxp_t1s_config {
	struct nxp_t1s_plca_config *plca;
	TENBASET_PHY_Type *base;
	void (*irq_config_func)(void);
};

struct nxp_t1s_version {
	uint8_t major;
	uint8_t minor;
	uint8_t customer;
};

struct nxp_t1s_data {
	uint32_t phy_id;
	struct nxp_t1s_version version;
	const struct device *dev;
	struct phy_link_state state;
	phy_callback_t cb;
	void *cb_data;
	struct k_work phy_isr_work;
};

static int phy_nxp_t1s_get_link(const struct device *dev, struct phy_link_state *state)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;

	uint16_t value = base->MODESTAT;

	state->is_up = (value & TENBASET_PHY_MODESTAT_STAT_MASK) == NXP_MODESTAT_LINKUP;
	state->speed = LINK_HALF_10BASE;

	return 0;
}

static int phy_nxp_t1s_link_cb_set(const struct device *dev, phy_callback_t cb, void *user_data)
{
	struct nxp_t1s_data *data = dev->data;

	data->cb = cb;
	data->cb_data = user_data;

	if (data->cb) {
		data->cb(dev, &data->state, data->cb_data);
	}

	return 0;
}

static void phy_isr_work_handler(struct k_work *work)
{
	struct nxp_t1s_data *const data = CONTAINER_OF(work, struct nxp_t1s_data, phy_isr_work);
	struct phy_link_state old_state = data->state;
	const struct device *dev = data->dev;
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;

	phy_nxp_t1s_get_link(dev, &data->state);

	if (memcmp(&old_state, &data->state, sizeof(struct phy_link_state)) != 0) {
		if (data->state.is_up) {
			LOG_INF("PHY (%p) Link speed 10 Mbps, half duplex\n", base);
		}
		if (data->cb) {
			data->cb(dev, &data->state, data->cb_data);
		}
	}
}

static int phy_nxp_t1s_reset(const struct device *dev)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;

	base->PHYCTRL = TENBASET_PHY_PHYCTRL_RESET_MASK;
	/* TODO */
	return 0;
}

static int phy_nxp_t1s_id(const struct device *dev, uint32_t *phy_id)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;
	uint32_t value;

	value = base->PHYID0;
	*phy_id = value << 16;

	value = base->PHYID1;
	*phy_id |= value;

	return 0;
}

static int phy_nxp_t1s_version(const struct device *dev, struct nxp_t1s_version *version)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;
	uint16_t value;

	value = base->VERSION;

	version->major = (value & TENBASET_PHY_VERSION_MAJORVER_MASK) >>
					  TENBASET_PHY_VERSION_MAJORVER_SHIFT;
	version->minor = (value & TENBASET_PHY_VERSION_MINORVER_MASK) >>
					  TENBASET_PHY_VERSION_MINORVER_SHIFT;
	version->customer = (value & TENBASET_PHY_VERSION_CUSTVER_MASK) >>
						 TENBASET_PHY_VERSION_CUSTVER_SHIFT;

	return 0;
}

static int phy_nxp_t1s_set_plca_cfg(const struct device *dev, struct phy_plca_cfg *plca_cfg)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;

	/* Disable plca before doing the configuration */
	base->PLCACTRL0 &= ~TENBASET_PHY_PLCACTRL0_EN_MASK;

	if (!plca_cfg->enable) {
		/* As the PLCA is disabled above, just return */
		return 0;
	}

	base->PLCACTRL1 = TENBASET_PHY_PLCACTRL1_ID(plca_cfg->node_id) |
					  TENBASET_PHY_PLCACTRL1_NCNT(plca_cfg->node_count);
	base->PLCABURST = TENBASET_PHY_PLCABURST_BTMR(plca_cfg->burst_timer) |
					  TENBASET_PHY_PLCABURST_MAXBC(plca_cfg->burst_count);
	base->PLCATOTMR = TENBASET_PHY_PLCATOTMR_TOTMR(plca_cfg->to_timer);

	/* Enable plca after doing all the configuration */
	base->PLCACTRL0 |= TENBASET_PHY_PLCACTRL0_EN_MASK;

	return 0;
}

static int phy_nxp_t1s_get_plca_cfg(const struct device *dev, struct phy_plca_cfg *plca_cfg)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;
	uint16_t val;

	val = base->PLCAIDVER;

	if ((val & TENBASET_PHY_PLCAIDVER_IDM_MASK) != OATC14_IDM) {
		return -ENODEV;
	}

	plca_cfg->version = (val & TENBASET_PHY_PLCAIDVER_VER_MASK) >>
						 TENBASET_PHY_PLCAIDVER_VER_SHIFT;

	val = base->PLCACTRL0;

	plca_cfg->enable = !!(val & TENBASET_PHY_PLCACTRL0_EN_MASK);

	val = base->PLCACTRL1;

	plca_cfg->node_id = (val & TENBASET_PHY_PLCACTRL1_ID_MASK) >>
						 TENBASET_PHY_PLCACTRL1_ID_SHIFT;
	plca_cfg->node_count = (val & TENBASET_PHY_PLCACTRL1_NCNT_MASK) >>
							TENBASET_PHY_PLCACTRL1_NCNT_SHIFT;

	val = base->PLCABURST;

	plca_cfg->burst_timer = (val & TENBASET_PHY_PLCABURST_BTMR_MASK) >>
							 TENBASET_PHY_PLCABURST_BTMR_SHIFT;
	plca_cfg->burst_count = (val & TENBASET_PHY_PLCABURST_MAXBC_MASK) >>
							 TENBASET_PHY_PLCABURST_MAXBC_SHIFT;

	val = base->PLCATOTMR;

	plca_cfg->to_timer = (val & TENBASET_PHY_PLCATOTMR_TOTMR_MASK) >>
						  TENBASET_PHY_PLCATOTMR_TOTMR_SHIFT;

	return 0;
}

static int phy_nxp_t1s_get_plca_sts(const struct device *dev, bool *plca_sts)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;
	uint16_t val;

	val = base->PLCASTAT;
	*plca_sts = !!(val & TENBASET_PHY_PLCASTAT_PST_MASK);

	return 0;
}

static int phy_nxp_t1s_set_dt_plca(const struct device *dev)
{
	const struct nxp_t1s_config *cfg = dev->config;
	struct phy_plca_cfg plca_cfg;

	if (!cfg->plca->enable) {
		return 0;
	}

	plca_cfg.enable = cfg->plca->enable;
	plca_cfg.node_id = cfg->plca->node_id;
	plca_cfg.node_count = cfg->plca->node_count;
	plca_cfg.burst_count = cfg->plca->burst_count;
	plca_cfg.burst_timer = cfg->plca->burst_timer;
	plca_cfg.to_timer = cfg->plca->to_timer;

	return phy_nxp_t1s_set_plca_cfg(dev, &plca_cfg);
}

static int phy_nxp_t1s_init(const struct device *dev)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;
	struct nxp_t1s_data *data = dev->data;
	int ret;

	data->dev = dev;

	ret = phy_nxp_t1s_reset(dev);
	if (ret) {
		return ret;
	}

	ret = phy_nxp_t1s_id(dev, &data->phy_id);
	if (ret) {
		return ret;
	}

	ret = phy_nxp_t1s_version(dev, &data->version);
	if (ret) {
		return ret;
	}

	base->MODECTRL = NXP_MODECTRL_LINKUP;

	ret = phy_nxp_t1s_set_dt_plca(dev);
	if (ret) {
		return ret;
	}

	k_work_init(&data->phy_isr_work, phy_isr_work_handler);
	phy_isr_work_handler(&data->phy_isr_work);

	base->INTENCLR1 = 0xFFFFU;
	config->irq_config_func();
	base->INTENSET1 = TENBASET_PHY_INTENSET1_PLCASTAT_MASK |
					  TENBASET_PHY_INTENSET1_MODESTAT_MASK;

	return 0;
}

static void phy_nxp_t1s_isr(const struct device *dev)
{
	const struct nxp_t1s_config *config = dev->config;
	TENBASET_PHY_Type *base = config->base;
	struct nxp_t1s_data *data = dev->data;

	base->INTENCAPT1 = base->INTSTAT1;

	k_work_submit(&data->phy_isr_work);
}

static DEVICE_API(ethphy, nxp_t1s_phy_api) = {
	.get_link = phy_nxp_t1s_get_link,
	.cfg_link = NULL,
	.link_cb_set = phy_nxp_t1s_link_cb_set,
	.set_plca_cfg = phy_nxp_t1s_set_plca_cfg,
	.get_plca_cfg = phy_nxp_t1s_get_plca_cfg,
	.get_plca_sts = phy_nxp_t1s_get_plca_sts,
	.read = NULL,
	.write = NULL,
	.read_c45 = NULL,
	.write_c45 = NULL,
};

#define NXP_T1S_PHY_CONNECT_IRQS(node_id, prop, idx)                                     \
	do {                                                                                 \
		IRQ_CONNECT(DT_IRQN_BY_IDX(node_id, idx), DT_IRQ_BY_IDX(node_id, idx, priority), \
		phy_nxp_t1s_isr, DEVICE_DT_GET(node_id), 0);                                     \
		irq_enable(DT_IRQN_BY_IDX(node_id, idx));                                        \
	} while (false);

#define NXP_T1S_PHY_IRQ_CONFIG_FUNC(n)                                                  \
	static void phy_nxp_t1s_##n##_irq_config_func(void)                                 \
	{                                                                                   \
		DT_FOREACH_PROP_ELEM(DT_DRV_INST(n), interrupt_names, NXP_T1S_PHY_CONNECT_IRQS) \
	}

#define NXP_T1S_PHY_INIT_DRIVER(n)                                  \
	static struct nxp_t1s_plca_config nxp_t1s_plca_##n##_config = { \
		.enable = DT_INST_PROP(n, plca_enable),                     \
		.node_id = DT_INST_PROP(n, plca_node_id),                   \
		.node_count = DT_INST_PROP(n, plca_node_count),             \
		.burst_count = DT_INST_PROP(n, plca_burst_count),           \
		.burst_timer = DT_INST_PROP(n, plca_burst_timer),           \
		.to_timer = DT_INST_PROP(n, plca_to_timer),                 \
	};                                                              \
                                                                    \
	static const struct nxp_t1s_config nxp_t1s_##n##_config = {     \
		.plca = &nxp_t1s_plca_##n##_config,                         \
		.base = (TENBASET_PHY_Type *)DT_INST_REG_ADDR(n),           \
		.irq_config_func = phy_nxp_t1s_##n##_irq_config_func,       \
	};                                                              \
                                                                    \
	static struct nxp_t1s_data nxp_t1s_##n##_data;                  \
                                                                    \
	DEVICE_DT_INST_DEFINE(n, &phy_nxp_t1s_init, NULL,               \
							&nxp_t1s_##n##_data,                    \
							&nxp_t1s_##n##_config,                  \
							POST_KERNEL,                            \
							CONFIG_PHY_INIT_PRIORITY,               \
							&nxp_t1s_phy_api);

#define NXP_T1S_PHY_INIT(n)                                         \
	NXP_T1S_PHY_IRQ_CONFIG_FUNC(n)	                                \
	NXP_T1S_PHY_INIT_DRIVER(n)

DT_INST_FOREACH_STATUS_OKAY(NXP_T1S_PHY_INIT);
