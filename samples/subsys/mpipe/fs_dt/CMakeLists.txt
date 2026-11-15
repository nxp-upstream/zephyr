# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fs_dt)

# The pipeline is described in pipeline.dtsi: the application only brings the
# file it copies
target_sources(app PRIVATE src/test_file.c)
