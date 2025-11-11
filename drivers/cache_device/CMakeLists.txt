# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/cache_device.h)

zephyr_library()
zephyr_library_property(ALLOW_EMPTY TRUE)

zephyr_library_sources_ifdef(CONFIG_CACHE_DEVICE_NXP_CACHE64	cache_device_nxp_cache64.c)
