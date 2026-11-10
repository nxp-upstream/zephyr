# SPDX-FileCopyrightText: Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(counter_mcux_rtc)

function(add_rtc_variant name)
  add_library(rtc_test_${name} OBJECT src/main.c)
  add_dependencies(rtc_test_${name} zephyr_generated_headers)
  target_link_libraries(rtc_test_${name} PRIVATE zephyr_interface)
  target_include_directories(rtc_test_${name} BEFORE PRIVATE
    mocks
    ${ZEPHYR_BASE}/drivers/counter
  )
  target_compile_definitions(rtc_test_${name} PRIVATE
    TEST_SUITE=mcux_rtc_${name}
    ${ARGN}
  )
  target_sources(app PRIVATE $<TARGET_OBJECTS:rtc_test_${name}>)
endfunction()

add_rtc_variant(osc TEST_OSC32=1 TEST_MUX=1)
add_rtc_variant(osc_no_timer TEST_OSC32=1 TEST_MUX=1 TEST_NO_TIMER=1)
add_rtc_variant(fallback)
add_rtc_variant(fallback_no_timer TEST_NO_TIMER=1)
add_rtc_variant(lpo TEST_OSC32=1 TEST_MUX=1 TEST_LPO=1)
add_rtc_variant(mux TEST_MUX=2)
add_rtc_variant(provider TEST_NO_OSCE=1)
