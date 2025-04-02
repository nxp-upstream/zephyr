# Copyright NXP 2025
# SPDX-License-Identifier: Apache-2.0

message ("psa-crypto-driver module is included.")

set(NXP_MODULE_MBEDTLS_PATH ${ZEPHYR_MBEDTLS_MODULE_DIR}/../)
set(NXP_MODULE_HAL_PATH ${ZEPHYR_HAL_NXP_MODULE_DIR})

if(DEFINED ENV{PSA_CRYPTO_HSE_PATH})
  set(PSA_CRYPTO_HSE_PATH $ENV{PSA_CRYPTO_HSE_PATH})
  message(STATUS "PSA_CRYPTO_HSE_PATH = ${PSA_CRYPTO_HSE_PATH}")
else()
  message(FATAL_ERROR "PSA_CRYPTO_HSE_PATH is not set")
endif()

zephyr_library_named(psa_crypto_driver_hse)
zephyr_compile_options("SHELL: $<TARGET_PROPERTY:compiler,imacros> psa_hse_config.h")

zephyr_include_directories(
  .
  ${PSA_CRYPTO_HSE_PATH}/demo/common/inc
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/mbedtls/3rdparty/nxp_hse/include/alt_layer
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/mbedtls/3rdparty/nxp_hse/include/psa_layer
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/nxp_hse_al/hse_dal/include
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/nxp_hse_al/keystore_mgmt/include

  ${NXP_MODULE_MBEDTLS_PATH}/mbedtls/include/psa
  ${NXP_MODULE_MBEDTLS_PATH}/mbedtls/library

  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/HSE/interface
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/HSE/interface/config
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/HSE/interface/inc_common
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/HSE/interface/inc_custom
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/HSE/interface/inc_services

  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/Crypto/include
  ${NXP_MODULE_HAL_PATH}/s32/soc/s32z270/include
)

FILE(GLOB psa_layer_sources
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/mbedtls/3rdparty/nxp_hse/library/psa_layer/*.c
)

foreach(src_file ${psa_layer_sources})
  set_source_files_properties(
    ${src_file}
    PROPERTIES COMPILE_OPTIONS "-include;${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/HSE/interface/hse_interface.h"
  )
endforeach()

# To include under specific condition, need to fix in PSA Crypto driver
list(FILTER psa_layer_sources EXCLUDE REGEX nxp_hse_psa_crypto_cmac.c)
list(FILTER psa_layer_sources EXCLUDE REGEX nxp_hse_psa_crypto_aead.c)

FILE(GLOB hse_dal_sources
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/nxp_hse_al/hse_dal/src/*.c
  ${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/nxp_hse_al/keystore_mgmt/src/*.c
)
# Remove hse_host_timming.c, trustedkeystore_interface.c which are not used
list(FILTER hse_dal_sources EXCLUDE REGEX hse_host_timing.c)
list(FILTER hse_dal_sources EXCLUDE REGEX trustedkeystore_interface.c)

zephyr_library_sources(
  ${psa_layer_sources}
  ${hse_dal_sources}
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/Rte/src/SchM_Crypto_43_HSE.c
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/Crypto/src/Hse_Ip.c
  ${NXP_MODULE_HAL_PATH}/s32/drivers/s32ze/Crypto/src/Mu_Ip_Irq.c
)

if(CONFIG_PSA_WANT_ALG_CMAC)
  zephyr_library_sources(${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/mbedtls/3rdparty/nxp_hse/library/psa_layer/nxp_hse_psa_crypto_cmac.c)
endif()

if(CONFIG_PSA_WANT_ALG_CCM OR CONFIG_PSA_WANT_ALG_GCM)
  zephyr_library_sources(${PSA_CRYPTO_HSE_PATH}/mbedtls_with_hse/mbedtls/3rdparty/nxp_hse/library/psa_layer/nxp_hse_psa_crypto_aead.c)
endif()

zephyr_library_sources(psa_hse_wrapper.c)
# Override the implementation of mbedtls_ms_time from PSA Crypto HSE driver
target_link_libraries(
  ${ZEPHYR_CURRENT_LIBRARY}
  INTERFACE
  "-Wl,--wrap=mbedtls_ms_time"
)

zephyr_linker_sources(NOCACHE_SECTION psa_hse_nocache.ld)
zephyr_library_link_libraries(mbedTLS)
