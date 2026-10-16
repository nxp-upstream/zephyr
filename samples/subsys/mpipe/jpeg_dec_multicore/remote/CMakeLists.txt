# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(jpeg_dec_multicore_remote)

# The server side of the interfaces the video transform client of the
# Multimedia Pipeline speaks, generated from the same definitions.
include(${ZEPHYR_BASE}/subsys/mpipe/erpc/mpipe_erpc.cmake)
mpipe_erpc_generate(app
  ${ZEPHYR_BASE}/subsys/mpipe/erpc/mpipe_stream.erpc
  ${CMAKE_CURRENT_BINARY_DIR}/erpc
  stream_sources
  C_SERVER
)
mpipe_erpc_generate(app
  ${ZEPHYR_BASE}/subsys/mpipe/erpc/vid/mpipe_vid_format.erpc
  ${CMAKE_CURRENT_BINARY_DIR}/erpc
  vid_format_sources
  C_SERVER
)

target_sources(app PRIVATE src/main.c ${stream_sources} ${vid_format_sources})
