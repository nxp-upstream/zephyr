/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_cache_device

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/cache_device.h>
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/logging/log.h>
#include <fsl_cache.h>

LOG_MODULE_REGISTER(cache_device_nxp_cache64, CONFIG_CACHE_LOG_LEVEL);

struct cache64_data {
    bool enabled;
};

struct cache64_config {
    CACHE64_CTRL_Type *base;
};

static int cache64_enable(const struct device *dev)
{
    const struct cache64_config *config = dev->config;
    struct cache64_data *data = dev->data;

    LOG_DBG("Enabling Cache64");
    CACHE64_EnableCache(config->base);
    data->enabled = true;
    return 0;
}

static int cache64_disable(const struct device *dev)
{
    const struct cache64_config *config = dev->config;
    struct cache64_data *data = dev->data;

    LOG_DBG("Disabling Cache64");
    CACHE64_DisableCache(config->base);
    data->enabled = false;
    return 0;
}

static int cache64_invalidate_all(const struct device *dev)
{
    const struct cache64_config *config = dev->config;

    LOG_DBG("Cache64 invalidate all");
    CACHE64_InvalidateCache(config->base);
    return 0;
}

static int cache64_flush_all(const struct device *dev)
{
    const struct cache64_config *config = dev->config;

    LOG_DBG("Cache64 flush all");
    CACHE64_CleanCache(config->base);
    return 0;
}

static int cache64_flush_and_invalidate_all(const struct device *dev)
{
    const struct cache64_config *config = dev->config;

    LOG_DBG("Cache64 flush and invalidate all");
    CACHE64_CleanInvalidateCache(config->base);
    return 0;
}

/* Global range operations: Cache64 deduces instance from the address window */
int cache_device_invalidate_range(void *addr, size_t size)
{
    LOG_DBG("Cache64 invalidate range: 0x%08x, size: %zu", (uint32_t)addr, size);
    CACHE64_InvalidateCacheByRange((uint32_t)addr, size);
    return 0;
}

int cache_device_flush_range(void *addr, size_t size)
{
    LOG_DBG("Cache64 flush range: 0x%08x, size: %zu", (uint32_t)addr, size);
    CACHE64_CleanCacheByRange((uint32_t)addr, size);
    return 0;
}

int cache_device_flush_and_invalidate_range(void *addr, size_t size)
{
    LOG_DBG("Cache64 flush and invalidate range: 0x%08x, size: %zu", (uint32_t)addr, size);
    CACHE64_CleanInvalidateCacheByRange((uint32_t)addr, size);
    return 0;
}

/* Path-specific global ops: Cache64 is unified, so map data/instr to unified ops */
int cache_device_data_flush_all(void)
{
    int ret = 0;
#define CACHE64_DO_FLUSH_ALL(inst) \
    ret |= cache64_flush_all(DEVICE_DT_INST_GET(inst));
    DT_INST_FOREACH_STATUS_OKAY(CACHE64_DO_FLUSH_ALL);
#undef CACHE64_DO_FLUSH_ALL
    return ret;
}

int cache_device_data_invalidate_all(void)
{
    int ret = 0;
#define CACHE64_DO_INV_ALL(inst) \
    ret |= cache64_invalidate_all(DEVICE_DT_INST_GET(inst));
    DT_INST_FOREACH_STATUS_OKAY(CACHE64_DO_INV_ALL);
#undef CACHE64_DO_INV_ALL
    return ret;
}

int cache_device_data_flush_and_invalidate_all(void)
{
    int ret = 0;
#define CACHE64_DO_FLUSH_INV_ALL(inst) \
    ret |= cache64_flush_and_invalidate_all(DEVICE_DT_INST_GET(inst));
    DT_INST_FOREACH_STATUS_OKAY(CACHE64_DO_FLUSH_INV_ALL);
#undef CACHE64_DO_FLUSH_INV_ALL
    return ret;
}

int cache_device_instr_flush_all(void)
{
    return cache_device_data_flush_all();
}

int cache_device_instr_invalidate_all(void)
{
    return cache_device_data_invalidate_all();
}

int cache_device_instr_flush_and_invalidate_all(void)
{
    return cache_device_data_flush_and_invalidate_all();
}

int cache_device_data_flush_range(void *addr, size_t size)
{
    return cache_device_flush_range(addr, size);
}

int cache_device_data_invalidate_range(void *addr, size_t size)
{
    return cache_device_invalidate_range(addr, size);
}

int cache_device_data_flush_and_invalidate_range(void *addr, size_t size)
{
    return cache_device_flush_and_invalidate_range(addr, size);
}

int cache_device_instr_flush_range(void *addr, size_t size)
{
    return cache_device_flush_range(addr, size);
}

int cache_device_instr_invalidate_range(void *addr, size_t size)
{
    return cache_device_invalidate_range(addr, size);
}

int cache_device_instr_flush_and_invalidate_range(void *addr, size_t size)
{
    return cache_device_flush_and_invalidate_range(addr, size);
}

/* get_info removed from minimal model; static characteristics unused. */

static int cache64_init(const struct device *dev)
{
    struct cache64_data *data = dev->data;

    data->enabled = false;
    LOG_INF("Cache64 initialized");

    return 0;
}

/* Cache64 device-specific API */
static const struct cache_device_driver_api cache64_api = {
    .enable = cache64_enable,
    .disable = cache64_disable,
    .invalidate_all = cache64_invalidate_all,
    .flush_all = cache64_flush_all,
    .flush_and_invalidate_all = cache64_flush_and_invalidate_all,
};

/* Device instantiation */
#define CACHE64_DEVICE(inst)                                                  \
    static const struct cache64_config cache64_config_##inst = {             \
        .base = (CACHE64_CTRL_Type *)DT_INST_REG_ADDR(inst),                     \
    };                                                                       \
    static struct cache64_data cache64_data_##inst;                         \
    DEVICE_DT_INST_DEFINE(inst, cache64_init, NULL,                         \
                          &cache64_data_##inst, &cache64_config_##inst,     \
                          POST_KERNEL, CONFIG_CACHE_DEVICE_INIT_PRIORITY,          \
                          &cache64_api);

DT_INST_FOREACH_STATUS_OKAY(CACHE64_DEVICE)
