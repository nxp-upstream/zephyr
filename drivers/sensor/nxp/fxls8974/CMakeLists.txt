# Makefile - FXLS8974 6-axis accelerometer/magnetometer
#
# Copyright (c) 2016, Freescale Semiconductor, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
zephyr_library()

zephyr_library_sources(fxls8974.c)
zephyr_library_sources_ifdef(CONFIG_FXLS8974_TRIGGER fxls8974_trigger.c)
