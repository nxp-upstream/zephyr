/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup cache_device_interface
 * @brief Device cache infrastructure (system/internal API).
 *
 * This API is primarily intended for:
 * - Cache controller drivers
 * - Platform/board initialization
 * - Power management and SoC bring-up
 * - Low-level driver code that must manage external caches directly
 *
 * Application code should typically use the higher-level sys_cache APIs
 * from include/zephyr/cache.h instead.
 *
 * The cache information exposed by this API aligns with the common
 * Devicetree cache information properties defined in
 * dts/bindings/cacheinfo.yaml. See that schema for property names.
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
#include <zephyr/cache_info.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cache purpose removed: device caches in this model are external by nature. */

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
 */

/** @typedef cache_device_api_enable
 *  @brief API for enabling cache */
typedef int (*cache_device_api_enable)(const struct device *dev);

/** @typedef cache_device_api_disable
 *  @brief API for disabling cache */
typedef int (*cache_device_api_disable)(const struct device *dev);

/** @typedef cache_device_api_flush_range
 *  @brief API for flushing a range in cache */
typedef int (*cache_device_api_flush_range)(const struct device *dev, void *addr, size_t size);

/** @typedef cache_device_api_invalidate_range
 *  @brief API for invalidating a range in cache */
typedef int (*cache_device_api_invalidate_range)(const struct device *dev, void *addr, size_t size);

/** @typedef cache_device_api_flush_and_invalidate_range
 *  @brief API for flushing and invalidating a range in cache */
typedef int (*cache_device_api_flush_and_invalidate_range)(const struct device *dev,
                     void *addr, size_t size);

/** @typedef cache_device_api_flush_all
 *  @brief API for flushing entire cache */
typedef int (*cache_device_api_flush_all)(const struct device *dev);

/** @typedef cache_device_api_invalidate_all
 *  @brief API for invalidating entire cache */
typedef int (*cache_device_api_invalidate_all)(const struct device *dev);

/** @typedef cache_device_api_flush_and_invalidate_all
 *  @brief API for flushing and invalidating entire cache */
typedef int (*cache_device_api_flush_and_invalidate_all)(const struct device *dev);

/** @typedef cache_device_api_get_info
 *  @brief API for getting cache information */
typedef int (*cache_device_api_get_info)(const struct device *dev, struct cache_info *info);

/**
 * @brief Extended cache driver API
 */
__subsystem struct cache_device_driver_api {
    cache_device_api_enable enable;
    cache_device_api_disable disable;
    cache_device_api_flush_range flush_range;
    cache_device_api_invalidate_range invalidate_range;
    cache_device_api_flush_and_invalidate_range flush_and_invalidate_range;
    cache_device_api_flush_all flush_all;
    cache_device_api_invalidate_all invalidate_all;
    cache_device_api_flush_and_invalidate_all flush_and_invalidate_all;
    /** Optional: get cache info */
    cache_device_api_get_info get_info;
};

/** @endcond */

/**
 * @brief Enable cache device
 *
 * @param dev Cache device instance
 *
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -ENOSYS: Operation not implemented.
 *  - -ENOTSUP: Hardware does not support enabling (cache absent).
 *  - -errno: Other failure.
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
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -ENOSYS: Operation not implemented.
 *  - -ENOTSUP: Hardware does not support disabling.
 *  - -errno: Other failure.
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
 * @brief Flush entire cache for a device
 *
 * @param dev Cache device instance
 *
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -ENOSYS: Operation not implemented.
 *  - -ENOTSUP: Hardware does not support full flush.
 *  - -errno: Other failure.
 */
static inline int cache_device_flush_all(const struct device *dev)
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
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -ENOSYS: Operation not implemented.
 *  - -ENOTSUP: Hardware does not support full invalidate.
 *  - -errno: Other failure.
 */
static inline int cache_device_invalidate_all(const struct device *dev)
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
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -ENOSYS: Operation not implemented.
 *  - -ENOTSUP: Hardware does not support combined flush+invalidate.
 *  - -errno: Other failure.
 */
static inline int cache_device_flush_and_invalidate_all(const struct device *dev)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->flush_and_invalidate_all) {
        return -ENOSYS;
    }

    return api->flush_and_invalidate_all(dev);
}

/**
 * @brief Flush cache range for a single device
 *
 * Implementations may return -ERANGE to indicate that a range is
 * outside the device's cacheable window(s); callers typically
 * normalize this to -ENOTSUP when aggregating across devices.
 */
static inline int cache_device_flush_range_dev(const struct device *dev, void *addr, size_t size)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->flush_range) {
        return -ENOSYS;
    }

    return api->flush_range(dev, addr, size);
}

/**
 * @brief Flush cache range (global dispatcher)
 *
 * @param addr Starting address to flush
 * @param size Range size in bytes
 *
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -EINVAL: @p addr is NULL or @p size == 0.
 *  - -ENOTSUP: Range flush not supported, or no device covers @p addr..@p addr+@p size-1.
 *
 * Notes:
 *  - Drivers may use -ERANGE internally to mean "outside this device window",
 *    but the global API SHOULD normalize to -ENOTSUP when no device applies.
 *  - -errno: Other failure.
 */
int cache_device_flush_range(void *addr, size_t size);

/**
 * @brief Invalidate cache range for a single device
 *
 * Implementations may return -ERANGE to indicate that a range is
 * outside the device's cacheable window(s); callers typically
 * normalize this to -ENOTSUP when aggregating across devices.
 */
static inline int cache_device_invalidate_range_dev(const struct device *dev,
                        void *addr, size_t size)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->invalidate_range) {
        return -ENOSYS;
    }

    return api->invalidate_range(dev, addr, size);
}

/**
 * @brief Invalidate cache range (global dispatcher)
 *
 * @param addr Starting address to invalidate
 * @param size Range size in bytes
 *
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -EINVAL: @p addr is NULL or @p size == 0.
 *  - -ENOTSUP: Range invalidate not supported, or no device covers the range.
 *
 * Notes:
 *  - Per-device logic MAY detect out-of-window with -ERANGE, but the global
 *    entrypoint SHOULD return -ENOTSUP when no matching device is found.
 *  - -errno: Other failure.
 */
int cache_device_invalidate_range(void *addr, size_t size);

/**
 * @brief Flush and invalidate cache range for a single device
 *
 * Implementations may return -ERANGE to indicate that a range is
 * outside the device's cacheable window(s); callers typically
 * normalize this to -ENOTSUP when aggregating across devices.
 */
static inline int cache_device_flush_and_invalidate_range_dev(const struct device *dev,
                         void *addr, size_t size)
{
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!api->flush_and_invalidate_range) {
        return -ENOSYS;
    }

    return api->flush_and_invalidate_range(dev, addr, size);
}

/**
 * @brief Flush and invalidate cache range (global dispatcher)
 *
 * @param addr Starting address
 * @param size Range size in bytes
 *
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -EINVAL: @p addr NULL / @p size == 0.
 *  - -ENOTSUP: Combined range op not supported, or no device covers the range.
 *
 * Notes:
 *  - Drivers MAY use -ERANGE internally; the global dispatcher SHOULD report
 *    -ENOTSUP to callers when no applicable device is present.
 *  - -errno: Other failure.
 */
int cache_device_flush_and_invalidate_range(void *addr, size_t size);

/*
 * Optional path-specific global APIs to operate on data (PS) or instruction (PC)
 * paths independently. Implementations may map these to unified operations when
 * the cache is unified.
 */

/* Data-path (PS) global ops */
int cache_device_enable_all(void);
int cache_device_disable_all(void);
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

/**
 * @brief Get cache information for a device cache instance
 *
 * @param dev Cache device instance
 * @param info Pointer to cache info structure to fill
 *
 * Return codes:
 *  - 0: Operation succeeded.
 *  - -EINVAL: @p info is NULL.
 *  - -ENOSYS: Driver did not implement get_info.
 *  - -ENOTSUP: Hardware/instance cannot provide info.
 *  - -errno: Other failure.
 */
__syscall int cache_device_get_info(const struct device *dev, struct cache_info *info);

static inline int z_impl_cache_device_get_info(const struct device *dev, struct cache_info *info)
{
#if !defined(CONFIG_DEVICE_CACHE_INFO)
    ARG_UNUSED(dev);
    ARG_UNUSED(info);
    return -ENOTSUP;
#else
    const struct cache_device_driver_api *api =
        (const struct cache_device_driver_api *)dev->api;

    if (!info) {
        return -EINVAL;
    }

    if (!api->get_info) {
        return -ENOSYS;
    }

    return api->get_info(dev, info);
#endif /* CONFIG_DEVICE_CACHE_INFO */
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#include <zephyr/syscalls/cache_device.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_CACHE_DEVICE_H_ */
