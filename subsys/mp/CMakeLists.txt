#
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

if(CONFIG_MP)
    zephyr_library_named(mp)

    # Core framework sources live directly under subsys/mp/*.c
    file(GLOB MP_CORE_SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/*.c)

    set(MP_CORE_OPTIONAL
        "mp_workqueue.c:CONFIG_MP_WORKQUEUE"
        "mp_transform_client.c:CONFIG_MP_RPC"
        "mp_fake_src.c:CONFIG_MP_FAKE_SRC"
    )

    foreach(ELEMENT_CONFIG ${MP_CORE_OPTIONAL})
        string(REPLACE ":" ";" ELEMENT_CONFIG_LIST ${ELEMENT_CONFIG})
        list(GET ELEMENT_CONFIG_LIST 0 ELEMENT_FILE)
        list(GET ELEMENT_CONFIG_LIST 1 ELEMENT_OPTION)

        if(NOT ${ELEMENT_OPTION})
            list(REMOVE_ITEM MP_CORE_SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/${ELEMENT_FILE})
        endif()
    endforeach()

    zephyr_library_sources(${MP_CORE_SOURCE_FILES})

    # Auto-discover plugin and utility subdirectories. Each directory owns a
    # self-guarded CMakeLists.txt (it checks its own CONFIG_MP_* option), so
    # adding a new plugin needs no change to this file.
    file(GLOB MP_SUBDIRS RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*)
    foreach(entry ${MP_SUBDIRS})
        if(IS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/${entry}
           AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/${entry}/CMakeLists.txt)
            add_subdirectory(${entry})
        endif()
    endforeach()
endif()
