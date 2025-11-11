/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_cache64

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/cache_device.h>
#include <zephyr/logging/log.h>
#include <fsl_cache.h>

LOG_MODULE_REGISTER(cache_device_nxp_cache64, CONFIG_CACHE_LOG_LEVEL);

struct cache64_data {
    bool enabled;
};

struct cache64_config {
    CACHE64_CTRL_Type *base;
    /* Static cache characteristics (from Devicetree when provided) */
    /* CACHE_INFO_TYPE_* */
    uint8_t type;
    /* 1,2,... or 0 if unknown */
    uint8_t level;
    /* bytes, 0 if unknown */
    uint32_t line_size;
    /* 0 if unknown */
    uint32_t ways;
    /* 0 if unknown */
    uint32_t sets;
    /* bytes, 0 if unknown */
    uint32_t size;
    /* CACHE_INFO_ATTR_* bitmask */
    uint32_t attrs;
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

static int cache64_get_info(const struct device *dev, struct cache_info *info)
{
    if (info == NULL) {
        return -EINVAL;
    }

    const struct cache64_config *config = dev->config;

    info->cache_type = config->type;
    info->cache_level = config->level;
    info->line_size = config->line_size;
    info->ways = config->ways;
    info->sets = config->sets;
    info->size = config->size;
    info->attributes = config->attrs;

    /* If nothing meaningful is known, indicate not supported */
    if (info->line_size == 0U && info->size == 0U && info->ways == 0U && info->sets == 0U) {
        return -ENOTSUP;
    }

    return 0;
}

/*
 * Helpers to check whether a given range is fully covered by any of the
 * cache windows defined on a device instance. The cache-windows DT property
 * is an array of base/size pairs: <base0 size0 base1 size1 ...>.
 */

static inline bool cache64_range_within(uintptr_t start, size_t size,
                                        uintptr_t base, size_t win_size)
{
    if (size == 0U) {
        return false;
    }
    uintptr_t end = start + size;
    uintptr_t win_end = base + win_size;
    return (start >= base) && (end <= win_end);
}

/* Macro used inside functions to check one base/size pair; relies on locals: start, size, covered */
#define CACHE64_CW_CHECK(node_id, prop, idx)                                                  \
    do {                                                                                      \
        if (((idx) % 2) == 0) {                                                               \
            prev_base = (uintptr_t)DT_PROP_BY_IDX(node_id, prop, idx);                        \
            have_prev = true;                                                                 \
        } else {                                                                              \
            size_t _s = (size_t)DT_PROP_BY_IDX(node_id, prop, idx);                           \
            if (have_prev) {                                                                  \
                if (cache64_range_within(start, size, prev_base, _s)) {                       \
                    covered = true;                                                           \
                }                                                                             \
                have_prev = false;                                                            \
            }                                                                                 \
        }                                                                                      \
    } while (0)

/* Define a per-instance predicate that checks windows declared in DT */
#define CACHE64_DEFINE_WINDOW_CHECK(inst)                                                     \
    static bool cache64_inst_range_in_windows_##inst(uintptr_t start, size_t size)           \
    {                                                                                        \
        bool covered = false;                                                                \
        uintptr_t prev_base = 0U;                                                            \
        bool have_prev = false;                                                              \
        /* If cache-windows is present, iterate elements; treat even idx as base */          \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, cache_windows), (                            \
            DT_FOREACH_PROP_ELEM_SEP(DT_DRV_INST(inst), cache_windows,                       \
                                      CACHE64_CW_CHECK, (;))                                  \
        ), (                                                                                 \
            ARG_UNUSED(start);                                                               \
            ARG_UNUSED(size);                                                                \
        ));                                                                                  \
        return covered;                                                                      \
    }

/* Instantiate the per-instance window checkers */
DT_INST_FOREACH_STATUS_OKAY(CACHE64_DEFINE_WINDOW_CHECK)

/* Find-and-run helper for range operations across all instances */
#define CACHE64_FOR_FIRST_COVERING(inst, fn_call)                                            \
    do {                                                                                     \
        if (!matched && cache64_inst_range_in_windows_##inst((uintptr_t)addr, size)) {       \
            fn_call;                                                                         \
            matched = true;                                                                  \
            rc = 0;                                                                          \
        }                                                                                    \
    } while (0)

/* Per-device range operations: only operate if range is within a declared window
 * for at least one instance. The dispatcher will normalize -ERANGE to -ENOTSUP
 * when aggregating across devices.
 */
static int cache64_invalidate_range(const struct device *dev, void *addr, size_t size)
{
    if (addr == NULL || size == 0U) {
        return -EINVAL;
    }

    int rc = -ERANGE;
    bool matched = false;
    LOG_DBG("Cache64 invalidate range: 0x%08x, size: %zu", (uint32_t)addr, size);

#define _DO_INV(inst)                                                                         \
    CACHE64_FOR_FIRST_COVERING(inst, CACHE64_InvalidateCacheByRange((uint32_t)addr, size))
    DT_INST_FOREACH_STATUS_OKAY(_DO_INV);
#undef _DO_INV

    return rc;
}

static int cache64_flush_range(const struct device *dev, void *addr, size_t size)
{
    if (addr == NULL || size == 0U) {
        return -EINVAL;
    }

    int rc = -ERANGE;
    bool matched = false;
    LOG_DBG("Cache64 flush range: 0x%08x, size: %zu", (uint32_t)addr, size);

#define _DO_FLUSH(inst)                                                                        \
    CACHE64_FOR_FIRST_COVERING(inst, CACHE64_CleanCacheByRange((uint32_t)addr, size))
    DT_INST_FOREACH_STATUS_OKAY(_DO_FLUSH);
#undef _DO_FLUSH

    return rc;
}

static int cache64_flush_and_invalidate_range(const struct device *dev, void *addr, size_t size)
{
    if (addr == NULL || size == 0U) {
        return -EINVAL;
    }

    int rc = -ERANGE;
    bool matched = false;
    LOG_DBG("Cache64 flush and invalidate range: 0x%08x, size: %zu", (uint32_t)addr, size);

#define _DO_FLUSH_INV(inst)                                                                    \
    CACHE64_FOR_FIRST_COVERING(inst, CACHE64_CleanInvalidateCacheByRange((uint32_t)addr, size))
    DT_INST_FOREACH_STATUS_OKAY(_DO_FLUSH_INV);
#undef _DO_FLUSH_INV

    return rc;
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
    .flush_range = cache64_flush_range,
    .invalidate_range = cache64_invalidate_range,
    .flush_and_invalidate_range = cache64_flush_and_invalidate_range,
    .invalidate_all = cache64_invalidate_all,
    .flush_all = cache64_flush_all,
    .flush_and_invalidate_all = cache64_flush_and_invalidate_all,
    .get_info = cache64_get_info,
};

/* Device instantiation */
#define CACHE64_DEVICE(inst)                                                  \
    static const struct cache64_config cache64_config_##inst = {              \
        .base = (CACHE64_CTRL_Type *)DT_INST_REG_ADDR(inst),                  \
        /* Default to unified external cache; prefer DT overrides when present */ \
        .type = CACHE_INFO_TYPE_UNIFIED,                                      \
        .level = DT_INST_PROP_OR(inst, cache_level, 0),                       \
        .line_size = DT_INST_PROP_OR(inst, cache_line_size, 0),               \
        .ways = DT_INST_PROP_OR(inst, cache_ways, 0),                         \
        .sets = DT_INST_PROP_OR(inst, cache_sets, 0),                         \
        .size = DT_INST_PROP_OR(inst, cache_size, 8 * 1024),                  \
        /* policy programmable via POLSEL; leave unspecified */               \
        .attrs = 0,                                                           \
    };                                                                        \
    static struct cache64_data cache64_data_##inst;                         \
    DEVICE_DT_INST_DEFINE(inst, cache64_init, NULL,                         \
                          &cache64_data_##inst, &cache64_config_##inst,     \
                          POST_KERNEL, CONFIG_CACHE_DEVICE_INIT_PRIORITY,          \
                          &cache64_api);

DT_INST_FOREACH_STATUS_OKAY(CACHE64_DEVICE)
