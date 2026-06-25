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

#if defined(CONFIG_MCP_HTTP_AUTH_VERIFY_SIGNATURE)
#include <mbedtls/pk.h>
#include <mbedtls/md.h>
#include <mbedtls/rsa.h>
#include <mbedtls/error.h>
#endif

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

#if defined(CONFIG_MCP_HTTP_AUTH_VERIFY_SIGNATURE)

/**
 * Get RSA public key for verification
 * This is a placeholder - you need to implement key storage/retrieval
 * 
 * @param kid Key ID (can be NULL)
 * @return PEM-encoded public key string, or NULL if not found
 */
static const char *get_public_key_pem(const char *kid)
{
	/* TODO: Implement key storage and retrieval
	 * Options:
	 * 1. Hardcode key in flash (for single key scenarios)
	 * 2. Store multiple keys indexed by kid
	 * 3. Fetch from secure storage
	 * 4. Use device-specific key
	 */
	
	/* Example hardcoded public key (replace with your actual key) */
	static const char *default_public_key = 
		"-----BEGIN PUBLIC KEY-----\n"
		"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...\n"
		"-----END PUBLIC KEY-----\n";
	
	LOG_DBG("Looking up public key for kid: %s", kid ? kid : "(default)");
	
	/* For now, return default key regardless of kid */
	return default_public_key;
}

/**
 * Verify RS256 (RSA with SHA-256) signature using mbedTLS
 */
static int verify_rs256_signature(const char *data, size_t data_len,
                                 const uint8_t *signature, size_t signature_len,
                                 const char *kid)
{
	mbedtls_pk_context pk;
	mbedtls_md_context_t md_ctx;
	const mbedtls_md_info_t *md_info;
	unsigned char hash[32]; /* SHA-256 produces 32 bytes */
	const char *public_key_pem;
	int ret;
	char error_buf[100];

	LOG_DBG("Verifying RS256 signature (kid: %s)", kid ? kid : "(none)");

	/* Get the public key */
	public_key_pem = get_public_key_pem(kid);
	if (public_key_pem == NULL) {
		LOG_ERR("Public key not found for kid: %s", kid ? kid : "(default)");
		return -ENOENT;
	}

	/* Initialize mbedTLS contexts */
	mbedtls_pk_init(&pk);
	mbedtls_md_init(&md_ctx);

	/* Parse the public key */
	ret = mbedtls_pk_parse_public_key(&pk, 
	                                  (const unsigned char *)public_key_pem,
	                                  strlen(public_key_pem) + 1);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, sizeof(error_buf));
		LOG_ERR("Failed to parse public key: -0x%04x (%s)", -ret, error_buf);
		ret = -EINVAL;
		goto cleanup;
	}

	/* Verify it's an RSA key */
	if (mbedtls_pk_get_type(&pk) != MBEDTLS_PK_RSA) {
		LOG_ERR("Key is not an RSA key");
		ret = -EINVAL;
		goto cleanup;
	}

	/* Get SHA-256 message digest info */
	md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if (md_info == NULL) {
		LOG_ERR("Failed to get SHA-256 info");
		ret = -ENOTSUP;
		goto cleanup;
	}

	/* Compute SHA-256 hash of the data */
	ret = mbedtls_md_setup(&md_ctx, md_info, 0);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, sizeof(error_buf));
		LOG_ERR("Failed to setup MD context: -0x%04x (%s)", -ret, error_buf);
		ret = -EINVAL;
		goto cleanup;
	}

	ret = mbedtls_md_starts(&md_ctx);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, sizeof(error_buf));
		LOG_ERR("Failed to start MD: -0x%04x (%s)", -ret, error_buf);
		ret = -EINVAL;
		goto cleanup;
	}

	ret = mbedtls_md_update(&md_ctx, (const unsigned char *)data, data_len);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, sizeof(error_buf));
		LOG_ERR("Failed to update MD: -0x%04x (%s)", -ret, error_buf);
		ret = -EINVAL;
		goto cleanup;
	}

	ret = mbedtls_md_finish(&md_ctx, hash);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, sizeof(error_buf));
		LOG_ERR("Failed to finish MD: -0x%04x (%s)", -ret, error_buf);
		ret = -EINVAL;
		goto cleanup;
	}

	LOG_DBG("Computed SHA-256 hash of signing input");

	/* Verify the signature */
	ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
	                       hash, sizeof(hash),
	                       signature, signature_len);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, sizeof(error_buf));
		LOG_ERR("Signature verification failed: -0x%04x (%s)", -ret, error_buf);
		ret = -EACCES;
		goto cleanup;
	}

	LOG_INF("RS256 signature verified successfully");
	ret = 0;

cleanup:
	mbedtls_md_free(&md_ctx);
	mbedtls_pk_free(&pk);
	return ret;
}

/**
 * Verify JWT signature
 * 
 * @param header_b64 Base64-encoded header (not null-terminated)
 * @param header_b64_len Length of header
 * @param payload_b64 Base64-encoded payload (not null-terminated)
 * @param payload_b64_len Length of payload
 * @param signature_b64 Base64-encoded signature (not null-terminated)
 * @param signature_b64_len Length of signature
 * @param algorithm Algorithm from header (e.g., "RS256", "HS256")
 * @param kid Key ID from header (can be NULL)
 * @return 0 if signature is valid, negative error code otherwise
 */
static int verify_jwt_signature(const char *header_b64, size_t header_b64_len,
                                const char *payload_b64, size_t payload_b64_len,
                                const char *signature_b64, size_t signature_b64_len,
                                const char *algorithm, const char *kid)
{
	uint8_t signature_decoded[512];
	int signature_len;
	char signing_input[2048];
	int signing_input_len;

	if ((header_b64 == NULL) || (payload_b64 == NULL) || (signature_b64 == NULL)) {
		LOG_ERR("Invalid signature verification parameters");
		return -EINVAL;
	}

	if (algorithm == NULL || strlen(algorithm) == 0) {
		LOG_ERR("Missing algorithm for signature verification");
		return -EINVAL;
	}

	LOG_DBG("Verifying signature with algorithm: %s, kid: %s", 
	        algorithm, kid ? kid : "(none)");

	/* Decode the signature */
	signature_len = decode_jwt_component(signature_b64, signature_b64_len,
	                                     signature_decoded, sizeof(signature_decoded));
	if (signature_len < 0) {
		LOG_ERR("Failed to decode signature: %d", signature_len);
		return signature_len;
	}

	LOG_DBG("Decoded signature: %d bytes", signature_len);

	/* Construct the signing input: base64(header).base64(payload) */
	if ((header_b64_len + 1 + payload_b64_len) >= sizeof(signing_input)) {
		LOG_ERR("Signing input too large");
		return -ENOSPC;
	}

	memcpy(signing_input, header_b64, header_b64_len);
	signing_input[header_b64_len] = '.';
	memcpy(signing_input + header_b64_len + 1, payload_b64, payload_b64_len);
	signing_input_len = header_b64_len + 1 + payload_b64_len;

	LOG_DBG("Signing input length: %d bytes", signing_input_len);

	/* Verify signature based on algorithm */
	if (strcmp(algorithm, "RS256") == 0) {
		return verify_rs256_signature(signing_input, signing_input_len,
		                              signature_decoded, signature_len, kid);
	} else if (strcmp(algorithm, "HS256") == 0) {
		//return verify_hs256_signature(signing_input, signing_input_len,
		//                              signature_decoded, signature_len, kid);
		return -EACCES;
	} else if (strcmp(algorithm, "ES256") == 0) {
		//return verify_es256_signature(signing_input, signing_input_len,
		//                              signature_decoded, signature_len, kid);
		return -EACCES;
	} else if (strcmp(algorithm, "none") == 0) {
		LOG_WRN("Algorithm 'none' is not allowed for security reasons");
		return -EACCES;
	} else {
		LOG_ERR("Unsupported algorithm: %s", algorithm);
		return -ENOTSUP;
	}
}

#endif /* CONFIG_MCP_HTTP_AUTH_VERIFY_SIGNATURE */
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

#if defined(MCP_HTTP_AUTH_VERIFY_SIGNATURE)
	/* Note: Signature verification would go here in a production system */
	/* The signature would be verified using the algorithm from header.alg */
	/* and the key identified by header.kid */
	LOG_DBG("JWT Signature (base64): %.*s", (int)signature_b64_len, signature_b64);
	/* Verify signature */
	ret = verify_jwt_signature(header_b64, header_b64_len,
	                          payload_b64, payload_b64_len,
	                          signature_b64, signature_b64_len,
	                          header.alg, header.kid);
	if (ret != 0) {
		LOG_ERR("Signature verification failed: %d", ret);
		return ret;
	}
#endif /* MCP_HTTP_AUTH_VERIFY_SIGNATURE */

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