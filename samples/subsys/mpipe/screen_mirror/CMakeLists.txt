# Copyright 2024-2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(screen_mirror)

target_sources(app PRIVATE
	src/main.c
	src/touch.c
	src/display.c
	src/control_msg.c
)

target_include_directories(app PRIVATE src)
