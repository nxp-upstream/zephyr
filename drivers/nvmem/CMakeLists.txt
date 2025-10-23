# Copyright 2025 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_NVMEM_NXP_OCOTP nvmem_nxp_ocotp.c)
zephyr_library_sources_ifdef(CONFIG_NVMEM_AT2X nvmem_at2x.c)
