# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

# The HiFi4 image is the audio server, with gain, FFT and IFFT; the Cortex-M33
# image embeds it. Either the Zephyr image ../aud_remote is built here as image
# "remote", or the binaries of a server compiled outside of Zephyr are embedded
# as they are: Zephyr never compiles them.

# Sysbuild passes its cache to every image: the Cortex-M33 reads these in
# dmic_fft_i2s.cmake
foreach(opt SAMPLE_RATE FRAME_INTERVAL_US GAIN_PERCENT)
  set(DMIC_FFT_I2S_${opt} ${SB_CONFIG_DMIC_FFT_I2S_${opt}} CACHE INTERNAL "")
endforeach()

if(SB_CONFIG_DMIC_FFT_I2S_REMOTE_PREBUILT)
  # The directory of the precompiled binaries is set by the user, see the help
  # of DMIC_FFT_I2S_REMOTE_BIN_DIR in Kconfig.sysbuild for what it must hold
  set(remote_bin_dir ${SB_CONFIG_DMIC_FFT_I2S_REMOTE_BIN_DIR})
  if("${remote_bin_dir}" STREQUAL "")
    message(FATAL_ERROR
      "SB_CONFIG_DMIC_FFT_I2S_REMOTE_BIN_DIR is empty: set it to the directory of the "
      "precompiled HiFi4 binaries, or build the Zephyr HiFi4 image with "
      "-DSB_CONFIG_DMIC_FFT_I2S_REMOTE_ZEPHYR=y"
    )
  endif()

  # Sizes of the HiFi4 memory regions dsp.c loads the sections into
  set(max_reset.bin 1024)
  set(max_text.bin 1048576)
  set(max_data.bin 1048576)
  foreach(f reset.bin text.bin data.bin)
    if(NOT EXISTS ${remote_bin_dir}/aud_dsp_server.${f})
      message(FATAL_ERROR "${remote_bin_dir}/aud_dsp_server.${f} not found")
    endif()
    file(SIZE ${remote_bin_dir}/aud_dsp_server.${f} size)
    if(size GREATER ${max_${f}})
      message(FATAL_ERROR "aud_dsp_server.${f} is ${size} bytes, more than ${max_${f}}")
    endif()
  endforeach()

  message(STATUS "HiFi4 image: precompiled binaries from ${remote_bin_dir}")
  set(HIFI4_BIN_RESET ${remote_bin_dir}/aud_dsp_server.reset.bin CACHE INTERNAL "")
  set(HIFI4_BIN_TEXT ${remote_bin_dir}/aud_dsp_server.text.bin CACHE INTERNAL "")
  set(HIFI4_BIN_DATA ${remote_bin_dir}/aud_dsp_server.data.bin CACHE INTERNAL "")
else()
  # Drop the prebuilt binaries of an earlier configuration of this build
  foreach(sec RESET TEXT DATA)
    unset(HIFI4_BIN_${sec} CACHE)
  endforeach()

  # The HiFi4 board of each supported target comes from remote-dsp-board-select.cmake.
  # dsp-load.cmake looks for the image under the name "remote".
  include(${APP_DIR}/remote-dsp-board-select.cmake)

  ExternalZephyrProject_Add(
    APPLICATION remote
    SOURCE_DIR ${APP_DIR}/../aud_remote
    BOARD ${REMOTE_BOARD}
    BUILD_ONLY TRUE
  )

  add_dependencies(${DEFAULT_IMAGE} remote)
  sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} remote)

  # The functions the Cortex-M33 pipeline opens, in the format it captures. The
  # channel count is negotiated, from the caps filter of src/main.c.
  set_config_bool(remote CONFIG_AUD_REMOTE_FUNC_GAIN y)
  set_config_bool(remote CONFIG_AUD_REMOTE_FUNC_FFT y)
  set_config_bool(remote CONFIG_AUD_REMOTE_FUNC_IFFT y)
  set_config_int(remote CONFIG_AUD_REMOTE_SAMPLE_RATE ${SB_CONFIG_DMIC_FFT_I2S_SAMPLE_RATE})
  set_config_int(remote CONFIG_AUD_REMOTE_FRAME_INTERVAL_US
    ${SB_CONFIG_DMIC_FFT_I2S_FRAME_INTERVAL_US}
  )
  set_config_int(remote CONFIG_AUD_REMOTE_GAIN_PERCENT ${SB_CONFIG_DMIC_FFT_I2S_GAIN_PERCENT})
endif()
