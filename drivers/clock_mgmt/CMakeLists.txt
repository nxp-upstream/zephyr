# Copyright 2024 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(clock_mgmt_common.c)
zephyr_library_sources_ifdef(CONFIG_CLOCK_MGMT_FIXED_SOURCE fixed_clock_source.c)

add_subdirectory(nxp_syscon)
