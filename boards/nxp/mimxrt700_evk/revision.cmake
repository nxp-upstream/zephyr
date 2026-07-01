# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

# The only non-default revision is "w25q512nw": the EVK reworked to populate the
# on-board W25Q512NW quad SPI NOR on XSPI0 instead of the default octal
# MX25UM51345G. The default (empty) revision is the as-shipped octal board.
if(DEFINED BOARD_REVISION AND NOT BOARD_REVISION STREQUAL "w25q512nw")
  message(FATAL_ERROR
    "Invalid MIMXRT700-EVK revision '${BOARD_REVISION}'. "
    "Valid revisions: <none> (default octal MX25UM51345G), w25q512nw (reworked W25Q512NW QSPI).")
endif()
