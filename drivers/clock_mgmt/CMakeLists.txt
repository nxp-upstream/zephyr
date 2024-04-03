# Copyright 2024 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_linker_sources(RODATA clock_mgmt_callbacks.ld)

zephyr_library_sources(clock_mgmt_common.c)

add_subdirectory(nxp_syscon)
