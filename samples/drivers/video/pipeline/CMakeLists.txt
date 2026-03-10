# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(video_pipeline)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# Multi-core support
if(CONFIG_INCLUDE_REMOTE_DIR)
  set(REMOTE_ZEPHYR_DIR ${CMAKE_CURRENT_BINARY_DIR}/../remote/zephyr)
  target_include_directories(zephyr_interface
    INTERFACE ${REMOTE_ZEPHYR_DIR}/include/public)
endif()

# TODO: eRPC and rpmsg-lite should fix this dependency inversion by using Kconfig instead of
# including application-specific erpc_config.h and rpmsg_config.h back to the libraries
if(CONFIG_MP_RPC_BACKEND_ERPC)
    zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/erpc)
    if (CONFIG_MP_ERPC_TRANSPORT_RPMSGLITE)
        zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/erpc/rpmsg_lite)
    endif()
endif()
