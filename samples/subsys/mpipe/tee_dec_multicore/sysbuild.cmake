# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

if("${SB_CONFIG_REMOTE_BOARD}" STREQUAL "")
  message(FATAL_ERROR
    "Target ${BOARD} not supported for this sample. "
    "There is no remote board selected in Kconfig.sysbuild")
endif()

set(REMOTE_APP remote)

ExternalZephyrProject_Add(
  APPLICATION ${REMOTE_APP}
  SOURCE_DIR  ${APP_DIR}/${REMOTE_APP}
  BOARD       ${SB_CONFIG_REMOTE_BOARD}
)

# The remote image is built first: the primary core needs its output image's
# load address.
add_dependencies(${DEFAULT_IMAGE} ${REMOTE_APP})
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} ${REMOTE_APP})
