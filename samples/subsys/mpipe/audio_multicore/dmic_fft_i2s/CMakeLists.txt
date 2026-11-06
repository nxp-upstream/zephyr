# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mpipe_dmic_fft_i2s)

# The HiFi4 computes gain, FFT and IFFT; this core captures and plays back
target_sources(app PRIVATE src/main.c)

include(${CMAKE_CURRENT_LIST_DIR}/dmic_fft_i2s.cmake)

# Boot and image embedding of the HiFi4: the loader of the RTxxx AMP samples
# (dsp.c, dspimgs.S). dspimgs.S includes the reset, text and data sections of
# the HiFi4 image, which are either the prebuilt binaries that sysbuild.cmake
# selected, or the ones of the sysbuild image "remote".
set(RTXXX_DSP_COMMON ${ZEPHYR_BASE}/samples/boards/nxp/adsp/rtxxx/common)

target_sources(app PRIVATE ${RTXXX_DSP_COMMON}/src/dsp.c ${RTXXX_DSP_COMMON}/src/dspimgs.S)
include_directories(${RTXXX_DSP_COMMON}/src)

zephyr_get(HIFI4_BIN_RESET SYSBUILD GLOBAL)
zephyr_get(HIFI4_BIN_TEXT SYSBUILD GLOBAL)
zephyr_get(HIFI4_BIN_DATA SYSBUILD GLOBAL)

if(DEFINED HIFI4_BIN_RESET AND DEFINED HIFI4_BIN_TEXT AND DEFINED HIFI4_BIN_DATA)
  set(hifi4_bin_depends ${HIFI4_BIN_RESET} ${HIFI4_BIN_TEXT} ${HIFI4_BIN_DATA})
else()
  set(hifi4_build_dir ${CMAKE_CURRENT_BINARY_DIR}/../remote/zephyr)
  set(HIFI4_BIN_RESET ${hifi4_build_dir}/zephyr.reset.bin)
  set(HIFI4_BIN_TEXT ${hifi4_build_dir}/zephyr.text.bin)
  set(HIFI4_BIN_DATA ${hifi4_build_dir}/zephyr.data.bin)
  set(hifi4_bin_depends ${hifi4_build_dir}/zephyr.elf)
endif()

set(hifi4_bin_syms
  "HIFI4_BIN_RESET=\"${HIFI4_BIN_RESET}\""
  "HIFI4_BIN_TEXT=\"${HIFI4_BIN_TEXT}\""
  "HIFI4_BIN_DATA=\"${HIFI4_BIN_DATA}\""
)

set_source_files_properties(
  ${RTXXX_DSP_COMMON}/src/dspimgs.S
  PROPERTIES
  COMPILE_DEFINITIONS "${hifi4_bin_syms}"
  OBJECT_DEPENDS "${hifi4_bin_depends}"
)
