# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(webrtc_remote)

target_sources(app PRIVATE src/main.c)

if(CONFIG_CODE_DATA_RELOCATION)
  # The heap the decoder takes its picture buffers from: the RAM of this core
  # is too small for it.
  zephyr_code_relocate(FILES ${ZEPHYR_BASE}/modules/h264swdec/zephyr_h264swdec.c
    LOCATION "${CONFIG_REMOTE_DECODER_RAM_NAME}_NOINIT" NOKEEP)
endif()

# The decoder is most of what this core does: the library is optimized for
# speed, which the rest of the image has no room for
target_compile_options(modules__h264swdec PRIVATE -O2)
