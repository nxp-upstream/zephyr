# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(video_pipeline)

# The pipeline is described in the devicetree: the application has no code
target_sources(app PRIVATE ${ZEPHYR_BASE}/misc/empty_file.c)
