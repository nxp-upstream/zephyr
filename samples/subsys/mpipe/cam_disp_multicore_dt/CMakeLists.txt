# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cam_disp_multicore_dt)

if(CONFIG_SAMPLE_INCLUDE_REMOTE_DIR)
  target_include_directories(zephyr_interface
    INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/../remote/zephyr/include/public)
endif()

# The pipeline is described in pipeline.dtsi: the application has no code
target_sources(app PRIVATE ${ZEPHYR_BASE}/misc/empty_file.c)
