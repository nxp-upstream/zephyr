# SPDX-FileCopyrightText: Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

option(LC3_TEST_HOST "Build codec/protocol host tests instead of firmware" OFF)
option(LC3_TEST_HOST_EXTENSIONS "Enable LC3plus and HR in host tests" OFF)

if(LC3_TEST_HOST)
  project(lc3_codec_host_test C)
  set(LIBLC3_DIR "" CACHE PATH "Path to the unmodified liblc3 module")
  if(NOT EXISTS "${LIBLC3_DIR}/include/lc3.h")
    message(FATAL_ERROR "Set LIBLC3_DIR to the west liblc3 checkout")
  endif()
  add_executable(lc3_codec_test
    src/codec.c
    src/protocol.c
    tests/test_codec.c
    "${LIBLC3_DIR}/src/attdet.c"
    "${LIBLC3_DIR}/src/bits.c"
    "${LIBLC3_DIR}/src/bwdet.c"
    "${LIBLC3_DIR}/src/energy.c"
    "${LIBLC3_DIR}/src/lc3.c"
    "${LIBLC3_DIR}/src/ltpf.c"
    "${LIBLC3_DIR}/src/mdct.c"
    "${LIBLC3_DIR}/src/plc.c"
    "${LIBLC3_DIR}/src/sns.c"
    "${LIBLC3_DIR}/src/spec.c"
    "${LIBLC3_DIR}/src/tables.c"
    "${LIBLC3_DIR}/src/tns.c"
  )
  target_include_directories(lc3_codec_test PRIVATE
    src
    "${LIBLC3_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include"
  )
  target_compile_features(lc3_codec_test PRIVATE c_std_11)
  target_compile_options(lc3_codec_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  target_compile_definitions(lc3_codec_test PRIVATE
    CONFIG_ARCH_POSIX=1
    CONFIG_LITTLE_ENDIAN=1
    LC3_PLUS=$<BOOL:${LC3_TEST_HOST_EXTENSIONS}>
    LC3_PLUS_HR=$<BOOL:${LC3_TEST_HOST_EXTENSIONS}>
  )
  if(LC3_TEST_HOST_EXTENSIONS)
    target_compile_definitions(lc3_codec_test PRIVATE CONFIG_LIBLC3_PLUS=1 CONFIG_LIBLC3_PLUS_HR=1)
  endif()
  target_link_libraries(lc3_codec_test PRIVATE m)
  enable_testing()
  add_test(NAME codec_protocol COMMAND lc3_codec_test)
  return()
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lc3_codec_test)

include("${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake")
target_sources(app PRIVATE
  src/main.c
  src/codec.c
  src/protocol.c
)
