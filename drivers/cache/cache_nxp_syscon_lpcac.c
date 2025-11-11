/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <zephyr/drivers/cache.h>
#include <zephyr/cache_info.h>
#include <zephyr/logging/log.h>
#include <fsl_cache_lpcac.h>

void cache_instr_enable(void)
{
	L1CACHE_EnableCodeCache();
}

void cache_instr_disable(void)
{
	L1CACHE_DisableCodeCache();
}

int cache_instr_flush_all(void)
{
	return -ENOTSUP;
}

int cache_instr_invd_all(void)
{
	L1CACHE_InvalidateCodeCache();

	return 0;
}

int cache_instr_flush_and_invd_all(void)
{
	return -ENOTSUP;
}

int cache_instr_flush_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

int cache_instr_invd_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

int cache_instr_flush_and_invd_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

#if defined(CONFIG_SYS_CACHE_INFO)
int cache_instr_get_info(struct cache_info *info)
{
	if (info == NULL) {
		return -EINVAL;
	}

	/* If not configured, report not supported */
	if (CONFIG_SYS_ICACHE_INFO_LINE_SIZE == 0) {
		return -ENOTSUP;
	}

	info->id = 0U;
	info->cache_type = CACHE_INFO_TYPE_INSTRUCTION;
	info->cache_level = CONFIG_SYS_ICACHE_INFO_LEVEL;
	info->line_size = CONFIG_SYS_ICACHE_INFO_LINE_SIZE;
	info->ways = CONFIG_SYS_ICACHE_INFO_WAYS;
	info->sets = CONFIG_SYS_ICACHE_INFO_SETS;
	info->physical_line_partition = 0U;
	info->size = CONFIG_SYS_ICACHE_INFO_SIZE;
	info->attributes = CONFIG_SYS_ICACHE_INFO_ATTRIBUTES;

	return 0;
}
#endif
