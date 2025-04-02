# Copyright 2025 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psa_crypto_hse)

set(PSA_CRYPTO_HSE_PATH $ENV{PSA_CRYPTO_HSE_PATH})

target_include_directories(app PRIVATE
  ${PSA_CRYPTO_HSE_PATH}/demo/common/application/psa_demo
  ${PSA_CRYPTO_HSE_PATH}/demo/common/inc
)

# Re-use the demo from PSA_wHSE package
target_compile_definitions(app PRIVATE PSA_DEMO)
target_sources(app PRIVATE
  ${PSA_CRYPTO_HSE_PATH}/demo/common/src/global_variables.c
  ${PSA_CRYPTO_HSE_PATH}/demo/common/application/psa_demo/psa_demo.c
  src/main.c
)

# Variables from the demo should be put in non-cacheable region
zephyr_linker_sources(NOCACHE_SECTION nocache.ld)
