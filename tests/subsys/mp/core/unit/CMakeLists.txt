# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mp_core_unit)

target_sources(app PRIVATE
  src/main.c
  src/test_element.c
  src/test_bin.c
  src/test_caps.c
  src/test_pad.c
  src/test_bus.c
  src/test_message.c
  src/test_value.c
  src/test_structure.c
  src/test_buffer.c
  src/test_thread.c
)
