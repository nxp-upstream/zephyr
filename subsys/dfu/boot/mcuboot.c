/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2016-2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MCUboot-specific legacy functions
 *
 * This file contains MCUboot-specific implementations that provide
 * backward compatibility with existing applications. These functions
 * work with flash area IDs rather than slot numbers.
 */

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <bootutil/bootutil_public.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/dfu/dfu_boot.h>

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) || \
	defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT)
#include <bootutil/boot_status.h>
#include <zephyr/retention/blinfo.h>
#endif

LOG_MODULE_REGISTER(mcuboot_dfu, CONFIG_IMG_MANAGER_LOG_LEVEL);

/*
 * MCUboot header constants
 */
#define BOOT_HEADER_MAGIC_V1 0x96f3b83d
#define BOOT_HEADER_SIZE_V1 32

/*
 * Image index enum for swap using offset mode
 */
enum image_index {
	IMAGE_INDEX_INVALID = -1,
	IMAGE_INDEX_0 = 0,
	IMAGE_INDEX_1 = 1,
	IMAGE_INDEX_2 = 2,
	IMAGE_INDEX_3 = 3,
	IMAGE_INDEX_4 = 4,
	IMAGE_INDEX_5 = 5,
	IMAGE_INDEX_6 = 6,
	IMAGE_INDEX_7 = 7,
};

/*
 * Slot partition definitions (needed for area_id to image mapping)
 */
#define SLOT0_PARTITION  slot0_partition
#define SLOT1_PARTITION  slot1_partition
#define SLOT2_PARTITION  slot2_partition
#define SLOT3_PARTITION  slot3_partition
#define SLOT4_PARTITION  slot4_partition
#define SLOT5_PARTITION  slot5_partition
#define SLOT6_PARTITION  slot6_partition
#define SLOT7_PARTITION  slot7_partition
#define SLOT8_PARTITION  slot8_partition
#define SLOT9_PARTITION  slot9_partition
#define SLOT10_PARTITION slot10_partition
#define SLOT11_PARTITION slot11_partition
#define SLOT12_PARTITION slot12_partition
#define SLOT13_PARTITION slot13_partition
#define SLOT14_PARTITION slot14_partition
#define SLOT15_PARTITION slot15_partition

/*
 * Raw (on-flash) representation of the v1 image header.
 */
struct mcuboot_v1_raw_header {
	uint32_t header_magic;
	uint32_t image_load_address;
	uint16_t header_size;
	uint16_t pad;
	uint32_t image_size;
	uint32_t image_flags;
	struct {
		uint8_t major;
		uint8_t minor;
		uint16_t revision;
		uint32_t build_num;
	} version;
	uint32_t pad2;
} __packed;

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) || \
	defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT)
#define INVALID_SLOT_ID 255

uint8_t boot_fetch_active_slot(void)
{
	int rc;
	uint8_t slot;

	rc = blinfo_lookup(BLINFO_RUNNING_SLOT, &slot, sizeof(slot));
	if (rc <= 0) {
		LOG_ERR("Failed to fetch active slot: %d", rc);
		return INVALID_SLOT_ID;
	}

	/* Convert slot number to flash area ID */
	int area_id = dfu_boot_get_flash_area_id(slot);
	if (area_id < 0) {
		return INVALID_SLOT_ID;
	}

	return (uint8_t)area_id;
}
#else
uint8_t boot_fetch_active_slot(void)
{
	int slot = dfu_boot_get_active_slot(dfu_boot_get_active_image());
	int area_id = dfu_boot_get_flash_area_id(slot);

	return (area_id >= 0) ? (uint8_t)area_id : 0;
}
#endif

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET)
/**
 * Get image index from flash area ID for swap-using-offset secondary slots.
 * This is the legacy version that works with area_id instead of slot number.
 * Returns IMAGE_INDEX_INVALID if area_id is not a secondary slot.
 */
static int get_image_index_from_area_id(uint8_t area_id)
{
	if (area_id == FIXED_PARTITION_ID(SLOT1_PARTITION)) {
		return IMAGE_INDEX_0;
	}
#if FIXED_PARTITION_EXISTS(SLOT3_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT3_PARTITION)) {
		return IMAGE_INDEX_1;
	}
#endif
#if FIXED_PARTITION_EXISTS(SLOT5_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT5_PARTITION)) {
		return IMAGE_INDEX_2;
	}
#endif
#if FIXED_PARTITION_EXISTS(SLOT7_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT7_PARTITION)) {
		return IMAGE_INDEX_3;
	}
#endif
#if FIXED_PARTITION_EXISTS(SLOT9_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT9_PARTITION)) {
		return IMAGE_INDEX_4;
	}
#endif
#if FIXED_PARTITION_EXISTS(SLOT11_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT11_PARTITION)) {
		return IMAGE_INDEX_5;
	}
#endif
#if FIXED_PARTITION_EXISTS(SLOT13_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT13_PARTITION)) {
		return IMAGE_INDEX_6;
	}
#endif
#if FIXED_PARTITION_EXISTS(SLOT15_PARTITION)
	if (area_id == FIXED_PARTITION_ID(SLOT15_PARTITION)) {
		return IMAGE_INDEX_7;
	}
#endif

	return IMAGE_INDEX_INVALID;
}

size_t boot_get_image_start_offset(uint8_t area_id)
{
	size_t off = 0;
	int image = get_image_index_from_area_id(area_id);

	if (image == IMAGE_INDEX_INVALID) {
		/* Not a secondary slot, no offset needed */
		return 0;
	}

	const struct flash_area *fa;
	uint32_t num_sectors = SWAP_USING_OFFSET_SECTOR_UPDATE_BEGIN;
	struct flash_sector sector_data;
	int rc;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		LOG_ERR("Flash open area %u failed: %d", area_id, rc);
		return 0;
	}

	if (mcuboot_swap_type_multi(image) != BOOT_SWAP_TYPE_REVERT) {
		/* For swap using offset mode, the image starts in the second sector of
		 * the upgrade slot, so apply the offset when this is needed
		 */
		rc = flash_area_get_sectors(area_id, &num_sectors, &sector_data);
		if ((rc != 0 && rc != -ENOMEM) ||
		    num_sectors != SWAP_USING_OFFSET_SECTOR_UPDATE_BEGIN) {
			LOG_ERR("Failed to get sector details: %d", rc);
		} else {
			off = sector_data.fs_size;
		}
	}

	flash_area_close(fa);

	LOG_DBG("Start offset for area %u: 0x%zx", area_id, off);
	return off;
}
#endif /* CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET */

static int boot_read_v1_header(uint8_t area_id,
			       struct mcuboot_v1_raw_header *v1_raw)
{
	const struct flash_area *fa;
	int rc;
	size_t off = boot_get_image_start_offset(area_id);

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		return rc;
	}

	rc = flash_area_read(fa, off, v1_raw, sizeof(*v1_raw));
	flash_area_close(fa);
	if (rc) {
		return rc;
	}

	v1_raw->header_magic = sys_le32_to_cpu(v1_raw->header_magic);
	v1_raw->header_size = sys_le16_to_cpu(v1_raw->header_size);

	if ((v1_raw->header_magic != BOOT_HEADER_MAGIC_V1) ||
	    (v1_raw->header_size < BOOT_HEADER_SIZE_V1)) {
		return -EIO;
	}

	v1_raw->image_load_address = sys_le32_to_cpu(v1_raw->image_load_address);
	v1_raw->image_size = sys_le32_to_cpu(v1_raw->image_size);
	v1_raw->image_flags = sys_le32_to_cpu(v1_raw->image_flags);
	v1_raw->version.revision = sys_le16_to_cpu(v1_raw->version.revision);
	v1_raw->version.build_num = sys_le32_to_cpu(v1_raw->version.build_num);

	return 0;
}

int boot_read_bank_header(uint8_t area_id,
			  struct mcuboot_img_header *header,
			  size_t header_size)
{
	int rc;
	struct mcuboot_v1_raw_header v1_raw;
	struct mcuboot_img_sem_ver *sem_ver;
	size_t v1_min_size = sizeof(uint32_t) + sizeof(struct mcuboot_img_header_v1);

	if (header_size < v1_min_size) {
		return -ENOMEM;
	}

	rc = boot_read_v1_header(area_id, &v1_raw);
	if (rc) {
		return rc;
	}

	header->mcuboot_version = 1U;
	header->h.v1.image_size = v1_raw.image_size;
	sem_ver = &header->h.v1.sem_ver;
	sem_ver->major = v1_raw.version.major;
	sem_ver->minor = v1_raw.version.minor;
	sem_ver->revision = v1_raw.version.revision;
	sem_ver->build_num = v1_raw.version.build_num;

	return 0;
}

int mcuboot_swap_type_multi(int image_index)
{
	return boot_swap_type_multi(image_index);
}

int mcuboot_swap_type(void)
{
#ifdef FLASH_AREA_IMAGE_SECONDARY
	return boot_swap_type();
#else
	return BOOT_SWAP_TYPE_NONE;
#endif
}

int boot_write_img_confirmed_multi(int image_index)
{
	int rc;

	rc = boot_set_confirmed_multi(image_index);
	if (rc) {
		return -EIO;
	}

	return 0;
}

int boot_erase_img_bank(uint8_t area_id)
{
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		return rc;
	}

	rc = flash_area_flatten(fa, 0, fa->fa_size);
	flash_area_close(fa);

	return rc;
}

ssize_t boot_get_trailer_status_offset(size_t area_size)
{
	return (ssize_t)area_size - BOOT_MAGIC_SZ - BOOT_MAX_ALIGN * 2;
}

ssize_t boot_get_area_trailer_status_offset(uint8_t area_id)
{
	const struct flash_area *fa;
	ssize_t offset;
	int rc;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		return rc;
	}

	offset = boot_get_trailer_status_offset(fa->fa_size);
	flash_area_close(fa);

	if (offset < 0) {
		return -EFAULT;
	}

	return offset;
}

bool boot_is_img_confirmed(void)
{
	return dfu_boot_is_confirmed();
}

int boot_write_img_confirmed(void)
{
	return dfu_boot_confirm();
}

int boot_request_upgrade(int permanent)
{
#ifdef FLASH_AREA_IMAGE_SECONDARY
	return dfu_boot_set_pending(1, permanent == BOOT_UPGRADE_PERMANENT);
#else
	return 0;
#endif
}

int boot_request_upgrade_multi(int image_index, int permanent)
{
	int slot = (image_index * 2) + 1; /* Secondary slot for image */
	return dfu_boot_set_pending(slot, permanent == BOOT_UPGRADE_PERMANENT);
}
