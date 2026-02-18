/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2016 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MCUboot public API for MCUboot control of image boot process
 *
 * This header provides backward compatibility wrappers around the
 * generic DFU boot API. New code should use <zephyr/dfu/dfu_boot.h> directly.
 */

#ifndef ZEPHYR_INCLUDE_DFU_MCUBOOT_H_
#define ZEPHYR_INCLUDE_DFU_MCUBOOT_H_

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <zephyr/types.h>
#include <zephyr/dfu/dfu_boot.h>

/**
 * @brief MCUboot public API for MCUboot control of image boot process
 * @defgroup mcuboot_api MCUboot image control API
 * @ingroup third_party
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Swap type definitions - for backward compatibility */
#ifndef BOOT_SWAP_TYPE_NONE
#define BOOT_SWAP_TYPE_NONE     1
#endif
#ifndef BOOT_SWAP_TYPE_TEST
#define BOOT_SWAP_TYPE_TEST     2
#endif
#ifndef BOOT_SWAP_TYPE_PERM
#define BOOT_SWAP_TYPE_PERM     3
#endif
#ifndef BOOT_SWAP_TYPE_REVERT
#define BOOT_SWAP_TYPE_REVERT   4
#endif
#ifndef BOOT_SWAP_TYPE_FAIL
#define BOOT_SWAP_TYPE_FAIL     5
#endif

#define BOOT_IMG_VER_STRLEN_MAX 25

/** Sector at which firmware update should be placed in swap using offset mode */
#define SWAP_USING_OFFSET_SECTOR_UPDATE_BEGIN 1

/** Boot upgrade request modes */
#define BOOT_UPGRADE_TEST       0
#define BOOT_UPGRADE_PERMANENT  1

/**
 * @brief MCUboot image header representation for image version
 */
struct mcuboot_img_sem_ver {
	uint8_t major;
	uint8_t minor;
	uint16_t revision;
	uint32_t build_num;
};

/**
 * @brief Model for the MCUboot image header as of version 1
 */
struct mcuboot_img_header_v1 {
	uint32_t image_size;
	struct mcuboot_img_sem_ver sem_ver;
};

/**
 * @brief Model for the MCUBoot image header
 */
struct mcuboot_img_header {
	uint32_t mcuboot_version;
	union {
		struct mcuboot_img_header_v1 v1;
	} h;
};

/**
 * @brief Read the MCUboot image header information from an image bank.
 *
 * @param area_id flash_area ID of image bank which stores the image.
 * @param header On success, the returned header information is available
 *               in this structure.
 * @param header_size Size of the header structure passed by the caller.
 * @return Zero on success, a negative value on error.
 */
int boot_read_bank_header(uint8_t area_id,
			  struct mcuboot_img_header *header,
			  size_t header_size);

/**
 * @brief Get the flash area id for the active image slot.
 *
 * @return flash area id for the active image slot
 */
uint8_t boot_fetch_active_slot(void);
	/**
		* @brief Check if the currently running image is confirmed as OK.
		*
		* @return True if the image is confirmed as OK, false otherwise.
		*/
	bool boot_is_img_confirmed(void);

	/**
		* @brief Marks the currently running image as confirmed.
		*
		* @return 0 on success, negative errno code on fail.
		*/
	int boot_write_img_confirmed(void);

	/**
		* @brief Marks the image with the given index in the primary slot as confirmed.
		*
		* @param image_index Image pair index.
		* @return 0 on success, negative errno code on fail.
		*/
	int boot_write_img_confirmed_multi(int image_index);

	/**
		* @brief Determines the action, if any, that mcuboot will take on the next reboot.
		*
		* @return a BOOT_SWAP_TYPE_[...] constant on success, negative errno code on fail.
		*/
	int mcuboot_swap_type(void);

	/**
		* @brief Determines the action, if any, that mcuboot will take on the next reboot.
		*
		* @param image_index Image pair index.
		* @return a BOOT_SWAP_TYPE_[...] constant on success, negative errno code on fail.
		*/
	int mcuboot_swap_type_multi(int image_index);

	/**
		* @brief Marks the image in slot 1 as pending.
		*
		* @param permanent Whether the image should be used permanently or only tested once.
		* @return 0 on success, negative errno code on fail.
		*/
	int boot_request_upgrade(int permanent);

	/**
		* @brief Marks the image with the given index in the secondary slot as pending.
		*
		* @param image_index Image pair index.
		* @param permanent Whether the image should be used permanently or only tested once.
		* @return 0 on success, negative errno code on fail.
		*/
	int boot_request_upgrade_multi(int image_index, int permanent);

	/**
		* @brief Erase the image Bank.
		*
		* @param area_id flash_area ID of image bank to be erased.
		* @return 0 on success, negative errno code on fail.
		*/
	int boot_erase_img_bank(uint8_t area_id);

	/**
 * @brief Get the offset of the status in the image bank
 *
 * @param area_id flash_area ID of image bank to get the status offset
 * @return a positive offset on success, negative errno code on fail
 */
ssize_t boot_get_area_trailer_status_offset(uint8_t area_id);

/**
 * @brief Get the offset of the status from an image bank size
 *
 * @param area_size size of image bank
 * @return offset of the status
 */
ssize_t boot_get_trailer_status_offset(size_t area_size);

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET) || defined(__DOXYGEN__)
/**
 * @brief Get the offset of the image header
 *
 * @param area_id flash_area ID of image bank
 * @return offset of the image header
 */
size_t boot_get_image_start_offset(uint8_t area_id);
#else
#define boot_get_image_start_offset(...) 0
#endif

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DFU_MCUBOOT_H_ */
