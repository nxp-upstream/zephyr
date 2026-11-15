# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nn_detect_dt)

# The pipeline is described in the devicetree: the application brings the
# model and what it draws from the results
target_sources(app PRIVATE src/main.c)

if(CONFIG_MPIPE_NN_BACKEND_TFLM)
  # A detection model is not shipped: supply a quantized detector, such as
  # the 128x128 Ultraface face detector of the NXP eIQ Model Zoo.
  if(NOT DEFINED NN_MODEL)
    message(FATAL_ERROR
      "nn_detect_dt with the TFLM backend needs a model: build with -DNN_MODEL=<path to .tflite>")
  endif()

  generate_inc_file_for_target(app ${NN_MODEL}
    ${ZEPHYR_BINARY_DIR}/include/generated/nn_model.inc)
endif()
