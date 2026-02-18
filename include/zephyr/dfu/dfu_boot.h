/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief DFU Boot abstraction API
 *
 * Generic bootloader abstraction layer for DFU operations.
 * This API provides bootloader-agnostic functions for image management,
 * slot control, and boot state operations.
 */

#ifndef ZEPHYR_INCLUDE_DFU_DFU_BOOT_H_
#define ZEPHYR_INCLUDE_DFU_DFU_BOOT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DFU Boot abstraction API
 * @defgroup dfu_boot_api DFU Boot API
 * @ingroup dfu
 * @{
 */

/** Maximum version string length: "255.255.65535.4294967295\0" */
#define DFU_BOOT_VER_MAX_STR_LEN 25

/** Maximum hash length (SHA-512) */
#define DFU_BOOT_HASH_MAX_LEN 64

/** SHA-256 hash length */
#define DFU_BOOT_HASH_LEN_SHA256 32

/** SHA-512 hash length */
#define DFU_BOOT_HASH_LEN_SHA512 64

/**
 * @name Slot state flags
 * @{
 */
/** Slot is marked for next boot (test or permanent) */
#define DFU_BOOT_STATE_F_PENDING    BIT(0)
/** Slot image is confirmed */
#define DFU_BOOT_STATE_F_CONFIRMED  BIT(1)
/** Slot is currently active (running) */
#define DFU_BOOT_STATE_F_ACTIVE     BIT(2)
/** Slot is marked for permanent swap */
#define DFU_BOOT_STATE_F_PERMANENT  BIT(3)
/** @} */

/**
 * @name Swap types
 * @{
 */
/** No swap scheduled */
#define DFU_BOOT_SWAP_TYPE_NONE     0
/** Test swap (revert on next boot unless confirmed) */
#define DFU_BOOT_SWAP_TYPE_TEST     1
/** Permanent swap */
#define DFU_BOOT_SWAP_TYPE_PERM     2
/** Revert to previous image */
#define DFU_BOOT_SWAP_TYPE_REVERT   3
/** Unknown/error state */
#define DFU_BOOT_SWAP_TYPE_UNKNOWN  255
/** @} */

/**
 * @name Image flags
 * @{
 */
/** Image is not bootable */
#define DFU_BOOT_IMG_F_NON_BOOTABLE BIT(0)
/** Image has fixed ROM address */
#define DFU_BOOT_IMG_F_ROM_FIXED    BIT(1)
/** @} */

/**
 * @brief Next boot type
 */
enum dfu_boot_next_type {
	/** Normal boot to active or scheduled slot */
	DFU_BOOT_NEXT_TYPE_NORMAL = 0,
	/** Test boot (will revert unless confirmed) */
	DFU_BOOT_NEXT_TYPE_TEST = 1,
	/** Revert to previous confirmed slot */
	DFU_BOOT_NEXT_TYPE_REVERT = 2,
};

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
 * Contains all relevant information about an image in a slot.
 */
struct dfu_boot_img_info {
	/** Image version */
	struct dfu_boot_img_version version;
	/** Image hash (SHA-256 or SHA-512) */
	uint8_t hash[DFU_BOOT_HASH_MAX_LEN];
	/** Length of hash in bytes */
	uint8_t hash_len;
	/** Image flags (DFU_BOOT_IMG_F_*) */
	uint32_t flags;
	/** True if a valid image is present in the slot */
	bool valid;
};

/**
 * @brief Get the active (currently running) slot for an image
 *
 * @param image Image index (0 for single-image systems)
 * @return Slot number (0, 1, ...) or negative errno on error
 */
int dfu_boot_get_active_slot(int image);

/**
 * @brief Get the currently active image index
 *
 * For single-image systems, this always returns 0.
 *
 * @return Image index (>= 0)
 */
int dfu_boot_get_active_image(void);

/**
 * @brief Read image information from a slot
 *
 * Reads and parses the image header and extracts version, hash, and flags.
 *
 * @param slot Slot number
 * @param info Pointer to structure to fill with image info
 * @return 0 on success, negative errno on error
 */
int dfu_boot_read_img_info(int slot, struct dfu_boot_img_info *info);

/**
 * @brief Get state flags for a slot
 *
 * Returns the current state of a slot as a combination of DFU_BOOT_STATE_F_* flags.
 *
 * @param slot Slot number
 * @return Combination of DFU_BOOT_STATE_F_* flags
 */
uint8_t dfu_boot_get_slot_state(int slot);

/**
 * @brief Get the swap type for a slot's image pair
 *
 * @param slot Slot number
 * @return DFU_BOOT_SWAP_TYPE_* value
 */
int dfu_boot_get_swap_type(int slot);

/**
 * @brief Mark a slot as pending for next boot
 *
 * @param slot Slot number to mark as pending
 * @param permanent If true, make the change permanent; if false, test only
 * @return 0 on success, negative errno on error
 */
int dfu_boot_set_pending(int slot, bool permanent);

/**
 * @brief Confirm the currently running image
 *
 * Prevents automatic revert on next boot if the current image was
 * booted as a test.
 *
 * @return 0 on success, negative errno on error
 */
int dfu_boot_confirm(void);

/**
 * @brief Check if the currently running image is confirmed
 *
 * @return true if confirmed, false otherwise
 */
bool dfu_boot_is_confirmed(void);

/**
 * @brief Erase a slot
 *
 * @param slot Slot number to erase
 * @return 0 on success, negative errno on error
 */
int dfu_boot_erase_slot(int slot);

/**
 * @brief Get flash area ID for a slot
 *
 * @param slot Slot number
 * @return Flash area ID (>= 0) or negative errno on error
 */
int dfu_boot_get_flash_area_id(int slot);

/**
 * @brief Check if a slot is in use
 *
 * A slot is considered in use if it's active, pending, or otherwise
 * cannot be safely erased.
 *
 * @param slot Slot number
 * @return 1 if in use, 0 if free, negative errno on error
 */
int dfu_boot_slot_in_use(int slot);

/**
 * @brief Get the slot that will boot next for an image
 *
 * @param image Image index
 * @param type Pointer to store the type of next boot (may be NULL)
 * @return Slot number or negative errno on error
 */
int dfu_boot_get_next_boot_slot(int image, enum dfu_boot_next_type *type);

/**
 * @brief Check if any slot has a pending operation
 *
 * @return true if any slot is pending, false otherwise
 */
bool dfu_boot_any_pending(void);

/**
 * @brief Get the erased byte value for a slot's flash
 *
 * @param slot Slot number
 * @param erased_val Pointer to store the erased byte value
 * @return 0 on success, negative errno on error
 */
int dfu_boot_get_erased_val(int slot, uint8_t *erased_val);

/**
 * @brief Get image data offset within a slot
 *
 * Some bootloaders store image data at an offset within the slot.
 * This function returns that offset.
 *
 * @param slot Slot number
 * @return Offset in bytes (0 for most bootloaders)
 */
size_t dfu_boot_get_image_offset(int slot);

/**
 * @brief Read raw data from a slot
 *
 * @param slot Slot number
 * @param offset Offset within slot (after image offset)
 * @param dst Destination buffer
 * @param len Number of bytes to read
 * @return 0 on success, negative errno on error
 */
int dfu_boot_read(int slot, size_t offset, void *dst, size_t len);

/**
 * @brief Compare two image versions
 *
 * Compares versions in semantic versioning order:
 * major.minor.revision.build_num
 *
 * @param a First version
 * @param b Second version
 * @return -1 if a < b, 0 if a == b, 1 if a > b
 */
int dfu_boot_vercmp(const struct dfu_boot_img_version *a,
		    const struct dfu_boot_img_version *b);

/**
 * @brief Format version as string
 *
 * Formats version as "major.minor.revision[.build_num]".
 * Build number is only included if non-zero.
 *
 * @param ver Version structure
 * @param buf Output buffer
 * @param buf_size Size of output buffer
 * @return Number of characters written (excluding null terminator),
 *         or negative errno on error
 */
int dfu_boot_ver_str(const struct dfu_boot_img_version *ver,
		     char *buf, size_t buf_size);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DFU_DFU_BOOT_H_ */