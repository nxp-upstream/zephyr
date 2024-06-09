# Copyright 2023 NXP
#
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(soc.c kw45_platform_init.S)
zephyr_sources_ifdef(CONFIG_PM power.c)

zephyr_linker_sources_ifdef(CONFIG_BT RAM_SECTIONS sections.ld)
