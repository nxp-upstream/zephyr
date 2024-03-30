# Copyright 2024 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_CLOCK_MGMT_NXP_SYSCON_SOURCE nxp_syscon_source.c)
