# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cdc_multi)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
list(APPEND app_sources src/main.c)

if (DEFINED CONFIG_APP_UART_TEST)
	list(APPEND app_sources src/uart_test.c)
endif()

target_sources(app PRIVATE ${app_sources})
