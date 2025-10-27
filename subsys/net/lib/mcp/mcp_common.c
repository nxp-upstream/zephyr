/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "mcp_common.h"

void *mcp_alloc(size_t size)
{
	return k_malloc(size);
}

void mcp_free(void *ptr)
{
	k_free(ptr);
}
