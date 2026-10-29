# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(cam_face_detection_tflm)

target_sources(app PRIVATE
  src/detect.c
  src/main.c
  src/preprocess.c
  src/inference.cpp
)

# The model, converted for the NPU and stored as a C array, so the build does
# not run the Neutron converter. model/ultraface_slim_npu.h records how it
# was produced. Another header defining g_model_tflite can be given; it must
# come from the converter version of the hal_nxp Neutron driver blobs.
set(MODEL_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/model/ultraface_slim_npu.h
  CACHE FILEPATH "Neutron-compiled model, as a header defining g_model_tflite")
if(NOT EXISTS "${MODEL_HEADER}")
  message(FATAL_ERROR "Model header '${MODEL_HEADER}' not found.")
endif()
set(model_dir ${CMAKE_BINARY_DIR}/model)
configure_file(${MODEL_HEADER} ${model_dir}/model_tflite.h COPYONLY)
target_include_directories(app PRIVATE ${model_dir})
