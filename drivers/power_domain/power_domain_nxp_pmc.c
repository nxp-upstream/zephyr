/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * NXP PMC power-rail controller driver.
 *
 * The PMC owns an array of "power down" bits in PDRUNCFG0..PDRUNCFGn.
 * Clearing a bit powers the corresponding rail up; setting a bit powers
 * it down. A separate APPLYCFG handshake commits the new configuration
 * to the analog power island. This driver models each rail as an
 * individual Zephyr power-domain device (a leaf compatible with
 * "power-domain"), so peripherals consume them with the standard
 * upstream "power-domains = <&rail>" devicetree property and the
 * pm_device_runtime API.
 *
 * Devicetree layout (mirrors nordic,nrfs-gdpwr):
 *
 *     pmc0: pmc@20f000 {
 *         compatible = "nxp,pmc-power-rail-controller";
 *         reg = <0x20f000 0x1000>;
 *
 *         pmc_apd_xspi0: apd-xspi0 {
 *             #power-domain-cells = <0>;
 *             nxp,pdruncfg-bit = <4 18>;
 *         };
 *         ...
 *     };
 *
 *     &xspi0 {
 *         power-domains = <&pmc_apd_xspi0>, <&pmc_ppd_xspi0>;
 *     };
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pmc_power_rail, CONFIG_POWER_DOMAIN_LOG_LEVEL);

#define DT_DRV_COMPAT nxp_pmc_power_rail_controller

/* PMC register map (offsets from DT reg base). */
#define NXP_PMC_STATUS_OFFSET      0x4U
#define NXP_PMC_CTRL_OFFSET        0xCU
#define NXP_PMC_PDRUNCFG0_OFFSET   0xA0U

#define NXP_PMC_STATUS_BUSY_MASK   BIT(0)
#define NXP_PMC_CTRL_APPLYCFG_MASK BIT(0)

#define NXP_PMC_BITS_PER_PDRUNCFG  32U

/*
 * Controller state. Shared by every child rail under the same
 * controller instance: child devices look up their controller via
 * DEVICE_DT_GET(DT_PARENT(...)).
 */
struct nxp_pmc_ctrl_config {
	mm_reg_t base;
};

struct nxp_pmc_ctrl_data {
	struct k_spinlock lock;
};

/* One rail (PDRUNCFG[idx], bit). */
struct nxp_pmc_rail_config {
	const struct device *ctrl;
	uint8_t pdruncfg_idx;
	uint8_t bit;
};

static inline mm_reg_t pmc_pdruncfg_addr(const struct nxp_pmc_ctrl_config *cfg, uint8_t idx)
{
	return cfg->base + NXP_PMC_PDRUNCFG0_OFFSET + (idx * 4U);
}

static void pmc_apply(const struct nxp_pmc_ctrl_config *cfg)
{
	while ((sys_read32(cfg->base + NXP_PMC_STATUS_OFFSET) & NXP_PMC_STATUS_BUSY_MASK) != 0U) {
	}
	sys_set_bits(cfg->base + NXP_PMC_CTRL_OFFSET, NXP_PMC_CTRL_APPLYCFG_MASK);
	while ((sys_read32(cfg->base + NXP_PMC_STATUS_OFFSET) & NXP_PMC_STATUS_BUSY_MASK) != 0U) {
	}
}

static void pmc_rail_set(const struct device *rail, bool power_on)
{
	const struct nxp_pmc_rail_config *rcfg = rail->config;
	const struct nxp_pmc_ctrl_config *ccfg = rcfg->ctrl->config;
	struct nxp_pmc_ctrl_data *cdata = rcfg->ctrl->data;
	mm_reg_t addr = pmc_pdruncfg_addr(ccfg, rcfg->pdruncfg_idx);
	uint32_t mask = BIT(rcfg->bit);
	k_spinlock_key_t key;

	key = k_spin_lock(&cdata->lock);
	if (power_on) {
		sys_clear_bits(addr, mask);
	} else {
		sys_set_bits(addr, mask);
	}
	pmc_apply(ccfg);
	k_spin_unlock(&cdata->lock, key);
}

static int pmc_rail_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		pmc_rail_set(dev, true);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		pmc_rail_set(dev, false);
		break;
	case PM_DEVICE_ACTION_TURN_ON:
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int pmc_rail_init(const struct device *dev)
{
	int ret;

	/*
	 * Rails are assumed to be in their reset state (powered down).
	 * Initialize the pm_device as suspended so pm_device_runtime_get()
	 * triggers a real RESUME -> pmc_rail_set(true) on first claim.
	 */
	ret = pm_device_driver_init(dev, pmc_rail_pm_action);
	if (ret < 0) {
		return ret;
	}

	return pm_device_runtime_enable(dev);
}

static int pmc_ctrl_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

/* Controller: one device per "nxp,pmc-power-rail-controller" node. */
#define PMC_CTRL_DEFINE(inst)                                                                      \
	static const struct nxp_pmc_ctrl_config pmc_ctrl_cfg_##inst = {                            \
		.base = DT_INST_REG_ADDR(inst),                                                    \
	};                                                                                         \
	static struct nxp_pmc_ctrl_data pmc_ctrl_data_##inst;                                      \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, pmc_ctrl_init, NULL, &pmc_ctrl_data_##inst,                    \
			      &pmc_ctrl_cfg_##inst, PRE_KERNEL_1,                                  \
			      CONFIG_POWER_DOMAIN_NXP_PMC_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PMC_CTRL_DEFINE)

/* Child rails: enumerated under each controller instance. */
#define PMC_RAIL_DEFINE(node_id)                                                                   \
	BUILD_ASSERT(DT_PROP_BY_IDX(node_id, nxp_pdruncfg_bit, 1) < NXP_PMC_BITS_PER_PDRUNCFG,     \
		     "PMC rail bit out of range");                                                 \
	static const struct nxp_pmc_rail_config pmc_rail_cfg_##node_id = {                         \
		.ctrl = DEVICE_DT_GET(DT_PARENT(node_id)),                                         \
		.pdruncfg_idx = DT_PROP_BY_IDX(node_id, nxp_pdruncfg_bit, 0),                      \
		.bit = DT_PROP_BY_IDX(node_id, nxp_pdruncfg_bit, 1),                               \
	};                                                                                         \
	PM_DEVICE_DT_DEFINE(node_id, pmc_rail_pm_action);                                          \
	DEVICE_DT_DEFINE(node_id, pmc_rail_init, PM_DEVICE_DT_GET(node_id), NULL,                  \
			 &pmc_rail_cfg_##node_id, PRE_KERNEL_1,                                    \
			 CONFIG_POWER_DOMAIN_NXP_PMC_INIT_PRIORITY, NULL);

#define PMC_INST_FOREACH_CHILD(inst) DT_INST_FOREACH_CHILD(inst, PMC_RAIL_DEFINE)

DT_INST_FOREACH_STATUS_OKAY(PMC_INST_FOREACH_CHILD)

