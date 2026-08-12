#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

if(CONFIG_MPIPE_FS)
  zephyr_library_sources(
    mpipe_filesrc.c
    mpipe_filesink.c
  )
endif()
