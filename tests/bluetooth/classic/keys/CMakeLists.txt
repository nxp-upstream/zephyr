# SPDX-FileCopyrightText: Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(classic_keys)

target_sources(app PRIVATE src/main.c src/hci.c)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/bluetooth)
set_property(SOURCE ${ZEPHYR_BASE}/subsys/bluetooth/host/conn.c
  DIRECTORY ${ZEPHYR_BASE}/subsys/bluetooth/host
  APPEND PROPERTY COMPILE_DEFINITIONS
    bt_conn_foreach=real_bt_conn_foreach
    bt_conn_lookup_handle=real_bt_conn_lookup_handle
    bt_conn_lookup_addr_br=real_bt_conn_lookup_addr_br
    bt_conn_set_state=real_bt_conn_set_state
    bt_conn_disconnect=real_bt_conn_disconnect
    bt_conn_unref=real_bt_conn_unref
)
set_property(SOURCE ${ZEPHYR_BASE}/subsys/bluetooth/host/settings.c
  DIRECTORY ${ZEPHYR_BASE}/subsys/bluetooth/host
  APPEND PROPERTY COMPILE_DEFINITIONS
    bt_settings_delete_link_key=real_bt_settings_delete_link_key
)
