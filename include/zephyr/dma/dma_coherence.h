/*
 * DMA Coherence Helper (header-only)
 *
 * Provides simple directional APIs to compose system (L1) cache
 * maintenance with device-side (memory) cache maintenance when both
 * are present. Ordering mirrors common Linux dma_map/unmap semantics
 * but remains optional and lightweight for Zephyr MCUs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DMA_DMA_COHERENCE_H_
#define ZEPHYR_INCLUDE_DMA_DMA_COHERENCE_H_

#include <zephyr/cache.h>
#if defined(CONFIG_CACHE_DEVICE)
#include <zephyr/drivers/cache_device.h>
#endif
#include <errno.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Direction mirrors Linux semantics */
enum dma_direction {
	DMA_TO_DEVICE,
	DMA_FROM_DEVICE,
	DMA_BIDIRECTIONAL,
};

/* Forward declarations (included from cache.h but added to satisfy static analysis) */
int sys_cache_data_flush_range(void *addr, size_t size);
int sys_cache_data_invd_range(void *addr, size_t size);

/*
 * Internal helper: treat ENOSYS/ENOTSUP as non-fatal when composing ops.
 * Rationale:
 * - Global cache_device_*_range() should normalize "no device covers range"
 *   to -ENOTSUP. ERANGE is not expected to surface from global APIs.
 * - sys cache range ops may also return -ENOTSUP when unsupported.
 * Therefore, -ENOSYS/-ENOTSUP are considered benign for composition.
 */
static inline int dma_coherence_merge_rc(int prev, int rc)
{
	/* Preserve the first real error */
	if (prev != 0 && prev != -ENOTSUP && prev != -ENOSYS) {
		return prev;
	}
	if (rc == 0 || rc == -ENOTSUP || rc == -ENOSYS) {
		/* success or effectively no-op on this path */
		return (prev == 0) ? 0 : prev;
	}
	return rc;
}

/* Prepare a buffer for a pending DMA transfer. */
/*
 * dma_cache_prepare(): prepare a buffer before DMA submission.
 * When the device-cache router is enabled sys_cache_* already performs
 * combined inner + outer maintenance. In that configuration we skip the
 * explicit cache_device_* path to avoid redundant work.
 */
static inline int dma_cache_prepare(void *addr, size_t size, enum dma_direction dir)
{
	if (size == 0U) {
		return -EINVAL;
	}

	int rc = 0;

	switch (dir) {
	    case DMA_TO_DEVICE:
	    case DMA_BIDIRECTIONAL:
		/* Push dirty data outward: always flush system cache first */
		/* sys_cache_data_flush_range is a syscall wrapper declared in cache.h */
		rc = dma_coherence_merge_rc(rc, sys_cache_data_flush_range(addr, size));
		/* If router is not aggregating, explicitly flush device caches */
#if !defined(CONFIG_CACHE_DEVICE_ROUTER) && defined(CONFIG_CACHE_DEVICE)
		rc = dma_coherence_merge_rc(rc, cache_device_data_flush_range(addr, size));
#endif
		break;
	case DMA_FROM_DEVICE:
		/* Pre-invalidate optional: many MCUs just invalidate on completion.
		 * Keep this minimal and let callers choose if needed.
		 */
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

/* Complete a DMA transfer; ensure CPU will observe fresh data. */
/*
 * dma_cache_complete(): finalize a DMA transfer so CPU sees fresh data.
 * With router active one invalidate is sufficient.
 */
static inline int dma_cache_complete(void *addr, size_t size, enum dma_direction dir)
{
	if (size == 0U) {
		return -EINVAL;
	}

	int rc = 0;

	switch (dir) {
	    case DMA_FROM_DEVICE:
	    case DMA_BIDIRECTIONAL:
		/* Invalidate after DMA completion: invalidate device caches first unless router aggregates */
#if !defined(CONFIG_CACHE_DEVICE_ROUTER) && defined(CONFIG_CACHE_DEVICE)
		rc = dma_coherence_merge_rc(rc, cache_device_data_invalidate_range(addr, size));
#endif
		/* sys_cache_data_invd_range is a syscall wrapper declared in cache.h */
		rc = dma_coherence_merge_rc(rc, sys_cache_data_invd_range(addr, size));
		break;
	case DMA_TO_DEVICE:
		/* Usually nothing to do post TX on MCUs */
		break;
	default:
		return -ENOTSUP;
	}

	return rc;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DMA_DMA_COHERENCE_H_ */
