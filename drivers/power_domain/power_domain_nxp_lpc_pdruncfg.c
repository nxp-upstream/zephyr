/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lpc_pdruncfg_power_domain

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(power_domain_nxp_lpc_pdruncfg, CONFIG_POWER_DOMAIN_LOG_LEVEL);

#include <fsl_power.h>

struct nxp_lpc_pdruncfg_pd_config {
  uint32_t pdruncfg_mask;
};

#ifdef CONFIG_PM_DEVICE_POWER_DOMAIN

struct pd_visitor_context {
  const struct device *domain;
  enum pm_device_action action;
};

static int pd_on_domain_visitor(const struct device *dev, void *context)
{
  struct pd_visitor_context *visitor_context = context;

 /* Only run action if the device is on the specified domain */
  if (!dev->pm || (dev->pm_base->domain != visitor_context->domain)) {
    return 0;
  }

  (void)pm_device_action_run(dev, visitor_context->action);
  return 0;
}

#endif

static int nxp_lpc_pdruncfg_pd_pm_action(const struct device *dev, enum pm_device_action action)
{
#ifdef CONFIG_PM_DEVICE_POWER_DOMAIN
  struct pd_visitor_context context = { .domain = dev };
#endif
  const struct nxp_lpc_pdruncfg_pd_config *cfg = dev->config;
  int rc = 0;

  switch (action) {
  case PM_DEVICE_ACTION_RESUME:
    /* Disable power-down => power domain ON */
    POWER_DisablePD(cfg->pdruncfg_mask);
    LOG_DBG("%s ON (mask 0x%08x)", dev->name, cfg->pdruncfg_mask);
#ifdef CONFIG_PM_DEVICE_POWER_DOMAIN
    context.action = PM_DEVICE_ACTION_TURN_ON;
    (void)device_supported_foreach(dev, pd_on_domain_visitor, &context);
#endif
    break;
  case PM_DEVICE_ACTION_SUSPEND:
#ifdef CONFIG_PM_DEVICE_POWER_DOMAIN
    context.action = PM_DEVICE_ACTION_TURN_OFF;
    (void)device_supported_foreach(dev, pd_on_domain_visitor, &context);
#endif
    /* Enable power-down => power domain OFF */
    POWER_EnablePD(cfg->pdruncfg_mask);
    LOG_DBG("%s OFF (mask 0x%08x)", dev->name, cfg->pdruncfg_mask);
    break;
  case PM_DEVICE_ACTION_TURN_ON:
  case PM_DEVICE_ACTION_TURN_OFF:
    /* No additional per-device handling required */
    break;
  default:
    rc = -ENOTSUP;
  }

  return rc;
}

static int nxp_lpc_pdruncfg_pd_init(const struct device *dev)
{
  return pm_device_driver_init(dev, nxp_lpc_pdruncfg_pd_pm_action);
}

#define NXP_LPC_PDRUNCFG_PD_DEVICE(inst)
	static const struct nxp_lpc_pdruncfg_pd_config nxp_lpc_pdruncfg_pd_cfg_##inst = {
		.pdruncfg_mask = DT_INST_PROP(inst, nxp_pdruncfg_mask),
	};
	PM_DEVICE_DT_INST_DEFINE(inst, nxp_lpc_pdruncfg_pd_pm_action);
	DEVICE_DT_INST_DEFINE(inst, nxp_lpc_pdruncfg_pd_init, PM_DEVICE_DT_INST_GET(inst),
			     NULL, &nxp_lpc_pdruncfg_pd_cfg_##inst,
			     POST_KERNEL, CONFIG_POWER_DOMAIN_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(NXP_LPC_PDRUNCFG_PD_DEVICE)
