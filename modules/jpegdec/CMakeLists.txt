# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_JPEGDEC)
  # The decoder's public header is included by Zephyr headers, so its include
  # path and the non-Arduino switch apply tree-wide.
  zephyr_include_directories(${ZEPHYR_JPEGDEC_MODULE_DIR}/src)
  zephyr_compile_definitions(JPEGDEC_NO_ARDUINO)

  zephyr_library()
  zephyr_library_sources(
    ${ZEPHYR_JPEGDEC_MODULE_DIR}/src/jpegdec.c
  )
  if(CONFIG_CPU_CORTEX_M4 OR CONFIG_CPU_CORTEX_M7)
    # The Cortex-M DSP paths: unaligned loads in the bit reader, and the
    # SIMD output stages of the IDCT and of the color conversion. The
    # decoder keeps them off by default; the intrinsics come from its own
    # header, which the Arduino build includes for the same cores.
    if(CONFIG_CPU_CORTEX_M4)
      zephyr_library_compile_definitions(ARM_MATH_CM4)
    else()
      zephyr_library_compile_definitions(ARM_MATH_CM7)
    endif()
    zephyr_library_compile_definitions(HAS_SIMD)
    zephyr_library_compile_options(-include ${CMAKE_CURRENT_LIST_DIR}/jpegdec_cm_simd.h)
  else()
    # Portable C core only: the host SIMD paths need extra compiler flags.
    zephyr_library_compile_definitions(NO_SIMD)
  endif()
  # The decoder aligns its buffers through an int64_t cast of a pointer,
  # which warns on 32-bit targets.
  zephyr_library_compile_options(-Wno-pointer-to-int-cast)
endif()
