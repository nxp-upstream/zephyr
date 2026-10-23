/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_pdcon_power_domain

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <fsl_power.h>
#include <soc_clock.h>

LOG_MODULE_REGISTER(nxp_pdcon_pd, CONFIG_POWER_DOMAIN_LOG_LEVEL);

/* domain-name is the power_domain_t member suffix: "Npu" is kPOWER_DomainNpu. */
#define PDCON_PD_DOMAIN(inst) _CONCAT(kPOWER_Domain, DT_INST_STRING_TOKEN(inst, domain_name))

struct pdcon_pd_config {
	power_domain_t domain;
};

static int pdcon_pd_status_to_errno(status_t status)
{
	switch (status) {
	case kStatus_Success:
		return 0;
	case kStatus_InvalidArgument:
		return -EINVAL;
	case kStatus_Timeout:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static int pdcon_pd_set_event(const struct device *dev, pdcon_event_t event)
{
	const struct pdcon_pd_config *config = dev->config;
	int ret = pdcon_pd_status_to_errno(POWER_SetDomainRunMode(config->domain, event));

	if (ret < 0) {
		LOG_ERR("%s: PDCON event %d failed (%d)", dev->name, (int)event, ret);
	}

	return ret;
}

static int pdcon_pd_power_on(const struct device *dev)
{
	const struct pdcon_pd_config *config = dev->config;
	int ret;

	ret = pdcon_pd_set_event(dev, kPDCON_EventNoneOrActive);
	if (ret < 0) {
		return ret;
	}

	/* Powering the domain off reset the roots soc_clock_init() wrote into its CCM. */
	soc_clock_restore_domain_roots(config->domain);

	pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_ON, NULL);

	return 0;
}

static int pdcon_pd_power_off(const struct device *dev)
{
	pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_OFF, NULL);

	return pdcon_pd_set_event(dev, kPDCON_EventPowerOff);
}

static int pdcon_pd_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return pdcon_pd_power_on(dev);
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_TURN_OFF:
		return pdcon_pd_power_off(dev);
	case PM_DEVICE_ACTION_TURN_ON:
		/* The domain is powered on by RESUME, which follows. */
		return 0;
	default:
		return -ENOTSUP;
	}
}

/*
 * Runtime PM keeps the domain out of the suspend pm_suspend_devices() does on a
 * system power state transition, so its state is the one its consumers left it
 * in: the one the SoC power code reads with pm_device_state_get(). Enabling it
 * suspends a domain nobody holds right away, so unused domains are off from
 * boot.
 */
static int pdcon_pd_init(const struct device *dev)
{
	return pm_device_runtime_enable(dev);
}

#define PDCON_PD_DEFINE(inst)                                                                      \
	static const struct pdcon_pd_config pdcon_pd_config_##inst = {                             \
		.domain = PDCON_PD_DOMAIN(inst),                                                   \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, pdcon_pd_pm_action);                                        \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, pdcon_pd_init, PM_DEVICE_DT_INST_GET(inst), NULL,              \
			      &pdcon_pd_config_##inst, PRE_KERNEL_1,                               \
			      CONFIG_POWER_DOMAIN_NXP_PDCON_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PDCON_PD_DEFINE)
