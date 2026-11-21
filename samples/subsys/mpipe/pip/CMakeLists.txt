# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pip_pipeline)

# The pipelines are described in pipeline.dtsi: the application only brings
# the file shown in the window
include(../common/mjpeg.cmake)
mpipe_sample_mjpeg_file(file_src window_filter)
