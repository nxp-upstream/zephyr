/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dfu/dfu_boot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>
#include <stdio.h>

#include <bootutil/bootutil_public.h>
#include <bootutil/image.h>

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) || \
	defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT)
#include <bootutil/boot_status.h>
#include <zephyr/retention/blinfo.h>
#endif

LOG_MODULE_REGISTER(dfu_boot_mcuboot, CONFIG_IMG_MANAGER_LOG_LEVEL);

/*
 * Slot partition definitions
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
 * Hash configuration
 */
#ifdef CONFIG_MCUBOOT_BOOTLOADER_USES_SHA512
#define MCUBOOT_HASH_TLV   IMAGE_TLV_SHA512
#define MCUBOOT_HASH_LEN   CONFIG_IMG_HASH_LEN
#else
#define MCUBOOT_HASH_TLV   IMAGE_TLV_SHA256
#define MCUBOOT_HASH_LEN   CONFIG_IMG_HASH_LEN
#endif

/*
 * Active slot detection
 */
#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) || \
	defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT)
#define ACTIVE_SLOT_FLASH_AREA_ID boot_fetch_active_slot()
#else
#define ACTIVE_SLOT_FLASH_AREA_ID DT_FIXED_PARTITION_ID(DT_CHOSEN(zephyr_code_partition))
#endif

/*
 * Slots per image configuration
 */
#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER)
#define SLOTS_PER_IMAGE 1
#else
#define SLOTS_PER_IMAGE 2
#endif

/*
 * Active image detection for multi-image
 */
#if CONFIG_MCUMGR_GRP_IMG_UPDATABLE_IMAGE_NUMBER >= 2

#define FIXED_PARTITION_IS_RUNNING_APP_PARTITION(label)                        \
	(FIXED_PARTITION_EXISTS(label) &&                                      \
	 DT_SAME_NODE(FIXED_PARTITION_NODE_MTD(DT_CHOSEN(zephyr_code_partition)), \
			  FIXED_PARTITION_MTD(label)) &&                           \
	 (FIXED_PARTITION_ADDRESS(label) <=                                    \
	  (CONFIG_FLASH_BASE_ADDRESS + CONFIG_FLASH_LOAD_OFFSET)) &&           \
	 (FIXED_PARTITION_ADDRESS(label) + FIXED_PARTITION_SIZE(label) >       \
	  (CONFIG_FLASH_BASE_ADDRESS + CONFIG_FLASH_LOAD_OFFSET)))

#if FIXED_PARTITION_EXISTS(slot0_ns_partition) && \
	FIXED_PARTITION_IS_RUNNING_APP_PARTITION(slot0_ns_partition)
#define ACTIVE_IMAGE_INDEX 0
#elif FIXED_PARTITION_EXISTS(slot0_partition) && \
	FIXED_PARTITION_IS_RUNNING_APP_PARTITION(slot0_partition)
#define ACTIVE_IMAGE_INDEX 0
#elif FIXED_PARTITION_EXISTS(slot1_partition) && \
	FIXED_PARTITION_IS_RUNNING_APP_PARTITION(slot1_partition)
#define ACTIVE_IMAGE_INDEX 0
#elif FIXED_PARTITION_EXISTS(slot2_partition) && \
	FIXED_PARTITION_IS_RUNNING_APP_PARTITION(slot2_partition)
#define ACTIVE_IMAGE_INDEX 1
#elif FIXED_PARTITION_EXISTS(slot3_partition) && \
	FIXED_PARTITION_IS_RUNNING_APP_PARTITION(slot3_partition)
#define ACTIVE_IMAGE_INDEX 1
#else
#define ACTIVE_IMAGE_INDEX 0
#endif

#else /* Single image */
#define ACTIVE_IMAGE_INDEX 0
#endif /* CONFIG_MCUMGR_GRP_IMG_UPDATABLE_IMAGE_NUMBER >= 2 */

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
 * Helper: convert slot number to image index
 */
static inline int slot_to_image(int slot)
{
	return slot / SLOTS_PER_IMAGE;
}

/*
 * Helper: get opposite slot in an image pair
 */
static inline int get_opposite_slot(int slot)
{
	return slot ^ 1;
}

/*
 * Helper: erased value as 32-bit word
 */
#define ERASED_VAL_32(x) (((x) << 24) | ((x) << 16) | ((x) << 8) | (x))

int dfu_boot_get_flash_area_id(int slot)
{
	switch (slot) {
	case 0:
		return FIXED_PARTITION_ID(SLOT0_PARTITION);
	case 1:
		return FIXED_PARTITION_ID(SLOT1_PARTITION);
#if FIXED_PARTITION_EXISTS(SLOT2_PARTITION)
	case 2:
		return FIXED_PARTITION_ID(SLOT2_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT3_PARTITION)
	case 3:
		return FIXED_PARTITION_ID(SLOT3_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT4_PARTITION)
	case 4:
		return FIXED_PARTITION_ID(SLOT4_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT5_PARTITION)
	case 5:
		return FIXED_PARTITION_ID(SLOT5_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT6_PARTITION)
	case 6:
		return FIXED_PARTITION_ID(SLOT6_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT7_PARTITION)
	case 7:
		return FIXED_PARTITION_ID(SLOT7_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT8_PARTITION)
	case 8:
		return FIXED_PARTITION_ID(SLOT8_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT9_PARTITION)
	case 9:
		return FIXED_PARTITION_ID(SLOT9_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT10_PARTITION)
	case 10:
		return FIXED_PARTITION_ID(SLOT10_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT11_PARTITION)
	case 11:
		return FIXED_PARTITION_ID(SLOT11_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT12_PARTITION)
	case 12:
		return FIXED_PARTITION_ID(SLOT12_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT13_PARTITION)
	case 13:
		return FIXED_PARTITION_ID(SLOT13_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT14_PARTITION)
	case 14:
		return FIXED_PARTITION_ID(SLOT14_PARTITION);
#endif
#if FIXED_PARTITION_EXISTS(SLOT15_PARTITION)
	case 15:
		return FIXED_PARTITION_ID(SLOT15_PARTITION);
#endif
	default:
		return -EINVAL;
	}
}

int dfu_boot_get_active_slot(int image)
{
	int slot = 0;

#if CONFIG_MCUMGR_GRP_IMG_UPDATABLE_IMAGE_NUMBER > 1
	slot = image * SLOTS_PER_IMAGE;
#elif defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) || \
	defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT)
	int rc;
	uint8_t temp_slot;

	rc = blinfo_lookup(BLINFO_RUNNING_SLOT, &temp_slot, sizeof(temp_slot));
	if (rc <= 0) {
		LOG_ERR("Failed to fetch active slot: %d", rc);
		return -EIO;
	}
	slot = (int)temp_slot;
#else
	/* Single image, check if running from slot1 */
	ARG_UNUSED(image);
#if FIXED_PARTITION_EXISTS(slot1_partition)
	if (FIXED_PARTITION_ID(SLOT1_PARTITION) == ACTIVE_SLOT_FLASH_AREA_ID) {
		slot = 1;
	}
#endif
#endif

	LOG_DBG("Active slot for image %d: %d", image, slot);
	return slot;
}

int dfu_boot_get_active_image(void)
{
	return ACTIVE_IMAGE_INDEX;
}

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET)
/**
 * Get image index from slot number for swap-using-offset secondary slots
 * Returns IMAGE_INDEX_INVALID if slot is not a secondary slot
 */
static int get_image_index_for_secondary_slot(int slot)
{
	int area_id = dfu_boot_get_flash_area_id(slot);

	if (area_id < 0) {
		return IMAGE_INDEX_INVALID;
	}

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

size_t dfu_boot_get_image_offset(int slot)
{
	size_t off = 0;
	int image = get_image_index_for_secondary_slot(slot);

	if (image == IMAGE_INDEX_INVALID) {
		/* Not a secondary slot, no offset needed */
		return 0;
	}

	int area_id = dfu_boot_get_flash_area_id(slot);
	const struct flash_area *fa;
	uint32_t num_sectors = SWAP_USING_OFFSET_SECTOR_UPDATE_BEGIN;
	struct flash_sector sector_data;
	int rc;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		LOG_ERR("Flash open area %d failed: %d", area_id, rc);
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

	LOG_DBG("Image offset for slot %d: 0x%zx", slot, off);
	return off;
}
#else
size_t dfu_boot_get_image_offset(int slot)
{
	ARG_UNUSED(slot);
	return 0;
}
#endif /* CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET */

size_t dfu_boot_get_trailer_status_offset(size_t area_size)
{
	return (size_t)area_size - BOOT_MAGIC_SZ - BOOT_MAX_ALIGN * 2;
}

int dfu_boot_get_erased_val(int slot, uint8_t *erased_val)
{
	const struct flash_area *fa;
	int area_id;
	int rc;

	area_id = dfu_boot_get_flash_area_id(slot);
	if (area_id < 0) {
		return area_id;
	}

	rc = flash_area_open(area_id, &fa);
	if (rc != 0) {
		LOG_ERR("Failed to open flash area %d: %d", area_id, rc);
		return -EIO;
	}

	*erased_val = flash_area_erased_val(fa);
	flash_area_close(fa);

	return 0;
}

int dfu_boot_read(int slot, size_t offset, void *dst, size_t len)
{
	const struct flash_area *fa;
	int area_id;
	int rc;

	area_id = dfu_boot_get_flash_area_id(slot);
	if (area_id < 0) {
		return area_id;
	}

	rc = flash_area_open(area_id, &fa);
	if (rc != 0) {
		LOG_ERR("Failed to open flash area %d: %d", area_id, rc);
		return -EIO;
	}

	rc = flash_area_read(fa, offset, dst, len);
	flash_area_close(fa);

	if (rc != 0) {
		LOG_ERR("Failed to read from flash: %d", rc);
		return -EIO;
	}

	return 0;
}

/**
 * Find TLV area in image
 */
static int find_tlvs(int slot, size_t *start_off, size_t *end_off, uint16_t magic)
{
	struct image_tlv_info tlv_info;
	int rc;

	rc = dfu_boot_read(slot, *start_off, &tlv_info, sizeof(tlv_info));
	if (rc != 0) {
		return rc;
	}

	if (tlv_info.it_magic != magic) {
		return -ENOENT;
	}

	*start_off += sizeof(tlv_info);
	*end_off = *start_off + tlv_info.it_tlv_tot;

	return 0;
}

int dfu_boot_read_img_info(int slot, struct dfu_boot_img_info *info)
{
	struct image_header hdr;
	struct image_tlv tlv;
	size_t data_off;
	size_t data_end;
	size_t img_offset;
	uint8_t erased_val;
	uint32_t erased_val_32;
	bool hash_found = false;
	int rc;
	int area_id;

	if (info == NULL) {
		return -EINVAL;
	}

	memset(info, 0, sizeof(*info));
	info->valid = false;

	area_id = dfu_boot_get_flash_area_id(slot);
	if (area_id < 0) {
		return area_id;
	}

	rc = dfu_boot_get_erased_val(slot, &erased_val);
	if (rc != 0) {
		return rc;
	}

	img_offset = dfu_boot_get_image_offset(slot);

	/* Read image header */
	rc = dfu_boot_read(slot, img_offset, &hdr, sizeof(hdr));
	if (rc != 0) {
		return rc;
	}

	erased_val_32 = ERASED_VAL_32(erased_val);

	/* Check magic */
	if (hdr.ih_magic == erased_val_32) {
		/* Slot is empty */
		return -ENOENT;
	}

	if (hdr.ih_magic != IMAGE_MAGIC) {
		LOG_DBG("Invalid image magic: 0x%08x", hdr.ih_magic);
		return 0;
	}

	/* Extract version */
	info->version.major = hdr.ih_ver.iv_major;
	info->version.minor = hdr.ih_ver.iv_minor;
	info->version.revision = hdr.ih_ver.iv_revision;
	info->version.build_num = hdr.ih_ver.iv_build_num;

	/* Extract other info */
	info->img_size = hdr.ih_img_size;
	info->hdr_size = hdr.ih_hdr_size;
	info->load_addr = hdr.ih_load_addr;

	/* Extract flags */
	info->flags = 0;
	if (hdr.ih_flags & IMAGE_F_NON_BOOTABLE) {
		info->flags |= DFU_BOOT_IMG_F_NON_BOOTABLE;
	}
	if (hdr.ih_flags & IMAGE_F_ROM_FIXED) {
		info->flags |= DFU_BOOT_IMG_F_ROM_FIXED;
	}

	/* Find TLVs to extract hash */
	data_off = img_offset + hdr.ih_hdr_size + hdr.ih_img_size;

	/* Try protected TLVs first */
	rc = find_tlvs(slot, &data_off, &data_end, IMAGE_TLV_PROT_INFO_MAGIC);
	if (rc == 0) {
		data_off = data_end - sizeof(struct image_tlv_info);
	}

	/* Find regular TLVs */
	rc = find_tlvs(slot, &data_off, &data_end, IMAGE_TLV_INFO_MAGIC);
	if (rc != 0) {
		LOG_DBG("No TLVs found in slot %d", slot);
		/* Image without TLVs - still valid but no hash */
		info->valid = true;
		return 0;
	}

	/* Search for hash TLV */
	while (data_off + sizeof(tlv) <= data_end) {
		rc = dfu_boot_read(slot, data_off, &tlv, sizeof(tlv));
		if (rc != 0) {
			return rc;
		}

		if (tlv.it_type == 0xff && tlv.it_len == 0xffff) {
			break;
		}

		if (tlv.it_type == MCUBOOT_HASH_TLV && tlv.it_len == MCUBOOT_HASH_LEN) {
			if (hash_found) {
				LOG_WRN("Multiple hash TLVs found");
				return -EINVAL;
			}

			data_off += sizeof(tlv);
			if (data_off + MCUBOOT_HASH_LEN > data_end) {
				return -EINVAL;
			}

			rc = dfu_boot_read(slot, data_off, info->hash, MCUBOOT_HASH_LEN);
			if (rc != 0) {
				return rc;
			}

			info->hash_len = MCUBOOT_HASH_LEN;
			hash_found = true;
			data_off += MCUBOOT_HASH_LEN;
		} else {
			data_off += sizeof(tlv) + tlv.it_len;
		}
	}

	if (!hash_found) {
		LOG_DBG("No hash TLV found in slot %d", slot);
	}

	info->valid = true;
	return 0;
}

int dfu_boot_validate_header(const void *data, size_t len, struct dfu_boot_img_info *info)
{
	const struct image_header *hdr = (const struct image_header *)data;

	if (len < sizeof(struct image_header)) {
		return -EINVAL;
	}

	if (hdr->ih_magic != IMAGE_MAGIC) {
		return -EINVAL;
	}

	if (info != NULL) {
		memset(info, 0, sizeof(*info));
		info->version.major = hdr->ih_ver.iv_major;
		info->version.minor = hdr->ih_ver.iv_minor;
		info->version.revision = hdr->ih_ver.iv_revision;
		info->version.build_num = hdr->ih_ver.iv_build_num;
		info->flags = hdr->ih_flags;
		info->load_addr = hdr->ih_load_addr;
		info->img_size = hdr->ih_img_size;
		info->hdr_size = hdr->ih_hdr_size;
		info->valid = true;
	}

	return 0;
}

int dfu_boot_get_swap_type(int slot)
{
	int image = slot_to_image(slot);

	switch (mcuboot_swap_type_multi(image)) {
	case BOOT_SWAP_TYPE_NONE:
		return DFU_BOOT_SWAP_TYPE_NONE;
	case BOOT_SWAP_TYPE_TEST:
		return DFU_BOOT_SWAP_TYPE_TEST;
	case BOOT_SWAP_TYPE_PERM:
		return DFU_BOOT_SWAP_TYPE_PERM;
	case BOOT_SWAP_TYPE_REVERT:
		return DFU_BOOT_SWAP_TYPE_REVERT;
	default:
		return DFU_BOOT_SWAP_TYPE_UNKNOWN;
	}
}

#if !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP) && \
	!defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER)
uint8_t dfu_boot_get_slot_state(int slot)
{
	uint8_t flags = 0;
	int swap_type;
	int image = slot_to_image(slot);
	int active_slot = dfu_boot_get_active_slot(image);

	swap_type = dfu_boot_get_swap_type(slot);

	switch (swap_type) {
	case DFU_BOOT_SWAP_TYPE_NONE:
		if (slot == active_slot) {
			flags |= DFU_BOOT_STATE_F_CONFIRMED;
		}
		break;

	case DFU_BOOT_SWAP_TYPE_TEST:
		if (slot == active_slot) {
			flags |= DFU_BOOT_STATE_F_CONFIRMED;
		} else {
			flags |= DFU_BOOT_STATE_F_PENDING;
		}
		break;

	case DFU_BOOT_SWAP_TYPE_PERM:
		if (slot == active_slot) {
			flags |= DFU_BOOT_STATE_F_CONFIRMED;
		} else {
			flags |= DFU_BOOT_STATE_F_PENDING | DFU_BOOT_STATE_F_PERMANENT;
		}
		break;

	case DFU_BOOT_SWAP_TYPE_REVERT:
		if (slot != active_slot) {
			flags |= DFU_BOOT_STATE_F_CONFIRMED;
		}
		break;
	}

	/* Active slot check */
	if (image == dfu_boot_get_active_image() && slot == active_slot) {
		flags |= DFU_BOOT_STATE_F_ACTIVE;
	}

	return flags;
}
#else /* DirectXIP or Firmware Updater mode */
uint8_t dfu_boot_get_slot_state(int slot)
{
	uint8_t flags = 0;
	int image = slot_to_image(slot);
	int active_slot = dfu_boot_get_active_slot(image);

	if (image == dfu_boot_get_active_image() && slot == active_slot) {
		flags = DFU_BOOT_STATE_F_ACTIVE;
#ifdef CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP
	} else {
		struct dfu_boot_img_info sinfo, ainfo;
		int rcs = dfu_boot_read_img_info(slot, &sinfo);
		int rca = dfu_boot_read_img_info(active_slot, &ainfo);

		if (rcs == 0 && rca == 0 && sinfo.valid && ainfo.valid) {
			int cmp = dfu_boot_vercmp(&ainfo.version, &sinfo.version);
			if (cmp < 0 || (cmp == 0 && active_slot > slot)) {
				flags = DFU_BOOT_STATE_F_PENDING | DFU_BOOT_STATE_F_PERMANENT;
			}
		}
#endif
	}

	return flags;
}
#endif /* DirectXIP / Firmware Updater */

#if !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP) && \
	!defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT) && \
	!defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER)
int dfu_boot_get_next_boot_slot(int image, enum dfu_boot_next_type *type)
{
	int active_slot = dfu_boot_get_active_slot(image);
	int state = mcuboot_swap_type_multi(image);
	int slot = get_opposite_slot(active_slot);
	enum dfu_boot_next_type lt = DFU_BOOT_NEXT_TYPE_NORMAL;

	switch (state) {
	case BOOT_SWAP_TYPE_NONE:
		slot = active_slot;
		break;
	case BOOT_SWAP_TYPE_PERM:
		/* Normal boot to opposite slot */
		break;
	case BOOT_SWAP_TYPE_REVERT:
		lt = DFU_BOOT_NEXT_TYPE_REVERT;
		break;
	case BOOT_SWAP_TYPE_TEST:
		lt = DFU_BOOT_NEXT_TYPE_TEST;
		break;
	default:
		LOG_DBG("Unexpected swap state %d", state);
		return -EINVAL;
	}

	LOG_DBG("Next boot slot for image %d: slot=%d, type=%d", image, slot, lt);

	if (type != NULL) {
		*type = lt;
	}

	return slot;
}
#elif defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP) || \
	defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT)
int dfu_boot_get_next_boot_slot(int image, enum dfu_boot_next_type *type)
{
	struct dfu_boot_img_info ainfo, oinfo;
	int active_slot = dfu_boot_get_active_slot(image);
	int other_slot = get_opposite_slot(active_slot);
	enum dfu_boot_next_type lt = DFU_BOOT_NEXT_TYPE_NORMAL;
	int return_slot = active_slot;
	int rca, rco;

	rca = dfu_boot_read_img_info(active_slot, &ainfo);
	rco = dfu_boot_read_img_info(other_slot, &oinfo);

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT)
	/* DirectXIP with revert needs to check boot state */
	const struct flash_area *fa;
	struct boot_swap_state active_state, other_state;
	int fa_id;

	memset(&active_state, 0, sizeof(active_state));
	memset(&other_state, 0, sizeof(other_state));

	fa_id = dfu_boot_get_flash_area_id(active_slot);
	if (flash_area_open(fa_id, &fa) == 0) {
		boot_read_swap_state(fa, &active_state);
		flash_area_close(fa);
	}

	fa_id = dfu_boot_get_flash_area_id(other_slot);
	if (flash_area_open(fa_id, &fa) == 0) {
		boot_read_swap_state(fa, &other_state);
		flash_area_close(fa);
	}

	if (active_state.magic == BOOT_MAGIC_GOOD &&
		active_state.copy_done == BOOT_FLAG_SET &&
		active_state.image_ok != BOOT_FLAG_SET) {
		/* Active slot is pending revert */
		lt = DFU_BOOT_NEXT_TYPE_REVERT;
		return_slot = other_slot;
	} else if (other_state.magic == BOOT_MAGIC_GOOD &&
		   other_state.image_ok != BOOT_FLAG_SET) {
		/* Other slot is pending test */
		if (rca == 0 && rco == 0 && ainfo.valid && oinfo.valid) {
			int cmp = dfu_boot_vercmp(&ainfo.version, &oinfo.version);
			if (cmp < 0 || (cmp == 0 && active_slot > other_slot)) {
				lt = DFU_BOOT_NEXT_TYPE_TEST;
				return_slot = other_slot;
			}
		}
	}
#else
	/* Plain DirectXIP: higher version wins */
	if (rca == 0 && rco == 0 && ainfo.valid && oinfo.valid) {
		int cmp = dfu_boot_vercmp(&ainfo.version, &oinfo.version);
		if (cmp < 0 || (cmp == 0 && active_slot > other_slot)) {
			return_slot = other_slot;
		}
	}
#endif

	if (type != NULL) {
		*type = lt;
	}

	return return_slot;
}
#else /* Firmware Updater mode - no next boot slot concept */
int dfu_boot_get_next_boot_slot(int image, enum dfu_boot_next_type *type)
{
	if (type != NULL) {
		*type = DFU_BOOT_NEXT_TYPE_NORMAL;
	}
	return dfu_boot_get_active_slot(image);
}
#endif

bool dfu_boot_any_pending(void)
{
	return (dfu_boot_get_slot_state(0) & DFU_BOOT_STATE_F_PENDING) ||
		   (dfu_boot_get_slot_state(1) & DFU_BOOT_STATE_F_PENDING);
}

int dfu_boot_slot_in_use(int slot)
{
	int image = slot_to_image(slot);
	int active_slot = dfu_boot_get_active_slot(image);

#if !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP) && \
	!defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) && \
	!defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD_WITH_REVERT) && \
	!defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER)
	enum dfu_boot_next_type type = DFU_BOOT_NEXT_TYPE_NORMAL;
	int nbs = dfu_boot_get_next_boot_slot(image, &type);

	if (slot == nbs && type == DFU_BOOT_NEXT_TYPE_REVERT) {
		LOG_DBG("Slot %d refused: revert pending", slot);
		return 1;
	}

	if ((slot == nbs && type == DFU_BOOT_NEXT_TYPE_TEST) ||
		(active_slot != nbs && type == DFU_BOOT_NEXT_TYPE_NORMAL)) {
#if defined(CONFIG_MCUMGR_GRP_IMG_ALLOW_ERASE_PENDING)
		LOG_DBG("Slot %d: allowed erase pending", slot);
#else
		LOG_DBG("Slot %d refused: pending", slot);
		return 1;
#endif
	}
#endif

	return (active_slot == slot) ? 1 : 0;
}

int dfu_boot_set_pending(int slot, bool permanent)
{
	int image = slot_to_image(slot);
	int rc;

	rc = boot_set_pending_multi(image, permanent ? 1 : 0);
	if (rc != 0) {
		LOG_ERR("Failed to set pending for slot %d: %d", slot, rc);
		return -EIO;
	}

	return 0;
}

int dfu_boot_confirm(void)
{
	int rc;

	rc = boot_write_img_confirmed();
	if (rc != 0) {
		LOG_ERR("Failed to confirm image: %d", rc);
		return -EIO;
	}

	return 0;
}

bool dfu_boot_is_confirmed(void)
{
	struct boot_swap_state state;
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(ACTIVE_SLOT_FLASH_AREA_ID, &fa);
	if (rc) {
		return false;
	}

	rc = boot_read_swap_state(fa, &state);
	flash_area_close(fa);

	if (rc != 0) {
		return false;
	}

	if (state.magic == BOOT_MAGIC_UNSET) {
		/* This is initial/preprogrammed image.
		 * Such image can neither be reverted nor physically confirmed.
		 * Treat this image as confirmed which ensures consistency
		 * with `boot_write_img_confirmed...()` procedures.
		 */
		return true;
	}

	return state.image_ok == BOOT_FLAG_SET;
}

int dfu_boot_erase_slot(int slot)
{
	const struct flash_area *fa;
	int area_id;
	int rc;

	area_id = dfu_boot_get_flash_area_id(slot);
	if (area_id < 0) {
		return area_id;
	}

	rc = flash_area_open(area_id, &fa);
	if (rc != 0) {
		LOG_ERR("Failed to open flash area %d: %d", area_id, rc);
		return -EIO;
	}

	rc = flash_area_flatten(fa, 0, fa->fa_size);
	flash_area_close(fa);

	if (rc != 0) {
		LOG_ERR("Failed to erase slot %d: %d", slot, rc);
		return -EIO;
	}

	return 0;
}

int dfu_boot_vercmp(const struct dfu_boot_img_version *a,
			const struct dfu_boot_img_version *b)
{
	if (a->major != b->major) {
		return (a->major < b->major) ? -1 : 1;
	}
	if (a->minor != b->minor) {
		return (a->minor < b->minor) ? -1 : 1;
	}
	if (a->revision != b->revision) {
		return (a->revision < b->revision) ? -1 : 1;
	}
#if defined(CONFIG_MCUMGR_GRP_IMG_VERSION_CMP_USE_BUILD_NUMBER)
	if (a->build_num != b->build_num) {
		return (a->build_num < b->build_num) ? -1 : 1;
	}
#endif
	return 0;
}
	int dfu_boot_ver_str(const struct dfu_boot_img_version *ver,
			 char *buf, size_t buf_size)
{
	int rc;

	if (buf == NULL || buf_size == 0) {
		return -EINVAL;
	}

	rc = snprintf(buf, buf_size, "%u.%u.%u",
			  (unsigned int)ver->major,
			  (unsigned int)ver->minor,
			  (unsigned int)ver->revision);

	if (rc < 0) {
		return -EINVAL;
	}

	if ((size_t)rc >= buf_size) {
		return -ENOSPC;
	}

	if (ver->build_num != 0) {
		int rc2 = snprintf(buf + rc, buf_size - rc, ".%u",
				   (unsigned int)ver->build_num);
		if (rc2 < 0) {
			return -EINVAL;
		}
		if ((size_t)rc2 >= (buf_size - rc)) {
			return -ENOSPC;
		}
		rc += rc2;
	}

	return rc;
}