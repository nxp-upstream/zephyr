/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DFU_DFU_BOOT_H_
#define ZEPHYR_INCLUDE_DFU_DFU_BOOT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DFU Boot Abstraction API
 * @defgroup dfu_boot DFU Boot Abstraction
 * @ingroup dfu
 * @{
 */

/** @} */

/**
 * @name Image state flags
 * @{
 */
/** Image is set for next swap */
#define DFU_BOOT_STATE_F_PENDING	0x01
/** Image has been confirmed */
#define DFU_BOOT_STATE_F_CONFIRMED	0x02
/** Image is currently active */
#define DFU_BOOT_STATE_F_ACTIVE		0x04
/** Image is to stay in primary slot after the next boot */
#define DFU_BOOT_STATE_F_PERMANENT	0x08
/** @} */

/**
 * @name Swap types
 * @{
 */
#define DFU_BOOT_SWAP_TYPE_NONE		0 /**< No swap */
#define DFU_BOOT_SWAP_TYPE_TEST		1 /**< Test swap */
#define DFU_BOOT_SWAP_TYPE_PERM		2 /**< Permanent swap */
#define DFU_BOOT_SWAP_TYPE_REVERT	3 /**< Revert swap */
#define DFU_BOOT_SWAP_TYPE_UNKNOWN	255 /**< Unknown swap */
/** @} */

/**
 * @name Image flags
 * @{
 */
/** Image is non-bootable */
#define DFU_BOOT_IMG_F_NON_BOOTABLE	0x00000010
/** Image has fixed ROM address */
#define DFU_BOOT_IMG_F_ROM_FIXED	0x00000100
/** @} */

/**
 * @name Next boot type
 * @{
 */
enum dfu_boot_next_type {
	/** Normal boot to active or non-active slot */
	DFU_BOOT_NEXT_TYPE_NORMAL = 0,
	/** Test/non-permanent boot to non-active slot */
	DFU_BOOT_NEXT_TYPE_TEST = 1,
	/** Revert to already confirmed slot */
	DFU_BOOT_NEXT_TYPE_REVERT = 2,
};
/** @} */

/**
 * @brief Image version structure
 */
struct dfu_boot_img_version {
	uint8_t major;
	uint8_t minor;
	uint16_t revision;
	uint32_t build_num;
};

/**
 * @brief Image information structure
 *
 * This structure is used to pass image information from header validation
 * and image info reading.
 */
struct dfu_boot_img_info {
	/** Image version */
	struct dfu_boot_img_version version;
	/** Image flags */
	uint32_t flags;
	/** Image load address */
	uint32_t load_addr;
	/** Image size */
	uint32_t img_size;
	/** Header size */
	uint16_t hdr_size;
	/** Hash length */
	uint8_t hash_len;
	/** Hash value */
	uint8_t hash[CONFIG_IMG_HASH_LEN];
	/** Whether the image info is valid */
	bool valid;
};

/**
 * @brief Get the state flags for a slot
 *
 * @param slot Slot number
 *
 * @return State flags (combination of DFU_BOOT_STATE_F_* values)
 */
uint8_t dfu_boot_get_slot_state(int slot);

/**
 * @brief Get the next boot slot for an image
 *
 * @param image Image number
 * @param type Pointer to store the next boot type (can be NULL)
 *
 * @return Slot number that will boot next, or -1 on error
 */
int dfu_boot_get_next_boot_slot(int image, enum dfu_boot_next_type *type);

/**
 * @brief Check if any slot is pending
 *
 * @return true if any slot is pending, false otherwise
 */
bool dfu_boot_any_pending(void);

/**
 * @brief Check if a slot is in use
 *
 * @param slot Slot number
 *
 * @return Non-zero if slot is in use, 0 otherwise
 */
int dfu_boot_slot_in_use(int slot);

/**
 * @brief Set a slot as pending for next boot
 *
 * @param slot Slot number
 * @param permanent If true, make the change permanent
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_set_pending(int slot, bool permanent);

/**
 * @brief Confirm the current image
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_confirm(void);

/**
 * @brief Check if the current image is confirmed
 *
 * @return true if confirmed, false otherwise
 */
bool dfu_boot_is_confirmed(void);

/**
 * @brief Erase a slot
 *
 * @param slot Slot number
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_erase_slot(int slot);

/**
 * @brief Read data from a slot
 *
 * @param slot Slot number
 * @param offset Offset within the slot
 * @param dst Destination buffer
 * @param num_bytes Number of bytes to read
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_read(int slot, size_t offset, void *dst, size_t num_bytes);

/**
 * @brief Get the flash area ID for a slot
 *
 * @param slot Slot number
 *
 * @return Flash area ID, or negative error code on failure
 */
int dfu_boot_get_flash_area_id(int slot);

/**
 * @brief Get the swap type for a slot
 *
 * @param slot Slot number
 *
 * @return Swap type (DFU_BOOT_SWAP_TYPE_* value)
 */
int dfu_boot_get_swap_type(int slot);

/**
 * @brief Compare two image versions
 *
 * @param a First version
 * @param b Second version
 *
 * @return -1 if a < b, 0 if a == b, 1 if a > b
 */
int dfu_boot_vercmp(const struct dfu_boot_img_version *a,
			const struct dfu_boot_img_version *b);

/**
 * @brief Get the erased value for a slot's flash
 *
 * @param slot Slot number
 * @param erased_val Pointer to store the erased value
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_get_erased_val(int slot, uint8_t *erased_val);

/**
 * @brief Get the image offset for a slot (for swap-using-offset mode)
 *
 * @param slot Slot number
 *
 * @return Image offset, or 0 if not applicable
 */
size_t dfu_boot_get_image_offset(int slot);

/**
 * @brief Get the trailer status offset for a given flash area size
 *
 * @param area_size Size of the flash area
 *
 * @return Offset of the trailer status area
 */
size_t dfu_boot_get_trailer_status_offset(size_t area_size);

/**
 * @brief Validate an image header
 *
 * @param data Pointer to the image data (must contain at least the header)
 * @param len Length of the data
 * @param info Pointer to store extracted image information (can be NULL)
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_validate_header(const void *data, size_t len, struct dfu_boot_img_info *info);

/**
 * @brief Format a version string
 *
 * @param ver Pointer to version structure
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 *
 * @return Number of characters written (excluding null terminator),
 *         or negative error code on failure
 */
int dfu_boot_ver_str(const struct dfu_boot_img_version *ver, char *dst, size_t dst_size);

/**
 * @brief Read image information from a slot
 *
 * @param slot Slot number
 * @param info Pointer to store image information
 *
 * @return 0 on success, negative error code on failure
 */
int dfu_boot_read_img_info(int slot, struct dfu_boot_img_info *info);

/**
 * @brief Get the active slot for an image
 *
 * @param image Image number
 *
 * @return Active slot number
 */
int dfu_boot_get_active_slot(int image);

/**
 * @brief Get the active image number
 *
 * @return Active image number
 */
int dfu_boot_get_active_image(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DFU_DFU_BOOT_H_ */