# SPDX-FileCopyrightText: Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(power_queries)

if(CONFIG_PM)
  target_sources(app PRIVATE
    src/memory.c
    src/power_domains.c
    src/power_states.c
    src/regulators.c
    src/stay_awake.c
  )
  target_sources_ifdef(CONFIG_CPU_FREQ app PRIVATE src/run_modes.c)
else()
  # With CONFIG_PM the kernel arms vbat_lptmr as the system timer's low-power companion.
  target_sources(app PRIVATE src/vbat_lptmr_alarm.c)
endif()
