/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/cache.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <fsl_cache.h>

LOG_MODULE_REGISTER(cache_nxp_xcache, CONFIG_CACHE_LOG_LEVEL);

#if !defined(NXP_XCACHE_INSTR)
#define NXP_XCACHE_INSTR XCACHE_PC
#endif

#if !defined(NXP_XCACHE_DATA)
#define NXP_XCACHE_DATA XCACHE_PS
#endif

#if defined(XCACHE_PHYMEM_BASES_NS)
/*
 * A secure image knows only the secure windows of the caches, but it can
 * also reach the memory through the non-secure alias, cached in the same
 * lines (the line commands ignore the alias bit). The HAL range operations
 * silently skip addresses outside the secure windows: translate them.
 */
static const uint32_t xcache_s_bases[] = XCACHE_PHYMEM_BASES;
static const uint32_t xcache_ns_bases[] = XCACHE_PHYMEM_BASES_NS;
static const uint32_t xcache_ns_sizes[] = XCACHE_PHYMEM_SIZES_NS;

BUILD_ASSERT(ARRAY_SIZE(xcache_s_bases) == ARRAY_SIZE(xcache_ns_bases),
	     "Secure and non-secure cache windows must match");
#endif

static uint32_t xcache_phys_addr(void *addr)
{
	uint32_t address = (uint32_t)(uintptr_t)addr;

#if defined(XCACHE_PHYMEM_BASES_NS)
	for (size_t i = 0; i < ARRAY_SIZE(xcache_ns_bases); i++) {
		if ((address >= xcache_ns_bases[i]) &&
		    ((address - xcache_ns_bases[i]) < xcache_ns_sizes[i])) {
			return address - xcache_ns_bases[i] + xcache_s_bases[i];
		}
	}
#endif

	return address;
}

void cache_data_enable(void)
{
	XCACHE_EnableCache(NXP_XCACHE_DATA);
}

void cache_data_disable(void)
{
	XCACHE_DisableCache(NXP_XCACHE_DATA);
}

int cache_data_flush_all(void)
{
	XCACHE_CleanCache(NXP_XCACHE_DATA);

	return 0;
}

int cache_data_invd_all(void)
{
	XCACHE_InvalidateCache(NXP_XCACHE_DATA);

	return 0;
}

int cache_data_flush_and_invd_all(void)
{
	XCACHE_CleanInvalidateCache(NXP_XCACHE_DATA);

	return 0;
}

int cache_data_flush_range(void *addr, size_t size)
{
	XCACHE_CleanCacheByRange(xcache_phys_addr(addr), size);

	return 0;
}

int cache_data_invd_range(void *addr, size_t size)
{
	XCACHE_InvalidateCacheByRange(xcache_phys_addr(addr), size);

	return 0;
}

int cache_data_flush_and_invd_range(void *addr, size_t size)
{
	XCACHE_CleanInvalidateCacheByRange(xcache_phys_addr(addr), size);

	return 0;
}

void cache_instr_enable(void)
{
	XCACHE_EnableCache(NXP_XCACHE_INSTR);
}

void cache_instr_disable(void)
{
	XCACHE_DisableCache(NXP_XCACHE_INSTR);
}

int cache_instr_flush_all(void)
{
	XCACHE_CleanCache(NXP_XCACHE_INSTR);

	return 0;
}

int cache_instr_invd_all(void)
{
	XCACHE_InvalidateCache(NXP_XCACHE_INSTR);

	return 0;
}

int cache_instr_flush_and_invd_all(void)
{
	XCACHE_CleanInvalidateCache(NXP_XCACHE_INSTR);

	return 0;
}

int cache_instr_flush_range(void *addr, size_t size)
{
	XCACHE_CleanCacheByRange(xcache_phys_addr(addr), size);

	return 0;
}

int cache_instr_invd_range(void *addr, size_t size)
{
	XCACHE_InvalidateCacheByRange(xcache_phys_addr(addr), size);

	return 0;
}

int cache_instr_flush_and_invd_range(void *addr, size_t size)
{
	XCACHE_CleanInvalidateCacheByRange(xcache_phys_addr(addr), size);

	return 0;
}
