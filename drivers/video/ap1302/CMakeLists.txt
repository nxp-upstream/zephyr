# SPDX-License-Identifier: Apache-2.0

# AP1302 driver
zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/../ap1302.c)

# Optional: embed an AP1302 firmware blob into the final image.
#
# Usage:
#   - Enable CONFIG_VIDEO_AP1302_BUILTIN_FIRMWARE=y
#   - Provide the blob path at build time:
#       west build ... -- -DAP1302_FIRMWARE_FILE=/abs/path/to/ap1302_fw.bin
#
# Notes:
# - The objcopy-generated _binary_* symbols are derived from the input filename.
#   We copy the firmware into the build directory with a stable basename to keep
#   symbols predictable:
#     _binary_ap1302_fw_bin_start/_end
if(CONFIG_VIDEO_AP1302_BUILTIN_FIRMWARE)
  set(AP1302_FIRMWARE_FILE "" CACHE FILEPATH "Path to the AP1302 firmware binary blob")

  if(NOT AP1302_FIRMWARE_FILE)
    message(FATAL_ERROR
      "CONFIG_VIDEO_AP1302_BUILTIN_FIRMWARE is enabled but AP1302_FIRMWARE_FILE is not set.\n"
      "Provide it at build time, e.g.:\n"
      "  west build ... -- -DAP1302_FIRMWARE_FILE=<path-to-ap1302-fw.bin>\n")
  endif()

  if(NOT EXISTS ${AP1302_FIRMWARE_FILE})
    message(FATAL_ERROR
      "AP1302_FIRMWARE_FILE does not exist: ${AP1302_FIRMWARE_FILE}")
  endif()

  # Keep the file name stable so the objcopy-generated symbols are predictable.
  set(AP1302_FW_BIN "${CMAKE_CURRENT_BINARY_DIR}/ap1302_fw.bin")
  set(AP1302_FW_OBJ "${CMAKE_CURRENT_BINARY_DIR}/ap1302_fw.o")

  add_custom_target(ap1302_fw_bin ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AP1302_FIRMWARE_FILE} ${AP1302_FW_BIN}
    BYPRODUCTS ${AP1302_FW_BIN}
    DEPENDS ${AP1302_FIRMWARE_FILE}
    VERBATIM
  )

  # NOTE: objcopy generates the _binary_* symbols from the *input filename*.
  # Use a stable basename by running in the build dir and passing a relative path.
  add_custom_target(ap1302_fw_obj ALL
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B arm ap1302_fw.bin ap1302_fw.o
    BYPRODUCTS ${AP1302_FW_OBJ}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ap1302_fw_bin
    VERBATIM
  )

  # Ensure the firmware object is created before linking the library.
  add_dependencies(drivers__video ap1302_fw_obj)

  zephyr_library_sources(${AP1302_FW_OBJ})
endif()
