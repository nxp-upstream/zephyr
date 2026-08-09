#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library_sources_ifdef(CONFIG_MP_DUMP mp_dump.c)
zephyr_library_sources_ifdef(CONFIG_MP_PLAYER mp_player.c)
