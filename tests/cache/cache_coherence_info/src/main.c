#include <zephyr/ztest.h>
#include <zephyr/cache.h>
#include <zephyr/cache_info.h>
#include <zephyr/dma/dma_coherence.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/cache_device.h>

ZTEST(cache_coherence_info, test_sys_cache_info_present_or_enotsup)
{
    struct cache_info ci;

    int rc_i = sys_cache_instr_get_info(&ci);
    zassert_true(rc_i == 0 || rc_i == -ENOTSUP, "instr rc=%d", rc_i);
    if (rc_i == 0) {
        zassert_true(ci.cache_type == CACHE_INFO_TYPE_INSTRUCTION || ci.cache_type == CACHE_INFO_TYPE_UNIFIED,
                     "unexpected instr cache_type=%u", (unsigned)ci.cache_type);
        zassert_true(ci.line_size > 0, "instr line_size should be > 0");
    }

    int rc_d = sys_cache_data_get_info(&ci);
    zassert_true(rc_d == 0 || rc_d == -ENOTSUP, "data rc=%d", rc_d);
    if (rc_d == 0) {
        zassert_true(ci.cache_type == CACHE_INFO_TYPE_DATA || ci.cache_type == CACHE_INFO_TYPE_UNIFIED,
                     "unexpected data cache_type=%u", (unsigned)ci.cache_type);
        zassert_true(ci.line_size > 0, "data line_size should be > 0");
    }
}

ZTEST(cache_coherence_info, test_dma_coherence_prepare_complete_ok)
{
    uint8_t buf[128];
    /* Expect composition to succeed even if some ops are ENOTSUP */
    zassert_ok(dma_cache_prepare(buf, sizeof(buf), DMA_TO_DEVICE));
    zassert_ok(dma_cache_complete(buf, sizeof(buf), DMA_TO_DEVICE));

    zassert_ok(dma_cache_complete(buf, sizeof(buf), DMA_FROM_DEVICE));
}

/* Optional: if a device cache is present (e.g. cache64), validate get_info */
ZTEST(cache_coherence_info, test_device_cache_info_if_present)
{
#if IS_ENABLED(CONFIG_CACHE_DEVICE_NXP_CACHE64) && \
    DT_NODE_EXISTS(DT_NODELABEL(cache64)) && \
    DT_NODE_HAS_STATUS(DT_NODELABEL(cache64), okay)
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(cache64));
    zassert_true(device_is_ready(dev), "cache64 device not ready");

    struct cache_info ci;
    int rc = cache_device_get_info(dev, &ci);
    zassert_true(rc == 0 || rc == -ENOTSUP || rc == -ENOSYS,
                 "unexpected device get_info rc=%d", rc);
    if (rc == 0) {
        /* Unified or data cache expected for cache64 */
        zassert_true(ci.cache_type == CACHE_INFO_TYPE_UNIFIED || ci.cache_type == CACHE_INFO_TYPE_DATA,
                     "unexpected device cache_type=%u", (unsigned)ci.cache_type);
        zassert_true(ci.line_size > 0, "device line_size should be > 0");
        zassert_true(ci.size > 0, "device size should be > 0");
    }
#else
    ztest_test_skip();
#endif
}

ZTEST_SUITE(cache_coherence_info, NULL, NULL, NULL, NULL, NULL);
