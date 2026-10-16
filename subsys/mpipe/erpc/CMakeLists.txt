#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

include(${CMAKE_CURRENT_LIST_DIR}/mpipe_erpc.cmake)

if(CONFIG_MPIPE_ERPC OR CONFIG_MPIPE_ERPC_SERVER)
  # The interface every client element and its server speak
  mpipe_erpc_generate(${ZEPHYR_CURRENT_LIBRARY}
    ${CMAKE_CURRENT_LIST_DIR}/mpipe_stream.erpc
    ${ZEPHYR_BINARY_DIR}/generated/mpipe/erpc
    mpipe_stream_erpc_sources
  )
  zephyr_library_sources(mpipe_erpc_objects.cpp ${mpipe_stream_erpc_sources})
  zephyr_library_sources_ifdef(CONFIG_MPIPE_ERPC mpipe_erpc.c)
  zephyr_library_sources_ifdef(CONFIG_MPIPE_ERPC_SERVER mpipe_erpc_server.c)
endif()

if(CONFIG_MPIPE_ERPC_VID OR CONFIG_MPIPE_ERPC_VID_SERVER)
  # The format interface of the video client elements and of their servers.
  # A server that is not built with this library generates its side from the
  # same file.
  mpipe_erpc_generate(${ZEPHYR_CURRENT_LIBRARY}
    ${CMAKE_CURRENT_LIST_DIR}/vid/mpipe_vid_format.erpc
    ${ZEPHYR_BINARY_DIR}/generated/mpipe/erpc
    mpipe_vid_format_erpc_sources
  )
  zephyr_library_sources(vid/mpipe_erpc_vid_objects.cpp ${mpipe_vid_format_erpc_sources})
  zephyr_library_sources_ifdef(CONFIG_MPIPE_ERPC_VID vid/mpipe_erpc_vid.c)
  zephyr_library_sources_ifdef(CONFIG_MPIPE_ERPC_VID_SERVER vid/mpipe_erpc_vid_server.c)
endif()
