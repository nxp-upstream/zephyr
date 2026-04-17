# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# Allow including private headers from drivers/video
zephyr_library_include_directories(..)

zephyr_library_sources(csi.c)
zephyr_library_sources(phy.c)
zephyr_library_sources(phy_imx95.c)
