# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

# Remote image of the mpipe audio multicore samples: an eRPC server, generated
# from the same definition as the client element of the Multimedia Pipeline,
# that runs the audio functions enabled in Kconfig. The sysbuild.cmake of each
# sample adds this image and selects the functions its pipeline opens.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mpipe_aud_remote)

target_sources(app PRIVATE main.c aud_remote_log.c)
target_sources_ifdef(CONFIG_AUD_REMOTE_FUNC_GAIN app PRIVATE func_gain.c)
target_sources_ifdef(CONFIG_AUD_REMOTE_FUNC_FFT app PRIVATE func_fft.c)
target_sources_ifdef(CONFIG_AUD_REMOTE_FUNC_IFFT app PRIVATE func_ifft.c)

# fft and ifft share the transform
if(CONFIG_AUD_REMOTE_FUNC_FFT OR CONFIG_AUD_REMOTE_FUNC_IFFT)
  target_sources(app PRIVATE fft.c)
endif()

find_program(ERPCGEN erpcgen REQUIRED)
set(erpc_idl ${ZEPHYR_BASE}/subsys/mpipe/aud/erpc/mpipe_aud_transform.erpc)
set(erpc_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/erpc)
set(erpc_gen_sources
  ${erpc_gen_dir}/c_mpipe_aud_transform_server.cpp
  ${erpc_gen_dir}/mpipe_aud_transform_server.cpp
  ${erpc_gen_dir}/mpipe_aud_transform_interface.cpp
)
add_custom_command(
  OUTPUT
    ${erpc_gen_sources}
    ${erpc_gen_dir}/c_mpipe_aud_transform_server.h
    ${erpc_gen_dir}/mpipe_aud_transform_common.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${erpc_gen_dir}
  COMMAND ${ERPCGEN} -g c -o ${erpc_gen_dir} ${erpc_idl}
  DEPENDS ${erpc_idl}
  COMMENT "Generating the eRPC server of the audio transform and its log"
)

# The server shims take the struct arguments from erpc_malloc(), which the
# Zephyr port provides on k_malloc, but include erpc_port.h only under the
# dynamic allocation policy
set_source_files_properties(${erpc_gen_dir}/mpipe_aud_transform_server.cpp
  PROPERTIES COMPILE_OPTIONS "-include;erpc_port.h"
)

target_include_directories(app PRIVATE ${erpc_gen_dir})
target_sources(app PRIVATE ${erpc_gen_sources})

# Split the image into the sections the Cortex-M33 loads, as the RTxxx AMP samples do
include(${ZEPHYR_BASE}/samples/boards/nxp/adsp/rtxxx/common/remote-dsp-imgs.cmake)
