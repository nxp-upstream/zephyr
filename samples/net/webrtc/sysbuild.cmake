# Copyright 2026 NXP
# SPDX-License-Identifier: Apache-2.0

# A sysbuild of the sample adds the image of the second core, which decodes
# the video of the peer for the first one (overlay-mimxrt1170-multicore.conf).
if("${SB_CONFIG_REMOTE_BOARD}" STREQUAL "")
  message(FATAL_ERROR
    "Target ${BOARD} has no second core for this sample: "
    "build it without sysbuild")
endif()

set(REMOTE_APP remote)

ExternalZephyrProject_Add(
  APPLICATION ${REMOTE_APP}
  SOURCE_DIR  ${APP_DIR}/${REMOTE_APP}
  BOARD       ${SB_CONFIG_REMOTE_BOARD}
)

add_dependencies(${DEFAULT_IMAGE} ${REMOTE_APP})
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} ${REMOTE_APP})
