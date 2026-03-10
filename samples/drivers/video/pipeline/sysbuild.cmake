#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#

set(REMOTE_APP remote)

ExternalZephyrProject_Add(
  APPLICATION ${REMOTE_APP}
  SOURCE_DIR  ${APP_DIR}/${REMOTE_APP}
  BOARD       ${SB_CONFIG_REMOTE_BOARD}
)

# Add dependencies so that the remote code will be built first
# This is required because some primary cores need information from the
# remote core's build, such as the output image's LMA
add_dependencies(${DEFAULT_IMAGE} ${REMOTE_APP})
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} ${REMOTE_APP})

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  # Make sure MCUboot is flashed first
  sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} mcuboot)
endif()

native_simulator_set_child_images(${DEFAULT_IMAGE} remote)
native_simulator_set_final_executable(${DEFAULT_IMAGE})
