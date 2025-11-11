/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Common cache information definitions shared by sys and device caches
 */

#ifndef ZEPHYR_INCLUDE_CACHE_INFO_H_
#define ZEPHYR_INCLUDE_CACHE_INFO_H_

#include <zephyr/sys/util.h>
#include <stdint.h>

/**
 * @brief Cache type definitions (shared)
 */
#define CACHE_INFO_TYPE_INSTRUCTION  BIT(0)
#define CACHE_INFO_TYPE_DATA         BIT(1)
#define CACHE_INFO_TYPE_UNIFIED      (CACHE_INFO_TYPE_INSTRUCTION | CACHE_INFO_TYPE_DATA)

/**
 * @brief Cache attribute flags (capabilities, shared)
 */
#define CACHE_INFO_ATTR_WRITE_THROUGH  BIT(0)
#define CACHE_INFO_ATTR_WRITE_BACK     BIT(1)
#define CACHE_INFO_ATTR_READ_ALLOCATE  BIT(2)
#define CACHE_INFO_ATTR_WRITE_ALLOCATE BIT(3)

/**
 * @brief Common cache information structure
 *
 * Matches the concepts in dts/bindings/cacheinfo.yaml.
 */
struct cache_info {
	/** Optional identifier unique within (type, level) for this cache */
	uint32_t id;
	/** Cache type (instruction, data, or unified), see CACHE_INFO_TYPE_* */
	uint32_t cache_type;
	/** Cache level (1 for L1, 2 for L2, etc.) */
	uint32_t cache_level;
	/** Cache line size in bytes (coherency line size) */
	uint32_t line_size;
	/** Number of ways (associativity), 0 if unknown */
	uint32_t ways;
	/** Number of sets, 0 if unknown */
	uint32_t sets;
	/** Physical line partition (if applicable, else 0) */
	uint32_t physical_line_partition;
	/** Total cache size in bytes, 0 if unknown */
	uint32_t size;
	/** Capability attributes bitfield, see CACHE_INFO_ATTR_* */
	uint32_t attributes;
};

#endif /* ZEPHYR_INCLUDE_CACHE_INFO_H_ */
