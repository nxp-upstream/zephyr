# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(jpeg_dec_dt)

# The pipeline is described in pipeline.dtsi: the application only brings the
# file it plays
include(../common/mjpeg.cmake)
mpipe_sample_mjpeg_file(file_src jpeg_filter)
