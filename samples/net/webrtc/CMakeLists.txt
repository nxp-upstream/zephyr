# SPDX-FileCopyrightText: Copyright (c) 2026 Meta Platforms, Inc.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(webrtc LANGUAGES C)

zephyr_include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_sources(app PRIVATE
  src/main.c
  src/platform_zephyr.c
  src/fault_trace_zephyr.c
  src/libpeer_transport_probe.c
  src/libpeer_endpoint.c
)

if(CONFIG_ZEPHYRTC_AUDIO_RT1170)
  set(MCUX_TEMPSENSOR_DIR
      "${ZEPHYR_HAL_NXP_MODULE_DIR}/mcux/mcux-sdk-ng/drivers/tempsensor")
  set(MCUX_RT1170_POWER_DIR
      "${ZEPHYR_HAL_NXP_MODULE_DIR}/mcux/mcux-sdk-ng/devices/RT/RT1170/MIMXRT1176/drivers/power")
  target_include_directories(app PRIVATE
    "${MCUX_TEMPSENSOR_DIR}"
    "${MCUX_RT1170_POWER_DIR}"
  )
  target_sources(app PRIVATE
    "${MCUX_TEMPSENSOR_DIR}/fsl_tempsensor.c"
    "${MCUX_RT1170_POWER_DIR}/fsl_soc_src.c"
  )
  target_sources_ifndef(CONFIG_ZEPHYRTC_MEDIA_MPIPE app PRIVATE src/media_rt1170_zephyr.c)
  # The SDK drivers do not build cleanly with the sample's warning flags.
  set_source_files_properties(
    "${MCUX_TEMPSENSOR_DIR}/fsl_tempsensor.c"
    "${MCUX_RT1170_POWER_DIR}/fsl_soc_src.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-error"
  )
elseif(NOT CONFIG_ZEPHYRTC_MEDIA_MPIPE)
  target_sources(app PRIVATE src/media_synthetic_zephyr.c)
endif()
if(CONFIG_ZEPHYRTC_MEDIA_MPIPE)
  target_sources(app PRIVATE src/media_mpipe.c)
  # The zbus listener macro leaves the parameters of the fields it does not
  # use, which the warning flags of the sample turn into errors.
  set_source_files_properties(src/media_mpipe.c PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-parameter"
  )
endif()

target_sources_ifdef(CONFIG_ZEPHYRTC_MODE_LOOPBACK app PRIVATE
  src/libpeer_loopback.c
)
target_sources_ifdef(CONFIG_ZEPHYRTC_SIGNALING_CLIENT app PRIVATE
  src/signaling_client_zephyr.c
  src/libpeer_offerer.c
)
target_sources_ifdef(CONFIG_ZEPHYRTC_MODE_HTTP_SIGNALING app PRIVATE
  src/signaling_test_server.c
  src/signaling_tests.c
)
target_sources_ifdef(CONFIG_ZEPHYRTC_MODE_EXTERNAL app PRIVATE
  src/libpeer_external.c
)
target_sources_ifdef(CONFIG_ZEPHYRTC_MODE_SELF_CALL app PRIVATE
  src/libpeer_self_call.c
)

if(CONFIG_ZEPHYRTC_VIDEO_H264 AND NOT CONFIG_ZEPHYRTC_MEDIA_MPIPE)
  target_sources(app PRIVATE
    src/video_codec_zephyr.c
    src/video_color_zephyr.c
  )
  set_source_files_properties(src/video_codec_zephyr.c PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-function;-Wno-sign-compare;-Wno-strict-prototypes"
  )
endif()

target_compile_options(app PRIVATE
  -Wall
  -Wextra
  -Wformat=2
  -Wmissing-prototypes
  -Wstrict-prototypes
  -Werror=implicit-function-declaration
  -Werror=incompatible-pointer-types
  -Werror=return-type
)

if(CONFIG_ZEPHYRTC_CODE_RELOCATE)
  # The code every frame goes through: the two codec libraries and the pixel
  # format conversions.
  zephyr_code_relocate(LIBRARY modules__minih264
    LOCATION "${CONFIG_ZEPHYRTC_MEDIA_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY modules__h264swdec
    LOCATION "${CONFIG_ZEPHYRTC_MEDIA_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(FILES ${ZEPHYR_BASE}/subsys/mpipe/vid/mpipe_vid_convert_table.c
    LOCATION "${CONFIG_ZEPHYRTC_MEDIA_CODE_RAM_NAME}_TEXT" NOKEEP)
  # The code every packet goes through: SRTP with its cipher tables, the
  # WebRTC stack and the IP stack.
  zephyr_code_relocate(LIBRARY libpeer_srtp
    LOCATION "${CONFIG_ZEPHYRTC_MEDIA_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(
    FILES ${ZEPHYR_LIBPEER_MODULE_DIR}/third_party/libsrtp/crypto/cipher/aes.c
    LOCATION "${CONFIG_ZEPHYRTC_MEDIA_CODE_RAM_NAME}_RODATA" NOKEEP)
  zephyr_code_relocate(LIBRARY libpeer
    LOCATION "${CONFIG_ZEPHYRTC_MEDIA_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY subsys__net__ip
    LOCATION "${CONFIG_ZEPHYRTC_NET_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY subsys__net
    LOCATION "${CONFIG_ZEPHYRTC_NET_CODE_RAM_NAME}_TEXT" NOKEEP)
endif()

if(CONFIG_ZEPHYRTC_CODEC_DATA_RELOCATE)
  # The memory the codecs work in: the state and working area of the encoder,
  # the heap of the decoder.
  zephyr_code_relocate(FILES ${ZEPHYR_BASE}/subsys/mpipe/vid/mpipe_vid_h264_encoder.c
    LOCATION "${CONFIG_ZEPHYRTC_ENCODER_RAM_NAME}_BSS" NOKEEP)
  if(NOT CONFIG_ZEPHYRTC_DECODER_REMOTE)
    zephyr_code_relocate(FILES ${ZEPHYR_BASE}/modules/h264swdec/zephyr_h264swdec.c
      LOCATION "${CONFIG_ZEPHYRTC_DECODER_RAM_NAME}_NOINIT" NOKEEP)
  endif()
endif()

if(CONFIG_SECOND_CORE_MCUX)
  # The start-up code of the SoC loads the image of the second core, whose
  # description the sysbuild leaves next to this image
  target_include_directories(zephyr_interface
    INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/../remote/zephyr/include/public)
endif()
