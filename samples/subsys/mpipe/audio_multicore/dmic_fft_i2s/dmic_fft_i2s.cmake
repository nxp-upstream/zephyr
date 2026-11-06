# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

# Build in the audio format and the gain that sysbuild.cmake sets from
# SB_CONFIG_DMIC_FFT_I2S_*, as DMIC_FFT_I2S_<name> in the sources of this image
foreach(opt SAMPLE_RATE FRAME_INTERVAL_US GAIN_PERCENT)
  zephyr_get(DMIC_FFT_I2S_${opt} SYSBUILD GLOBAL)
  if(NOT DEFINED DMIC_FFT_I2S_${opt})
    message(FATAL_ERROR "DMIC_FFT_I2S_${opt} is not set: build dmic_fft_i2s with --sysbuild")
  endif()
  target_compile_definitions(app PRIVATE DMIC_FFT_I2S_${opt}=${DMIC_FFT_I2S_${opt}})
endforeach()
