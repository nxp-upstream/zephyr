# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mpipe_h264_enc_dec)

target_sources(app PRIVATE src/main.c)

if(CONFIG_CODEC_CODE_RELOCATE)
  # The code every frame goes through: the two codec libraries and the pixel
  # format conversions.
  zephyr_code_relocate(LIBRARY modules__minih264
    LOCATION "${CONFIG_CODEC_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(LIBRARY modules__h264swdec
    LOCATION "${CONFIG_CODEC_CODE_RAM_NAME}_TEXT" NOKEEP)
  zephyr_code_relocate(FILES ${ZEPHYR_BASE}/subsys/mpipe/vid/mpipe_vid_convert_table.c
    LOCATION "${CONFIG_CODEC_CODE_RAM_NAME}_TEXT" NOKEEP)
endif()

if(CONFIG_CODEC_DATA_RELOCATE)
  # The memory the codecs work in: the state and working area of the encoder,
  # the heap of the decoder.
  zephyr_code_relocate(FILES ${ZEPHYR_BASE}/subsys/mpipe/vid/mpipe_vid_h264_encoder.c
    LOCATION "${CONFIG_CODEC_ENCODER_RAM_NAME}_BSS" NOKEEP)
  zephyr_code_relocate(FILES ${ZEPHYR_BASE}/modules/h264swdec/zephyr_h264swdec.c
    LOCATION "${CONFIG_CODEC_DECODER_RAM_NAME}_NOINIT" NOKEEP)
endif()
