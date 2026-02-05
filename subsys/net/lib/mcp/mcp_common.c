/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "mcp_common.h"

__weak void *mcp_alloc(size_t size)
{
	return k_malloc(size);
}

__weak void mcp_free(void *ptr)
{
	k_free(ptr);
}

void mcp_safe_strcpy(char *dst, size_t dst_sz, const char *src)
{
	if (!dst || dst_sz == 0) {
		return;
	}

	if (!src) {
		dst[0] = '\0';
		return;
	}

	(void)snprintk(dst, dst_sz, "%s", src);
}
