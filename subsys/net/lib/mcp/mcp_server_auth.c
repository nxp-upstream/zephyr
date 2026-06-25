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
#include <zephyr/sys/base64.h>

#if defined(CONFIG_POSIX_CLOCK)
#include <zephyr/posix/time.h>          // For clock_gettime (expiration check)
#endif

LOG_MODULE_REGISTER(mcp_server_auth, CONFIG_MCP_LOG_LEVEL);

#if defined(CONFIG_MCP_HTTP_AUTH_ENABLED)

/**The token contains 3 parts: header.payload.signature 
 * header: 
{
  "alg": "RS256",
  "typ": "JWT",
  "kid": "abc123"
}
payload:
{
  "iss": "https://auth.example.com",
  "sub": "client-123",
  "aud": "mcp-server",
  "exp": 1718000000,
  "scope": "mcp:invoke"
}
signature is computed over: base64(header) + "." + base64(payload)
*/

/**
 * JWT header structure
 */
struct jwt_header {
	const char *alg;    /* Algorithm: "RS256", "HS256", etc. */
	const char *typ;    /* Type: "JWT" */
	const char *kid;    /* Key ID: "abc123" */
};

/**
 * Token claims structure matching the payload in the JWT format authorization header
 */
struct token_claims {
	const char *iss;    /* Issuer: "https://auth.example.com" */
	const char *sub;    /* Subject: "client-123" */
	const char *aud;    /* Audience: "mcp-server" */
	int64_t exp;        /* Expiration time (Unix timestamp) */
	const char *scope;  /* Scope: "mcp:invoke" */
};

/**
 * Split JWT token into its three parts: header, payload, signature
 * 
 * @param jwt_token Full JWT token string
 * @param header Output pointer to header part (not null-terminated)
 * @param header_len Output length of header part
 * @param payload Output pointer to payload part (not null-terminated)
 * @param payload_len Output length of payload part
 * @param signature Output pointer to signature part (not null-terminated)
 * @param signature_len Output length of signature part
 * @return 0 on success, negative error code on failure
 */
static int split_jwt_token(const char *jwt_token,
                          const char **header, size_t *header_len,
                          const char **payload, size_t *payload_len,
                          const char **signature, size_t *signature_len)
{
	const char *first_dot;
	const char *second_dot;

	if (jwt_token == NULL) {
		LOG_ERR("NULL JWT token");
		return -EINVAL;
	}

	/* Find first dot (separates header and payload) */
	first_dot = strchr(jwt_token, '.');
	if (first_dot == NULL) {
		LOG_ERR("Invalid JWT format: missing first delimiter");
		return -EINVAL;
	}

	/* Find second dot (separates payload and signature) */
	second_dot = strchr(first_dot + 1, '.');
	if (second_dot == NULL) {
		LOG_ERR("Invalid JWT format: missing second delimiter");
		return -EINVAL;
	}

	/* Extract header */
	*header = jwt_token;
	*header_len = first_dot - jwt_token;

	/* Extract payload */
	*payload = first_dot + 1;
	*payload_len = second_dot - (first_dot + 1);

	/* Extract signature */
	*signature = second_dot + 1;
	*signature_len = strlen(second_dot + 1);

	if (*header_len == 0 || *payload_len == 0 || *signature_len == 0) {
		LOG_ERR("Invalid JWT format: empty component");
		return -EINVAL;
	}

	LOG_DBG("JWT split - header: %zu bytes, payload: %zu bytes, signature: %zu bytes",
		*header_len, *payload_len, *signature_len);

	return 0;
}

/**
 * Decode base64 encoded JWT component
 * 
 * @param encoded Base64 encoded string (not null-terminated)
 * @param encoded_len Length of encoded string
 * @param decoded Buffer for decoded output
 * @param decoded_size Size of decoded buffer
 * @return Length of decoded data on success, negative error code on failure
 */
static int decode_jwt_component(const char *encoded, size_t encoded_len,
                                uint8_t *decoded, size_t decoded_size)
{
	size_t decoded_len;
	int ret;

	if ((encoded == NULL) || (decoded == NULL) || (decoded_size == 0) || (encoded_len == 0)) {
		return -EINVAL;
	}

	ret = base64_decode(decoded, decoded_size, &decoded_len, encoded, encoded_len);
	if (ret != 0) {
		LOG_ERR("Base64 decode failed: %d", ret);
		return ret;
	}

	LOG_DBG("Decoded %zu bytes from %zu base64 chars", decoded_len, encoded_len);
	return (int)decoded_len;
}

/**
 * Parse and validate JWT header
 * 
 * @param header_json Decoded header JSON string
 * @param header_len Length of header JSON
 * @param header_out Output structure for parsed header
 * @return 0 on success, negative error code on failure
 */
static int parse_jwt_header(const uint8_t *header_json, int header_len, 
                           struct jwt_header *header_out)
{
	static const struct json_obj_descr header_descr[] = {
		JSON_OBJ_DESCR_PRIM(struct jwt_header, alg, JSON_TOK_STRING),
		JSON_OBJ_DESCR_PRIM(struct jwt_header, typ, JSON_TOK_STRING),
		JSON_OBJ_DESCR_PRIM(struct jwt_header, kid, JSON_TOK_STRING),
	};
	
	char header_str[256];
	int ret;

	if ((header_json == NULL) || (header_len <= 0) || (header_out == NULL)) {
		LOG_ERR("Invalid header parameters");
		return -EINVAL;
	}

	/* Ensure header is null-terminated string */
	if (header_len >= sizeof(header_str)) {
		LOG_ERR("Header too large: %d bytes (max: %zu)", header_len, sizeof(header_str) - 1);
		return -ENOSPC;
	}
	
	memcpy(header_str, header_json, header_len);
	header_str[header_len] = '\0';

	LOG_DBG("Parsing JWT header: %s", header_str);

	/* Parse JSON header */
	memset(header_out, 0, sizeof(*header_out));
	ret = json_obj_parse(header_str, header_len, header_descr, 
	                    ARRAY_SIZE(header_descr), header_out);
	if (ret < 0) {
		LOG_ERR("Failed to parse header JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate required fields */
	if (header_out->alg == NULL || strlen(header_out->alg) == 0) {
		LOG_ERR("Missing or empty 'alg' field in header");
		return -EINVAL;
	}
	
	if (header_out->typ == NULL || strlen(header_out->typ) == 0) {
		LOG_ERR("Missing or empty 'typ' field in header");
		return -EINVAL;
	}

	/* Validate type is JWT */
	if (strcmp(header_out->typ, "JWT") != 0) {
		LOG_ERR("Invalid token type: '%s' (expected: 'JWT')", header_out->typ);
		return -EINVAL;
	}

#if defined(CONFIG_MCP_HTTP_AUTH_EXPECTED_ALGORITHM)
	/* Validate algorithm if configured */
	if (strcmp(header_out->alg, CONFIG_MCP_HTTP_AUTH_EXPECTED_ALGORITHM) != 0) {
		LOG_ERR("Unsupported algorithm: '%s' (expected: '%s')", 
		        header_out->alg, CONFIG_MCP_HTTP_AUTH_EXPECTED_ALGORITHM);
		return -EINVAL;
	}
#endif

	LOG_INF("JWT Header - alg: %s, typ: %s, kid: %s",
	        header_out->alg, header_out->typ, 
	        header_out->kid ? header_out->kid : "(none)");

	return 0;
}

/**
 * Validate authentication token
 * 
 * @param token Decoded token bytes
 * @param token_len Length of token in bytes
 * @return 0 if token is valid, negative error code otherwise
 */
static int validate_token(const uint8_t *token, int token_len)
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

/**
 * Preprocess and validate JWT authentication token
 * 
 * @param jwt_token Full JWT token string (header.payload.signature)
 * @return 0 if token is valid, negative error code otherwise
 */
int preprocess_and_validate_token(const char *jwt_token)
{
	const char *header_b64, *payload_b64, *signature_b64;
	size_t header_b64_len, payload_b64_len, signature_b64_len;
	uint8_t header_decoded[256];
	uint8_t payload_decoded[512];
	struct jwt_header header;
	int header_len, payload_len;
	int ret;

	if (jwt_token == NULL) {
		LOG_ERR("NULL JWT token");
		return -EINVAL;
	}

	LOG_DBG("Processing JWT token");

	/* Split the JWT into its three components */
	ret = split_jwt_token(jwt_token,
	                     &header_b64, &header_b64_len,
	                     &payload_b64, &payload_b64_len,
	                     &signature_b64, &signature_b64_len);
	if (ret != 0) {
		LOG_ERR("Failed to split JWT token: %d", ret);
		return ret;
	}

	/* Decode header (for logging/debugging purposes) */
	header_len = decode_jwt_component(header_b64, header_b64_len,
	                                  header_decoded, sizeof(header_decoded));
	if (header_len < 0) {
		LOG_ERR("Failed to decode JWT header: %d", header_len);
		return header_len;
	}

	/* Null-terminate header*/
	if (header_len < sizeof(header_decoded)) {
		header_decoded[header_len] = '\0';
		LOG_DBG("JWT Header: %s", header_decoded);
	}

	/* Parse and validate header */
	ret = parse_jwt_header(header_decoded, header_len, &header);
	if (ret != 0) {
		LOG_ERR("Failed to parse JWT header: %d", ret);
		return ret;
	}

	/* Decode payload (this contains the claims we need to validate) */
	payload_len = decode_jwt_component(payload_b64, payload_b64_len,
	                                   payload_decoded, sizeof(payload_decoded));
	if (payload_len < 0) {
		LOG_ERR("Failed to decode JWT payload: %d", payload_len);
		return payload_len;
	}

	/* Null-terminate payload */
	if (payload_len < sizeof(payload_decoded)) {
		payload_decoded[payload_len] = '\0';
		LOG_DBG("JWT Payload: %s", payload_decoded);
	} else {
		LOG_ERR("Payload too large for buffer");
		return -ENOSPC;
	}

	/* Note: Signature verification would go here in a production system */
	/* The signature would be verified using the algorithm from header.alg */
	/* and the key identified by header.kid */
	LOG_DBG("JWT Signature (base64): %.*s", (int)signature_b64_len, signature_b64);
	//todo: verify_signature

	/* Validate the decoded payload (claims) */
	ret = validate_token(payload_decoded, payload_len);
	if (ret != 0) {
		LOG_ERR("Token validation failed: %d", ret);
		return ret;
	}

	LOG_INF("JWT token preprocessed and validated successfully");
	return 0;
}

#endif /* CONFIG_MCP_HTTP_AUTH_ENABLED */