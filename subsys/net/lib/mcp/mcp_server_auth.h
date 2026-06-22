/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <inttypes.h>
#include <zephyr/kernel.h>

/**
 * Validate authentication token
 * 
 * @param token Decoded token bytes
 * @param token_len Length of token in bytes
 * @return 0 if token is valid, negative error code otherwise
 */
int validate_token(const uint8_t *token, int token_len);