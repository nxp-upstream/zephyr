#
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library_sources(
    mp_zvid_buffer_pool.c
    mp_zvid_object.c
    mp_zvid_src.c
    mp_zvid_transform.c
)

if(CONFIG_MP_RPC)
    zephyr_library_sources(
        mp_zvid_buffer_pool_client.c
        mp_zvid_transform_client.c
    )

    if(CONFIG_MP_RPC_BACKEND_ERPC)
        find_program(ERPCGEN erpcgen REQUIRED)
        set(ERPC_DIR ${CMAKE_CURRENT_LIST_DIR}/erpc)
        set(ERPC_GENERATED_FILES
            ${ERPC_DIR}/mp_zvid_transform_common.h
            ${ERPC_DIR}/c_mp_zvid_transform_client.h
            ${ERPC_DIR}/c_mp_zvid_transform_client.cpp
            ${ERPC_DIR}/mp_zvid_transform_client.cpp
            ${ERPC_DIR}/mp_zvid_transform_interface.cpp
        )
        add_custom_command(
            OUTPUT ${ERPC_GENERATED_FILES}
            COMMAND
                ${ERPCGEN} -v -g c -o ${ERPC_DIR} ${ERPC_DIR}/mp_zvid_transform.erpc
            COMMAND
                ${CMAKE_COMMAND} -E rm -f ${ERPC_DIR}/*server.cpp ${ERPC_DIR}/*server.h ${ERPC_DIR}/*server.hpp
            DEPENDS
                ${ERPC_DIR}/mp_zvid_transform.erpc
                ${ERPC_DIR}/external.h
        )
        add_custom_target(generate_erpc_client DEPENDS ${ERPC_GENERATED_FILES})
        add_dependencies(${ZEPHYR_CURRENT_LIBRARY} generate_erpc_client)
        zephyr_library_sources(
            mp_zvid_transform_client_erpc.c
            ${ERPC_DIR}/c_mp_zvid_transform_client.cpp
            ${ERPC_DIR}/mp_zvid_transform_client.cpp
            ${ERPC_DIR}/mp_zvid_transform_interface.cpp
        )
    endif()
endif()
