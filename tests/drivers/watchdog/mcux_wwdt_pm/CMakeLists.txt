# SPDX-FileCopyrightText: Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
list(APPEND DTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mcux_wwdt_pm)

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${ZEPHYR_BASE}/drivers/watchdog"
  "${ZEPHYR_BASE}/subsys/task_wdt"
  "${ZEPHYR_BASE}/subsys/pm"
)
target_compile_definitions(app PRIVATE
  CONFIG_SOC_FAMILY_MCXN=1
  CONFIG_WDT_MCUX_WWDT_WARNING_INTERRUPT_CFG=0
)
target_sources(app PRIVATE
  src/main.c
  src/task_wdt_test.c
)
