#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_program(ERPCGEN erpcgen REQUIRED)

project(remote)

set(ERPC_DIR ${CMAKE_CURRENT_LIST_DIR}/src/erpc)
add_custom_command(
    OUTPUT
        ${ERPC_DIR}/c_mp_zvid_transform_server.cpp
        ${ERPC_DIR}/mp_zvid_transform_server.cpp
        ${ERPC_DIR}/mp_zvid_transform_interface.cpp
    COMMAND
        ${ERPCGEN} -v -g c -o ${ERPC_DIR} ${ERPC_DIR}/mp_zvid_transform_server.erpc
    COMMAND
        ${CMAKE_COMMAND} -E rm -f ${ERPC_DIR}/*client.cpp ${ERPC_DIR}/*client.h ${ERPC_DIR}/*client.hpp
    DEPENDS
        ${ERPC_DIR}/mp_zvid_transform_server.erpc
)

target_sources(app PRIVATE
    src/main.c
    src/jpeg.c
    ${ERPC_DIR}/erpc_error_handler.c
    ${ERPC_DIR}/c_mp_zvid_transform_server.cpp
    ${ERPC_DIR}/mp_zvid_transform_server.cpp
    ${ERPC_DIR}/mp_zvid_transform_interface.cpp
)

# TODO: eRPC and rpmsg-lite should fix this dependency inversion by using Kconfig instead of
# including application-specific erpc_config.h and rpmsg_config.h back to the libraries
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/erpc)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/erpc/rpmsg_lite)
