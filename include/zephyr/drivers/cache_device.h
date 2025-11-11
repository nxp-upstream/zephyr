/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup cache_device_interface
 * @brief Extended cache controller driver API for device-specific operations.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CACHE_DEVICE_H_
#define ZEPHYR_INCLUDE_DRIVERS_CACHE_DEVICE_H_

/**
 * @brief Extended interfaces for cache controllers.
 * @defgroup cache_device_interface Device-Specific Cache Controller
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cache type definitions
 * @name Cache Types
 * @{
 */
#define CACHE_DEVICE_TYPE_INSTRUCTION  BIT(0)
#define CACHE_DEVICE_TYPE_DATA        BIT(1)
#define CACHE_DEVICE_TYPE_UNIFIED     (CACHE_DEVICE_TYPE_INSTRUCTION | CACHE_DEVICE_TYPE_DATA)
/** @} */

/**
 * @brief Cache purpose definitions
 * @name Cache Purpose
 * @{
 */
#define CACHE_DEVICE_PURPOSE_INTERNAL_FLASH   BIT(0)
#define CACHE_DEVICE_PURPOSE_EXTERNAL_FLASH   BIT(1)
#define CACHE_DEVICE_PURPOSE_SYSTEM          BIT(2)
/** @} */

/* Cache information structure removed to keep API minimal. */

/* -------------------------------------------------------------------------
 * Region-based cache policy (simple API)
 * ------------------------------------------------------------------------- */

/* Per-range cache policy. */
enum cache_device_region_policy {
    CACHE_DEVICE_POLICY_NONCACHEABLE = 0,  /* Bypass the cache */
    CACHE_DEVICE_POLICY_WRITE_THROUGH = 1, /* Cacheable, writes propagate */
    CACHE_DEVICE_POLICY_WRITE_BACK    = 2, /* Cacheable, write-back */
};

/**
 * @brief Set cache policy for a memory range.
 *
 * Program the device-side cache policy for [start, start+length-1].
 * Implementations may align start/length to hardware granularity and may
 * coalesce with existing regions as required by the controller.
 *
 * Boundary semantics follow typical device caches (e.g., CACHE64): the
 * boundary address is part of the upper region when partitioning.
 *
 * @param start  Start system address.
 * @param length Length in bytes; must be > 0.
 * @param policy Desired cache policy for the range.
 * @return 0 on success, -EINVAL on invalid args, -ENOTSUP if unsupported.
 */
int cache_device_set_range_policy(uintptr_t start,
                                  size_t length,
                                  enum cache_device_region_policy policy);

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
 */

/**
 * @typedef cache_device_api_enable
 * @brief API for enabling cache
 */
typedef int (*cache_device_api_enable)(const struct device *dev);

/**
 * @typedef cache_device_api_disable
 * @brief API for disabling cache
 */
typedef int (*cache_device_api_disable)(const struct device *dev);

/**
 * @typedef cache_device_api_flush_all
 * @brief API for flushing entire cache
 */
typedef int (*cache_device_api_flush_all)(const struct device *dev);

/**
 * @typedef cache_device_api_invalidate_all
 * @brief API for invalidating entire cache
 */
typedef int (*cache_device_api_invalidate_all)(const struct device *dev);

/**
 * @typedef cache_device_api_flush_and_invalidate_all
 * @brief API for flushing and invalidating entire cache
 */
typedef int (*cache_device_api_flush_and_invalidate_all)(const struct device *dev);

/*
 * Remove per-device range callbacks; range ops become global APIs.
 * Implementations may internally dispatch to the right cache instance
 * based on the address window.
 */

/* get_info callback removed. */

/**
 * @brief Extended cache driver API
 */
__subsystem struct cache_device_driver_api {
    cache_device_api_enable enable;
    cache_device_api_disable disable;
    cache_device_api_flush_all flush_all;
    cache_device_api_invalidate_all invalidate_all;
    cache_device_api_flush_and_invalidate_all flush_and_invalidate_all;
    /* Range callbacks removed from per-device API; get_info removed. */
};

/** @endcond */

/**
 * @brief Enable cache device
 *
 * @param dev Cache device instance
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
__syscall int cache_device_enable(const struct device *dev);

static inline int z_impl_cache_device_enable(const struct device *dev)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->enable) {
        return -ENOSYS;
    }

    return api->enable(dev);
}

/**
 * @brief Disable cache device
 *
 * @param dev Cache device instance
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
__syscall int cache_device_disable(const struct device *dev);

static inline int z_impl_cache_device_disable(const struct device *dev)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->disable) {
        return -ENOSYS;
    }

    return api->disable(dev);
}

/**
 * @brief Flush entire cache
 *
 * @param dev Cache device instance
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
__syscall int cache_device_flush_all(const struct device *dev);

static inline int z_impl_cache_device_flush_all(const struct device *dev)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->flush_all) {
        return -ENOSYS;
    }

    return api->flush_all(dev);
}

/**
 * @brief Invalidate entire cache
 *
 * @param dev Cache device instance
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
__syscall int cache_device_invalidate_all(const struct device *dev);

static inline int z_impl_cache_device_invalidate_all(const struct device *dev)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->invalidate_all) {
        return -ENOSYS;
    }

    return api->invalidate_all(dev);
}

/**
 * @brief Flush and invalidate entire cache
 *
 * @param dev Cache device instance
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
__syscall int cache_device_flush_and_invalidate_all(const struct device *dev);

static inline int z_impl_cache_device_flush_and_invalidate_all(const struct device *dev)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->flush_and_invalidate_all) {
        return -ENOSYS;
    }

    return api->flush_and_invalidate_all(dev);
}

/**
 * @brief Flush cache range (global)
 *
 * @param addr Starting address to flush
 * @param size Range size in bytes
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
int cache_device_flush_range(void *addr, size_t size);

/**
 * @brief Invalidate cache range (global)
 *
 * @param addr Starting address to invalidate
 * @param size Range size in bytes
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
int cache_device_invalidate_range(void *addr, size_t size);

/**
 * @brief Flush and invalidate cache range (global)
 *
 * @param addr Starting address
 * @param size Range size in bytes
 *
 * @return 0 if successful
 * @return -ENOTSUP if operation not supported
 * @return -errno code if failure
 */
int cache_device_flush_and_invalidate_range(void *addr, size_t size);

/*
 * Optional path-specific global APIs to operate on data (PS) or instruction (PC)
 * paths independently. Implementations may map these to unified operations when
 * the cache is unified.
 */

/* Data-path (PS) global ops */
int cache_device_data_flush_all(void);
int cache_device_data_invalidate_all(void);
int cache_device_data_flush_and_invalidate_all(void);

int cache_device_data_flush_range(void *addr, size_t size);
int cache_device_data_invalidate_range(void *addr, size_t size);
int cache_device_data_flush_and_invalidate_range(void *addr, size_t size);

/* Instruction-path (PC) global ops */
int cache_device_instr_flush_all(void);
int cache_device_instr_invalidate_all(void);
int cache_device_instr_flush_and_invalidate_all(void);

int cache_device_instr_flush_range(void *addr, size_t size);
int cache_device_instr_invalidate_range(void *addr, size_t size);
int cache_device_instr_flush_and_invalidate_range(void *addr, size_t size);

/* cache_device_get_info() removed. */

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#include <zephyr/syscalls/cache_device.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_CACHE_DEVICE_H_ */
