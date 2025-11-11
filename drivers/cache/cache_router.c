#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/arch/cache.h>
#include <zephyr/drivers/cache_device.h>
#include <errno.h>

/* Helper: combine two return codes.
 *  - EINVAL from any path wins (bad arguments).
 *  - If either path succeeds (0), overall success is 0.
 *  - Otherwise return first non-zero error (for diagnostics).
 */
static inline int cache_router_compose_rc(int inner_rc, int outer_rc)
{
    if (inner_rc == -EINVAL || outer_rc == -EINVAL) {
        return -EINVAL;
    }

    if (inner_rc == 0 || outer_rc == 0) {
        return 0;
    }

    return inner_rc ? inner_rc : outer_rc;
}

void z_sys_cache_data_enable_router(void)
{
#ifdef CONFIG_DCACHE
    cache_data_enable();
#endif
#ifdef CONFIG_CACHE_DEVICE
    /* Enable all external/device caches */
    (void)cache_device_enable_all();
#endif
}

void z_sys_cache_data_disable_router(void)
{
#ifdef CONFIG_DCACHE
    cache_data_disable();
#endif
#ifdef CONFIG_CACHE_DEVICE
    (void)cache_device_disable_all();
#endif
}

int z_sys_cache_data_flush_range_router(void *addr, size_t size)
{
    if (!addr || size == 0U) {
        return -EINVAL;
    }

    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_DCACHE
    inner_rc = arch_dcache_flush_range(addr, size);
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_flush_range(addr, size);
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_data_invd_range_router(void *addr, size_t size)
{
    if (!addr || size == 0U) {
        return -EINVAL;
    }

    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_DCACHE
    inner_rc = arch_dcache_invd_range(addr, size);
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_invalidate_range(addr, size);
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_data_flush_and_invd_range_router(void *addr, size_t size)
{
    if (!addr || size == 0U) {
        return -EINVAL;
    }

    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_DCACHE
    inner_rc = arch_dcache_flush_and_invd_range(addr, size);
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_flush_and_invalidate_range(addr, size);
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_data_flush_all_router(void)
{
    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_DCACHE
    inner_rc = arch_dcache_flush_all();
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_data_flush_all();
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_data_invd_all_router(void)
{
    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_DCACHE
    inner_rc = arch_dcache_invd_all();
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_data_invalidate_all();
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_data_flush_and_invd_all_router(void)
{
    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_DCACHE
    inner_rc = arch_dcache_flush_and_invd_all();
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_data_flush_and_invalidate_all();
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_instr_flush_range_router(void *addr, size_t size)
{
    if (!addr || size == 0U) {
        return -EINVAL;
    }

    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_ICACHE
    inner_rc = arch_icache_flush_range(addr, size);
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_instr_flush_range(addr, size);
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_instr_invd_range_router(void *addr, size_t size)
{
    if (!addr || size == 0U) {
        return -EINVAL;
    }

    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_ICACHE
    inner_rc = arch_icache_invd_range(addr, size);
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_instr_invalidate_range(addr, size);
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_instr_flush_and_invd_range_router(void *addr, size_t size)
{
    if (!addr || size == 0U) {
        return -EINVAL;
    }

    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_ICACHE
    inner_rc = arch_icache_flush_and_invd_range(addr, size);
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_instr_flush_and_invalidate_range(addr, size);
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_instr_flush_all_router(void)
{
    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_ICACHE
    inner_rc = arch_icache_flush_all();
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_instr_flush_all();
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_instr_invd_all_router(void)
{
    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_ICACHE
    inner_rc = arch_icache_invd_all();
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_instr_invalidate_all();
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

int z_sys_cache_instr_flush_and_invd_all_router(void)
{
    int inner_rc = 0;
    int outer_rc = 0;

#ifdef CONFIG_ICACHE
    inner_rc = arch_icache_flush_and_invd_all();
#endif

#ifdef CONFIG_CACHE_DEVICE
    outer_rc = cache_device_instr_flush_and_invalidate_all();
#endif

    return cache_router_compose_rc(inner_rc, outer_rc);
}

void z_sys_cache_instr_enable_router(void)
{
#ifdef CONFIG_ICACHE
    cache_instr_enable();
#endif
#ifdef CONFIG_CACHE_DEVICE
    /* Unified external caches; enabling once is sufficient */
#endif
}

void z_sys_cache_instr_disable_router(void)
{
#ifdef CONFIG_ICACHE
    cache_instr_disable();
#endif
#ifdef CONFIG_CACHE_DEVICE
    /* No generic disable-all API yet */
#endif
}
