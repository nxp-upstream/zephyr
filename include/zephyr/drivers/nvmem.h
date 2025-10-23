/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup nvmem_driver_interface
 * @brief NVMEM provider driver API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_NVMEM_H_
#define ZEPHYR_INCLUDE_DRIVERS_NVMEM_H_

/**
 * @brief Interfaces for NVMEM provider devices (EEPROM, OTP/eFuse, etc.).
 * @defgroup nvmem_driver_interface NVMEM providers
 * @since 4.4
 * @version 0.1.0
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/types.h>
#include <sys/types.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
 */

/** @brief Callback to read from an NVMEM provider. */
typedef int (*nvmem_api_read_t)(const struct device *dev, off_t offset,
				void *buf, size_t len);

/** @brief Callback to write to an NVMEM provider. */
typedef int (*nvmem_api_write_t)(const struct device *dev, off_t offset,
				const void *buf, size_t len);

/** @brief Callback to get total size from an NVMEM provider. */
typedef size_t (*nvmem_api_size_t)(const struct device *dev);

__subsystem struct nvmem_driver_api {
	nvmem_api_read_t read;
	nvmem_api_write_t write;
	nvmem_api_size_t size;
};

/** @endcond */

/**
 * @brief Read bytes from an NVMEM provider device.
 *
 * Providers should implement device-specific semantics and alignment rules.
 *
 * @param dev		NVMEM provider device instance.
 * @param offset	Start offset within the provider address space.
 * @param buf		Destination buffer.
 * @param len		Number of bytes to read.
 *
 * @return 0 on success, negative errno on failure.
 */
__syscall int nvmem_read(const struct device *dev, off_t offset,
				void *buf, size_t len);

static inline int z_impl_nvmem_read(const struct device *dev, off_t offset,
				void *buf, size_t len)
{
	return DEVICE_API_GET(nvmem, dev)->read(dev, offset, buf, len);
}

/**
 * @brief Write bytes to an NVMEM provider device.
 *
 * Providers must enforce their semantics (e.g., OTP 1->0 only) and return
 * -EROFS for read-only configurations.
 *
 * @param dev		NVMEM provider device instance.
 * @param offset	Start offset within the provider address space.
 * @param buf		Source buffer.
 * @param len		Number of bytes to write.
 *
 * @return 0 on success, negative errno on failure.
 */
__syscall int nvmem_write(const struct device *dev, off_t offset,
				const void *buf, size_t len);
static inline int z_impl_nvmem_write(const struct device *dev, off_t offset,
				const void *buf, size_t len)
{
	return DEVICE_API_GET(nvmem, dev)->write(dev, offset, buf, len);
}

/**
 * @brief Get total size in bytes of an NVMEM provider device.
 *
 * @param dev	NVMEM provider device instance.
 *
 * @return Size in bytes.
 */
__syscall size_t nvmem_get_size(const struct device *dev);
static inline size_t z_impl_nvmem_get_size(const struct device *dev)
{
	return DEVICE_API_GET(nvmem, dev)->size(dev);
}

#ifdef __cplusplus
}
#endif

/** @} */

#include <zephyr/syscalls/nvmem.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_NVMEM_H_ */
