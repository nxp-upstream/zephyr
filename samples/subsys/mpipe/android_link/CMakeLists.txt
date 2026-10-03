# Copyright 2024-2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(android_link)

target_sources(app PRIVATE
  src/main.c
  src/touch.c
  src/display.c
  src/control_msg.c
  src/osd.c
)
target_sources_ifdef(CONFIG_ANDROID_LINK_MIC app PRIVATE src/mic_mode.c)

target_include_directories(app PRIVATE src)

if(CONFIG_NET_STACK_CODE_RELOCATE)
  # Relocate the networking stack and the L2 layer into the RAM named by
  # CONFIG_NET_STACK_CODE_RAM_NAME (ITCM on the RT1170), as the original demo does.
  zephyr_code_relocate(LIBRARY subsys__net__ip
    LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY subsys__net
    LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
  if(CONFIG_NET_L2_ETHERNET)
    zephyr_code_relocate(LIBRARY drivers__ethernet
      LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
    zephyr_code_relocate(LIBRARY subsys__net__l2__ethernet
      LOCATION "${CONFIG_NET_STACK_CODE_RAM_NAME}_TEXT" NOKEEP)
  endif()
endif()
