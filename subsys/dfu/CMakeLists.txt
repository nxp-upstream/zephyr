# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IMG_MANAGER)

zephyr_library()

# Common DFU image utilities (bootloader-agnostic)
zephyr_library_sources(
    img_util/flash_img.c
)

# Backend-specific sources
if(CONFIG_MCUBOOT_IMG_MANAGER)
    zephyr_library_sources(
        boot/mcuboot_backend.c
        boot/mcuboot.c
    )
    zephyr_library_sources_ifdef(CONFIG_MCUBOOT_SHELL boot/mcuboot_shell.c)
    zephyr_library_link_libraries(MCUBOOT_BOOTUTIL)
endif()

if(CONFIG_NXPBOOT_IMG_MANAGER)
    zephyr_library_sources(
        boot/nxp_boot.c
    )
endif()

endif() # CONFIG_IMG_MANAGER
