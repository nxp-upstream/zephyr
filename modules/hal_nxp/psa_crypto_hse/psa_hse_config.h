/*
 * Copyright NXP 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PSA_HSE_CONFIG_H
#define PSA_HSE_CONFIG_H

#define PSA_CRYPTO_DRIVER_NXP_HSE
#define PSA_KEY_LOCATION_VENDOR_FLAG            ((psa_key_location_t) 0x800000)
#define PSA_KEY_LOCATION_HSE                     ((psa_key_location_t)PSA_KEY_LOCATION_VENDOR_FLAG|0x1)

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define PSA_CRYPTO_ACCELERATOR_DRIVER_PRESENT
#define S32Z280

/* Internal macros meant to be called only from within the library. */
#define MBEDTLS_INTERNAL_VALIDATE_RET( cond, ret )	do { } while( 0 )
#define MBEDTLS_INTERNAL_VALIDATE( cond )		do { } while( 0 )

/* Bad input parameters to function. */
#define MBEDTLS_ERR_md5_ALT_BAD_INPUT_DATA		-0x0077
/* md5 input length is invalid. */
#define MBEDTLS_ERR_md5_ALT_INVALID_INPUT_LENGTH	-0x0076

/* Use RNG from PSA Crypto driver */
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG

/* Require by PSA Crypto driver, missing Kconfig symbol in Zephyr */
#define MBEDTLS_USE_NXP_HSE_HASH_MD5_HMAC_HASH_384_512

#endif /* PSA_HSE_CONFIG_H */
