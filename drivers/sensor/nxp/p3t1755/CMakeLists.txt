# Copyright 2024 NXP
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(p3t1755.c)
zephyr_library_sources_ifdef(CONFIG_P3T1755_TRIGGER    p3t1755_trigger.c)
