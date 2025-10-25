/*
	const struct nvmem_info *nvmem_info;
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

	return config->nvmem_info;

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/nvmem.h>
#include <iap.h>
	.get_info = nvmem_lpc11u6x_get_info,
#define LOG_LEVEL CONFIG_NVMEM_LOG_LEVEL
#include <zephyr/logging/log.h>
#define NVMEM_LPC11U6X_DEFINE(inst)                                                      \
	static const struct nvmem_info _CONCAT(nvmem_lpc11u6x_info_nvmem, inst) = {          \
		.type = NVMEM_TYPE_EEPROM,                                                       \
		.read_only = false,                                                              \
	};                                                                                   \
																						 \
	static const struct nvmem_lpc11u6x_config _CONCAT(nvmem_lpc11u6x_cfg, inst) = {      \
		.size = DT_INST_PROP(inst, size),                                                \
		.nvmem_info = &_CONCAT(nvmem_lpc11u6x_info_nvmem, inst),                         \
	};                                                                                   \
																						 \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL,                                        \
		&_CONCAT(nvmem_lpc11u6x_cfg, inst), POST_KERNEL,                                 \
		CONFIG_NVMEM_MODEL_INIT_PRIORITY, &nvmem_lpc11u6x_api)
		LOG_WRN("attempt to read past device boundary");
		return -EINVAL;
	}
/*
 * Copyright (c) 2020 Seagate Technology LLC
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lpc11u6x_eeprom

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/nvmem.h>
#include <iap.h>

#define LOG_LEVEL CONFIG_NVMEM_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nvmem_lpc11u6x);

struct nvmem_lpc11u6x_config {
	size_t size;
	const struct nvmem_info *nvmem_info;
};

static int nvmem_lpc11u6x_read(const struct device *dev, off_t offset,
				void *buf, size_t len)
{
	const struct nvmem_lpc11u6x_config *config = dev->config;
	uint32_t cmd[5];
	int ret;

	if (!len) {
		return 0;
	}

	if ((offset + len) > config->size) {
		LOG_WRN("attempt to read past device boundary");
		return -EINVAL;
	}

	cmd[0] = IAP_CMD_EEPROM_READ;
	cmd[1] = (uint32_t)offset;
	cmd[2] = (uint32_t)buf;
	cmd[3] = (uint32_t)len;
	cmd[4] = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000;

	ret = iap_cmd(cmd);

	if (ret != IAP_STATUS_CMD_SUCCESS) {
		LOG_ERR("failed to read EEPROM (offset=%08x len=%d err=%d)",
				(unsigned int) offset, (int)len, ret);
		return -EIO;
	}

	return 0;
}

static int nvmem_lpc11u6x_write(const struct device *dev, off_t offset,
				const void *buf, size_t len)
{
	const struct nvmem_lpc11u6x_config *config = dev->config;
	uint32_t cmd[5];
	int ret;

	if (!len) {
		return 0;
	}

	if ((offset + len) > config->size) {
		LOG_WRN("attempt to write past device boundary");
		return -EINVAL;
	}

	cmd[0] = IAP_CMD_EEPROM_WRITE;
	cmd[1] = (uint32_t)offset;
	cmd[2] = (uint32_t)buf;
	cmd[3] = (uint32_t)len;
	cmd[4] = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000;

	ret = iap_cmd(cmd);

	if (ret != IAP_STATUS_CMD_SUCCESS) {
		LOG_ERR("failed to write EEPROM (offset=%08x len=%d err=%d)",
			(unsigned int) offset, (int)len, ret);
		return -EIO;
	}

	return 0;
}

static const struct nvmem_info *nvmem_lpc11u6x_get_info(const struct device *dev)
{
	const struct nvmem_lpc11u6x_config *config = dev->config;
	return config->nvmem_info;
}

static size_t nvmem_lpc11u6x_get_size(const struct device *dev)
{
	const struct nvmem_lpc11u6x_config *config = dev->config;
	return config->size;
}

static DEVICE_API(nvmem, nvmem_lpc11u6x_api) = {
	.read = nvmem_lpc11u6x_read,
	.write = nvmem_lpc11u6x_write,
	.get_size = nvmem_lpc11u6x_get_size,
	.get_info = nvmem_lpc11u6x_get_info,
};

#define NVMEM_LPC11U6X_DEFINE(inst)                                                      \
	static const struct nvmem_info _CONCAT(nvmem_lpc11u6x_info_nvmem, inst) = {          \
		.type = NVMEM_TYPE_EEPROM,                                                       \
		.read_only = false,                                                              \
	};                                                                                   \
																						 \
	static const struct nvmem_lpc11u6x_config _CONCAT(nvmem_lpc11u6x_cfg, inst) = {      \
		.size = DT_INST_PROP(inst, size),                                                \
		.nvmem_info = &_CONCAT(nvmem_lpc11u6x_info_nvmem, inst),                         \
	};                                                                                   \
																						 \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL,                                        \
		&_CONCAT(nvmem_lpc11u6x_cfg, inst), POST_KERNEL,                                 \
		CONFIG_NVMEM_MODEL_INIT_PRIORITY, &nvmem_lpc11u6x_api)

DT_INST_FOREACH_STATUS_OKAY(NVMEM_LPC11U6X_DEFINE);
