/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_COMMON_H_
#define ZEPHYR_SUBSYS_MCP_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>
#include "mcp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Weak allocation function, uses k_malloc by default
 */
__weak void *mcp_alloc(size_t size);

/**
 * @brief Weak free function, uses k_malloc by default
 */
__weak void mcp_free(void *ptr);

/**
 * @brief Safe string copy function, uses snprintk for bounded copying
 */
void mcp_safe_strcpy(char *dst, size_t dst_sz, const char *src);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MCP_COMMON_H_ */
