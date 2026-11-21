# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tee_dec_multicore)

# The pipeline is described in pipeline.dtsi: the application brings the
# stream it pushes and what it draws on the frames
target_sources(app PRIVATE src/main.c)

if(CONFIG_SAMPLE_INCLUDE_REMOTE_DIR)
  target_include_directories(zephyr_interface
    INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/../remote/zephyr/include/public)
endif()

include(../common/mjpeg.cmake)
mpipe_sample_mjpeg_generate(jpeg_filter)
