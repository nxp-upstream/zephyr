# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

# Test MJPEG stream of the samples whose pipeline is described in the
# devicetree, generated at build time with the GStreamer CLI. The frame size
# is read from the caps filter node pinning it, so the devicetree stays the
# only place stating it.

set(MJPEG_NUM_FRAMES 150 CACHE STRING "Number of frames in the generated test MJPEG stream")

# Generate the stream as mjpeg.inc, for an application embedding it.
function(mpipe_sample_mjpeg_generate caps_filter_label)
  find_program(GST_LAUNCH gst-launch-1.0 REQUIRED)

  dt_nodelabel(caps_filter NODELABEL ${caps_filter_label} REQUIRED)
  dt_prop(width PATH ${caps_filter} PROPERTY width REQUIRED)
  dt_prop(height PATH ${caps_filter} PROPERTY height REQUIRED)

  set(mjpeg_file ${ZEPHYR_BINARY_DIR}/include/generated/test.mjpeg)

  add_custom_command(
    OUTPUT ${mjpeg_file}
    COMMAND ${GST_LAUNCH}
      videotestsrc num-buffers=${MJPEG_NUM_FRAMES}
      ! videoconvert
      ! "video/x-raw,format=I420,width=${width},height=${height}"
      ! timeoverlay font-desc="Sans 32"
      ! jpegenc
      ! filesink location=${mjpeg_file}
    COMMENT "Generating test.mjpeg (${width}x${height}, ${MJPEG_NUM_FRAMES} frames) using GStreamer"
  )

  add_custom_target(gen_test_mjpeg DEPENDS ${mjpeg_file})

  generate_inc_file_for_target(app
    ${mjpeg_file}
    ${ZEPHYR_BINARY_DIR}/include/generated/mjpeg.inc
  )

  add_dependencies(app gen_test_mjpeg)
endfunction()

# Generate the stream and write it at boot to the path of the file source
# node, before the pipelines of the devicetree are built and played.
function(mpipe_sample_mjpeg_file file_src_label caps_filter_label)
  mpipe_sample_mjpeg_generate(${caps_filter_label})

  target_sources(app PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/mjpeg_file.c)
  target_compile_definitions(app PRIVATE MJPEG_FILE_SRC=${file_src_label})
endfunction()
