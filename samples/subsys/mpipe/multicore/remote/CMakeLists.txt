# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(multicore_remote)

# The server side of the video transform interface, generated from the same
# definition as the client element of the Multimedia Pipeline.
find_program(ERPCGEN erpcgen REQUIRED)
set(erpc_idl ${ZEPHYR_BASE}/subsys/mpipe/vid/erpc/mpipe_vid_transform.erpc)
set(erpc_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/erpc)
set(erpc_gen_sources
  ${erpc_gen_dir}/c_mpipe_vid_transform_server.cpp
  ${erpc_gen_dir}/mpipe_vid_transform_server.cpp
  ${erpc_gen_dir}/mpipe_vid_transform_interface.cpp
)
add_custom_command(
  OUTPUT
    ${erpc_gen_sources}
    ${erpc_gen_dir}/c_mpipe_vid_transform_server.h
    ${erpc_gen_dir}/mpipe_vid_transform_common.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${erpc_gen_dir}
  COMMAND ${ERPCGEN} -g c -o ${erpc_gen_dir} ${erpc_idl}
  DEPENDS ${erpc_idl}
  COMMENT "Generating the eRPC server of the video transform"
)

target_include_directories(app PRIVATE ${erpc_gen_dir})
target_sources(app PRIVATE src/main.c ${erpc_gen_sources})
