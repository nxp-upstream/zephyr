/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MCP_SERVER_AUTH_H_
#define MCP_SERVER_AUTH_H_

#include <string.h>
#include <inttypes.h>
#include <zephyr/kernel.h>

#if defined(CONFIG_MCP_HTTP_AUTH_ENABLED)

/**
 * Preprocess and validate JWT authentication token
 * 
 * @param jwt_token Full JWT token string (header.payload.signature)
 * @return 0 if token is valid, negative error code otherwise
 */
int preprocess_and_validate_token(const char *jwt_token);

#endif /* CONFIG_MCP_HTTP_AUTH_ENABLED */

#endif /* MCP_SERVER_AUTH_H_ */