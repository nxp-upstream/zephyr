/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(CONFIG_ARM_SCMI_REBOOT)

#include <zephyr/drivers/firmware/scmi/system.h>
#include <string.h>

#if defined(CONFIG_REBOOT)
#include <zephyr/sys/reboot.h>
#endif
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(scmi_reboot, CONFIG_PM_LOG_LEVEL);

static int scmi_reboot_handler(int type)
{
	struct scmi_system_power_state_config cfg;
	int ret;

	cfg.flags = SCMI_SYSTEM_POWER_FLAG_FORCEFUL;

	switch (type) {
	case SYS_REBOOT_WARM:
		cfg.system_state = SCMI_SYSTEM_POWER_STATE_WARM_RESET;
		break;
	case SYS_REBOOT_COLD:
		cfg.system_state = SCMI_SYSTEM_POWER_STATE_COLD_RESET;
		break;
	default:
		LOG_ERR("Unsupported reboot type: %d", type);
		return -EINVAL;
	}

	ret = scmi_system_power_state_set(&cfg);
	if (ret < 0) {
		LOG_ERR("System reboot failed with error: %d", ret);
	}

	return ret;
}

#if defined(CONFIG_REBOOT)
void sys_arch_reboot(int type)
{
	scmi_reboot_handler(type);
}
#endif

static int scmi_reboot_init(void)
{
	int ret;
	uint32_t version, mesg_attr;

	/* Query SCMI system protocol version */
	ret = scmi_system_protocol_version(&version);
	if (ret < 0) {
		LOG_WRN("Failed to query SCMI system protocol version: %d", ret);
		return 0;
	}

	LOG_INF("SCMI system protocol version: 0x%08x", version);

	ret = scmi_system_protocol_message_attributes(SCMI_SYSTEM_MSG_POWER_STATE_SET, &mesg_attr);
	if (ret < 0) {
		LOG_WRN("Failed to query SCMI system Msg Attr: %d", ret);
		return 0;
	}

	if (mesg_attr & SCMI_SYSTEM_MSG_ATTR_WARM_RESET) {
		LOG_INF("Warm reset supported");
	}

	if (mesg_attr & SCMI_SYSTEM_MSG_ATTR_SUSPEND) {
		LOG_INF("System suspend supported");
	}

	return 0;
}

SYS_INIT(scmi_reboot_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_ARM_SCMI_REBOOT */
