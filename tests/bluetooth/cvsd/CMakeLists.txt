# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(bluetooth_cvsd)

set(cvsd_dir ${ZEPHYR_BASE}/samples/bluetooth/classic/handsfree/src)

target_sources(testbinary PRIVATE
  src/main.c
  ${cvsd_dir}/cvsd.c
)

target_include_directories(testbinary PRIVATE ${cvsd_dir})
