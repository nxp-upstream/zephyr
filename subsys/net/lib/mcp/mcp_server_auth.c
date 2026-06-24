/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/service.h>
#include <zephyr/data/json.h> 

#if defined(CONFIG_POSIX_CLOCK)
#include <zephyr/posix/time.h>          // For clock_gettime (expiration check)
#endif

LOG_MODULE_REGISTER(mcp_server_auth, CONFIG_MCP_LOG_LEVEL);

/**
 * Token claims structure matching your JWT format
 */
struct token_claims {
	const char *iss;    /* Issuer: "https://auth.example.com" */
	const char *sub;    /* Subject: "client-123" */
	const char *aud;    /* Audience: "mcp-server" */
	int64_t exp;        /* Expiration time (Unix timestamp) */
	const char *scope;  /* Scope: "mcp:invoke" */
};

/**
 * Validate authentication token
 * 
 * @param token Decoded token bytes
 * @param token_len Length of token in bytes
 * @return 0 if token is valid, negative error code otherwise
 */
int validate_token(const uint8_t *token, int token_len)
{
    static const struct json_obj_descr token_descr[] = {
		JSON_OBJ_DESCR_PRIM(struct token_claims, iss, JSON_TOK_STRING),
		JSON_OBJ_DESCR_PRIM(struct token_claims, sub, JSON_TOK_STRING),
		JSON_OBJ_DESCR_PRIM(struct token_claims, aud, JSON_TOK_STRING),
		JSON_OBJ_DESCR_PRIM(struct token_claims, exp, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM(struct token_claims, scope, JSON_TOK_STRING),
	};
	
	struct token_claims claims;
	int ret;
	char token_str[38];

	int64_t current_time;

	if ((token == NULL) || (token_len <= 0)) {
		LOG_ERR("Invalid token parameters");
		return -EINVAL;
	}
	/* Ensure token is null-terminated string */
	if (token_len >= sizeof(token_str)) {
		LOG_ERR("Token too large: %d bytes (max: %zu)", token_len, sizeof(token_str));
		return -ENOSPC;
	}
	
	memcpy(token_str, token, token_len);
	token_str[token_len] = '\0';

	LOG_DBG("Parsing token: %s", token_str);

	/* Parse JSON token */
	memset(&claims, 0, sizeof(claims));
	ret = json_obj_parse(token_str, token_len, token_descr, ARRAY_SIZE(token_descr), &claims);
	if (ret < 0) {
		LOG_ERR("Failed to parse token JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate required fields are present */
	if (claims.iss == NULL || strlen(claims.iss) == 0) {
		LOG_ERR("Missing or empty 'iss' field");
		return -EINVAL;
	}
	
	if (claims.sub == NULL || strlen(claims.sub) == 0) {
		LOG_ERR("Missing or empty 'sub' field");
		return -EINVAL;
	}
	
	if (claims.aud == NULL || strlen(claims.aud) == 0) {
		LOG_ERR("Missing or empty 'aud' field");
		return -EINVAL;
	}

	if (claims.exp == 0) {
		LOG_ERR("Missing or invalid 'exp' field");
		return -EINVAL;
	}

	/* Check token expiration */
	current_time = k_uptime_get() / 1000; /* Convert to seconds */
	
#if defined(CONFIG_MCP_HTTP_AUTH_CHECK_EXPIRATION)
	/* If you have real-time clock, use it instead */
	#if defined(CONFIG_POSIX_CLOCK)
	struct timespec ts;
	
	ret = clock_gettime(CLOCK_REALTIME, &ts);
	if (ret == 0) {
		current_time = ts.tv_sec;
	} else {
		LOG_WRN("Failed to get real time, using uptime");
	}
	#endif

	if (current_time >= claims.exp) {
		LOG_ERR("Token expired: exp=%lld, current=%lld", claims.exp, current_time);
		return -EACCES;
	}
	
	LOG_DBG("Token expires in %lld seconds", claims.exp - current_time);
#else
	LOG_DBG("Token expiration check disabled (exp: %lld)", claims.exp);
#endif

	/* Validate issuer */
#if defined(CONFIG_MCP_HTTP_AUTH_EXPECTED_ISSUER)
	if (strcmp(claims.iss, CONFIG_MCP_HTTP_AUTH_EXPECTED_ISSUER) != 0) {
		LOG_ERR("Invalid issuer: '%s' (expected: '%s')", 
			claims.iss, CONFIG_MCP_HTTP_AUTH_EXPECTED_ISSUER);
		return -EACCES;
	}
	LOG_DBG("Issuer validated: %s", claims.iss);
#endif

	/* Validate audience */
#if defined(CONFIG_MCP_HTTP_AUTH_EXPECTED_AUDIENCE)
	if (strcmp(claims.aud, CONFIG_MCP_HTTP_AUTH_EXPECTED_AUDIENCE) != 0) {
		LOG_ERR("Invalid audience: '%s' (expected: '%s')", 
			claims.aud, CONFIG_MCP_HTTP_AUTH_EXPECTED_AUDIENCE);
		return -EACCES;
	}
	LOG_DBG("Audience validated: %s", claims.aud);
#endif

	/* Validate scope if required */
#if defined(CONFIG_MCP_HTTP_AUTH_REQUIRED_SCOPE)
	if (claims.scope == NULL || strlen(claims.scope) == 0) {
		LOG_ERR("Missing scope");
		return -EACCES;
	}
	
	/* Check if required scope is present (handles space-separated scopes) */
	if (strstr(claims.scope, CONFIG_MCP_HTTP_AUTH_REQUIRED_SCOPE) == NULL) {
		LOG_ERR("Required scope '%s' not found in: '%s'", 
			CONFIG_MCP_HTTP_AUTH_REQUIRED_SCOPE, claims.scope);
		return -EACCES;
	}
	LOG_DBG("Scope validated: %s", claims.scope);
#endif

	LOG_INF("Token validated successfully - iss: %s, sub: %s, aud: %s, scope: %s, exp: %lld",
		claims.iss, claims.sub, claims.aud, 
		claims.scope ? claims.scope : "(none)", claims.exp);

	LOG_DBG("Token validated successfully");
	return 0;
}