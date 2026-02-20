/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dfu/dfu_boot.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(dfu_boot_nxp, CONFIG_IMG_MANAGER_LOG_LEVEL);

/*
 * Slot partition definitions
 */
#define SLOT0_PARTITION  slot0_partition
#define SLOT1_PARTITION  slot1_partition

/*
 * Slots per image configuration
 * Note: In NXP ROM boot architecture, slot1 is a staging area, not a swap partner
 */
#define SLOTS_PER_IMAGE 2

/*
 * Active image is always 0 (slot0 is always the running image)
 */
#define ACTIVE_IMAGE_INDEX 0

/*
 * IFR0 OTACFG page configuration
 */
#define IFR0_FLASH_NODE		DT_CHOSEN(zephyr_flash_controller)
#define IFR0_BASE		0x02000000U
#define IFR_SECTOR_SIZE		8192U
#define IFR_SECTOR_OFFSET(n)	((n) * IFR_SECTOR_SIZE)
#define IFR_SECT_OTA_CFG	3U  /* OTACFG sector */
#define IFR_OTA_CFG_ADDR	(IFR0_BASE + IFR_SECTOR_OFFSET(IFR_SECT_OTA_CFG))

/*
 * OTA Configuration (stored in User IFR0 OTACFG page)
 */
#define OTA_UPDATE_AVAILABLE_MAGIC	0x746f4278U  /* "xBot" */
#define OTA_EXTERNAL_FLASH_MAGIC	0x74784578U  /* "xExt" */
#define OTA_UPDATE_SUCCESS		0x5ac3c35aU
#define OTA_UPDATE_FAIL_SB3		0x4412d283U
#define OTA_UPDATE_FAIL_ERASE		0x2d61e1acU

/* Feature unlock key */
static const uint8_t ota_unlock_key[16] = {
	0x61, 0x63, 0x74, 0x69, 0x76, 0x61, 0x74, 0x65,
	0x53, 0x65, 0x63, 0x72, 0x65, 0x74, 0x78, 0x4d
};

/*
 * OTA Configuration structure (matches IFR0 OTACFG page layout)
 */
__packed struct nxp_ota_config {
	uint32_t update_available;	/* 0x00: OTA_UPDATE_AVAILABLE_MAGIC if update pending */
	uint32_t reserved1;		/* 0x04: Reserved */
	uint32_t reserved2;		/* 0x08: Reserved */
	uint32_t reserved3;		/* 0x0C: Reserved */
	uint32_t dump_location;		/* 0x10: OTA_EXTERNAL_FLASH_MAGIC or internal */
	uint32_t baud_rate;		/* 0x14: LPSPI baud rate for external flash */
	uint32_t dump_address;		/* 0x18: Start address of SB3 file */
	uint32_t file_size;		/* 0x1C: Size of SB3 file in bytes */
	uint32_t update_status;		/* 0x20: Status after update attempt */
	uint32_t reserved4;		/* 0x24: Reserved */
	uint32_t reserved5;		/* 0x28: Reserved */
	uint32_t reserved6;		/* 0x2C: Reserved */
	uint8_t unlock_key[16];		/* 0x30: Feature unlock key */
};

/*
 * SB3 Image Header definitions
 */
#define IMAGE_HEADER_MAGIC_SB3	0x33766273U  /* "sbv3" in little-endian */

__packed struct image_header {
	uint32_t magic;
	uint16_t sb3_version_minor;
	uint16_t sb3_version_major;
	uint32_t flags;
	uint32_t block_count;
	uint32_t block_size;
	uint32_t timestamp_low;
	uint32_t timestamp_high;
	uint32_t fw_version;
	uint32_t total_length;
	uint32_t image_type;
	uint32_t cert_offset;
	uint8_t description[16];
};

/*
 * Helper: erased value as 32-bit word
 */
#define ERASED_VAL_32(x) (((x) << 24) | ((x) << 16) | ((x) << 8) | (x))

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
 * IFR0 OTACFG page access functions
 */


static int erase_otacfg_page(void)
{
	const struct device *flash_dev;
	int rc;

	flash_dev = DEVICE_DT_GET(IFR0_FLASH_NODE);
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}

	/* Erase the OTACFG sector first */
	rc = flash_erase(flash_dev, IFR_OTA_CFG_ADDR, IFR_SECTOR_SIZE);
	if (rc != 0) {
		LOG_ERR("Failed to erase OTACFG page: %d", rc);
		return -EIO;
	}

	return 0;
}

/**
 * @brief Write OTA configuration to IFR0 OTACFG page
 *
 * @param cfg Pointer to OTA configuration structure
 * @return 0 on success, negative error code on failure
 */
static int write_otacfg_page(const struct nxp_ota_config *cfg)
{
	const struct device *flash_dev;
	int rc;

	flash_dev = DEVICE_DT_GET(IFR0_FLASH_NODE);
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}

	/* Erase the OTACFG sector first */
	rc = flash_erase(flash_dev, IFR_OTA_CFG_ADDR, IFR_SECTOR_SIZE);
	if (rc != 0) {
		LOG_ERR("Failed to erase OTACFG page: %d", rc);
		return -EIO;
	}

	/* Write the OTA configuration */
	rc = flash_write(flash_dev, IFR_OTA_CFG_ADDR, cfg, sizeof(*cfg));
	if (rc != 0) {
		LOG_ERR("Failed to write OTACFG page: %d", rc);
		return -EIO;
	}

	LOG_INF("OTACFG page programmed successfully");
	return 0;
}

/**
 * @brief Read OTA configuration from IFR0 OTACFG page
 *
 * @param cfg Pointer to OTA configuration structure to fill
 * @return 0 on success, negative error code on failure
 */
static int read_otacfg_page(struct nxp_ota_config *cfg)
{
	const struct device *flash_dev;
	int rc;

	flash_dev = DEVICE_DT_GET(IFR0_FLASH_NODE);
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}

	rc = flash_read(flash_dev, IFR_OTA_CFG_ADDR, cfg, sizeof(*cfg));
	if (rc != 0) {
		LOG_ERR("Failed to read OTACFG page: %d", rc);
		return -EIO;
	}

	return 0;
}

/**
 * @brief Check if an OTA update is already pending in OTACFG
 *
 * @return true if update is pending, false otherwise
 */
static bool is_ota_update_pending(void)
{
	struct nxp_ota_config cfg;
	int rc;

	rc = read_otacfg_page(&cfg);
	if (rc != 0) {
		return false;
	}

	return (cfg.update_available == OTA_UPDATE_AVAILABLE_MAGIC);
}

/**
 * @brief Get the last OTA update status
 *
 * @return OTA update status value, or 0 if read fails
 */
static uint32_t get_ota_update_status(void)
{
	struct nxp_ota_config cfg;
	int rc;

	rc = read_otacfg_page(&cfg);
	if (rc != 0) {
		return 0;
	}

	return cfg.update_status;
}

/*
 * DFU Boot API implementation
 */

int dfu_boot_get_flash_area_id(int slot)
{
	switch (slot) {
	case 0:
		return FIXED_PARTITION_ID(SLOT0_PARTITION);
	case 1:
		return FIXED_PARTITION_ID(SLOT1_PARTITION);
	default:
		return -EINVAL;
	}
}

int dfu_boot_get_active_slot(int image)
{
	ARG_UNUSED(image);

	/* In NXP ROM boot architecture, slot0 is always the active slot */
	LOG_DBG("Active slot for image %d: 0", image);
	return 0;
}

int dfu_boot_get_active_image(void)
{
	return ACTIVE_IMAGE_INDEX;
}

size_t dfu_boot_get_image_offset(int slot)
{
	ARG_UNUSED(slot);
	/* No offset needed - images start at beginning of partition */
	return 0;
}

size_t dfu_boot_get_trailer_status_offset(size_t area_size)
{
	ARG_UNUSED(area_size);
	/* NXP ROM boot doesn't use trailer status in the image area */
	return 0;
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
 * @brief Parse SB3 header and extract image info
 */
static int parse_sb3_header(const struct image_header *hdr, struct dfu_boot_img_info *info)

{
	/* Check for SB3 image */
	if (hdr->magic != IMAGE_HEADER_MAGIC_SB3) {
		LOG_DBG("Header has invalid magic: 0x%08x", hdr->magic);
		return -ENOENT;
	}

	if ((hdr->sb3_version_major != 3) && (hdr->sb3_version_minor != 1))
	{
		LOG_DBG("Header has unsupported SB3 version: %u.%u", hdr->sb3_version_major, hdr->sb3_version_minor);
		return -ENOENT;
	}

	if ((hdr->block_size != 292) && (hdr->block_size != 308))
	{
		LOG_DBG("Header has invalid block size: %u, unable to determine hash type", hdr->block_size);
		return -ENOENT;
	}

	/* SB3 firmware version format is not enforced, assume following encoding:
	 * major.minor.revision packed in 32-bit value
	 */
	info->version.major = (hdr->fw_version >> 24) & 0xFF;
	info->version.minor = (hdr->fw_version >> 16) & 0xFF;
	info->version.revision = hdr->fw_version & 0xFFFF;
	info->version.build_num = 0;
	info->img_size = hdr->total_length + (hdr->block_count * hdr->block_size);
	info->hdr_size = sizeof(struct image_header);
	info->load_addr = 0;
	info->flags = 0;

	if(hdr->block_size == 292)
	{
		/* SHA-256 */
		info->hash_len = 32;
	}
	else
	{
		/* SHA-384 */
		info->hash_len = 48;
	}

	info->valid = true;

	return 0;
}

int dfu_boot_read_img_info(int slot, struct dfu_boot_img_info *info)
{
	struct image_header hdr;
	size_t img_offset;
	uint8_t erased_val;
	uint32_t erased_val_32;
	int rc;

	if (info == NULL) {
		return -EINVAL;
	}

	memset(info, 0, sizeof(*info));
	info->valid = false;

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

	if(slot == 0)
	{
		/* slot0 is always the active image and is not in SB3 format but plain binary
		 * so we need to fake the slot information */
		info->img_size = FIXED_PARTITION_SIZE(SLOT0_PARTITION);
		info->hdr_size = 0;
		info->load_addr = 0;
		info->hash_len = CONFIG_IMG_HASH_LEN;
		info->valid = true;
	}
	else
	{
		rc = parse_sb3_header(&hdr, info);
		if (rc != 0) {
			LOG_DBG("Failed to parse SB3 header from slot %d", slot);
			return rc;
		}

		rc = dfu_boot_read(slot, img_offset + info->hdr_size, info->hash, info->hash_len);
		if (rc != 0) {
			LOG_DBG("Failed to read image hash from slot %d", slot);
			return rc;
		}
	}

	info->flags = dfu_boot_get_slot_state(slot);

	// else {
	//     // info->flags |= DFU_BOOT_STATE_F_PENDING;
	//     info->version.major++;
	// }

	return rc;
}

int dfu_boot_validate_header(const void *data, size_t len, struct dfu_boot_img_info *info)
{
	int rc = 0;
	const struct image_header *hdr = (const struct image_header *)data;

	if (len < sizeof(uint32_t)) {
		return -EINVAL;
	}

	if (info != NULL) {
		memset(info, 0, sizeof(*info));

		rc = parse_sb3_header(hdr, info);
		if (rc != 0) {
			LOG_DBG("sb3 header is invalid.");
			return rc;
		}
	}

	return rc;
}

int dfu_boot_get_swap_type(int slot)
{
	/*
	 * NXP ROM boot architecture:
	 * - slot0: Always the active/running image, no swap
	 * - slot1: Staging area, check OTACFG for pending update
	 */
	if (slot == 0) {
		return DFU_BOOT_SWAP_TYPE_NONE;
	}

	/* Check if there's a pending update in OTACFG */
	if (is_ota_update_pending()) {
		return DFU_BOOT_SWAP_TYPE_PERM;  /* Overwrite is permanent */
	}

	/* Check if there's a valid image in slot1 */
	struct dfu_boot_img_info info;
	int rc = dfu_boot_read_img_info(slot, &info);

	if (rc == 0 && info.valid) {
		/* Image present but not yet marked for update */
		return DFU_BOOT_SWAP_TYPE_NONE;
	}

	return DFU_BOOT_SWAP_TYPE_NONE;
}

uint8_t dfu_boot_get_slot_state(int slot)
{
	uint8_t flags = 0;

	/*
	 * NXP ROM boot architecture:
	 * - slot0: Always active, confirmed
	 * - slot1: Staging area, pending if OTACFG indicates update
	 */
	if (slot == 0) {
		flags = DFU_BOOT_STATE_F_CONFIRMED | DFU_BOOT_STATE_F_ACTIVE;
	} else {
		/* Check if OTACFG has a pending update */
		if (is_ota_update_pending()) {
			flags = DFU_BOOT_STATE_F_PENDING;
		}
		/* If no pending update, flags remain 0 (slot is available) */
	}

	return flags;
}

int dfu_boot_get_next_boot_slot(int image, enum dfu_boot_next_type *type)
{
	ARG_UNUSED(image);
	int slot;

	/* NXP ROM architecture is not slot-based, but staging-based.
	 * This means, we always boot from "slot0", "slot1" is just a staging area where the new
	 * image is dumped, and it can be another core image, so it won't necessarily overwrite
	 * the slot0.
	 * To integrate with the slot-based approach of img_mgmt and dfu, we report the next boot slot
	 * as being slot1 only when the ota update is pending in IFR, just so it can show up as
	 * pending in the SMP report. */
	if(is_ota_update_pending())
	{
		slot = 1;
		if (type != NULL) {
			*type = DFU_BOOT_NEXT_TYPE_TEST;
		}
	}
	else
	{
		slot = 0;
		if (type != NULL) {
			*type = DFU_BOOT_NEXT_TYPE_NORMAL;
		}
	}

	return slot;
}

bool dfu_boot_any_pending(void)
{
	/* Check if OTACFG has a pending update */
	return is_ota_update_pending();
}

int dfu_boot_slot_in_use(int slot)
{
	/*
	 * slot0: Always in use (active)
	 * slot1: Never "in use" from upload perspective - it's always available
	 *        for staging new images
	 */
	if (slot == 0) {
		return 1;  /* Active slot, cannot overwrite */
	}

	return 0;  /* Staging slot is always available for upload */
}

int dfu_boot_set_pending(int slot, bool permanent)
{
	const struct flash_area *fa;
	struct nxp_ota_config cfg;
	int area_id;
	int rc;

	ARG_UNUSED(permanent);

	/*
	 * In NXP ROM boot architecture, the OTA configuration in IFR0 OTACFG
	 * page needs to be programmed to trigger the update on next boot.
	 */
	if (slot == 0) {
		LOG_ERR("Cannot set pending on active slot");
		return -EINVAL;
	}

	/* Verify slot1 contains a valid image */
	struct dfu_boot_img_info info;

	rc = dfu_boot_read_img_info(slot, &info);
	if (rc != 0 || !info.valid) {
		LOG_ERR("No valid image in slot %d", slot);
		return -ENOENT;
	}

	/* Get the flash area to determine the physical address */
	area_id = dfu_boot_get_flash_area_id(slot);
	if (area_id < 0) {
		return area_id;
	}

	rc = flash_area_open(area_id, &fa);
	if (rc != 0) {
		LOG_ERR("Failed to open flash area %d: %d", area_id, rc);
		return -EIO;
	}

	/* Prepare OTA configuration */
	memset(&cfg, 0, sizeof(cfg));
	cfg.update_available = OTA_UPDATE_AVAILABLE_MAGIC;
	cfg.dump_location = 0;  /* Internal flash */
	cfg.baud_rate = 0;      /* Not used for internal flash */
	cfg.dump_address = fa->fa_off;  /* Physical address of slot1 */
	cfg.file_size = info.img_size;
	cfg.update_status = 0;
	memcpy(cfg.unlock_key, ota_unlock_key, sizeof(ota_unlock_key));

	flash_area_close(fa);

	LOG_INF("Setting pending for slot %d: addr=0x%08x, size=%u",
		slot, cfg.dump_address, cfg.file_size);

	/* Write the OTA configuration to IFR0 OTACFG page */
	rc = write_otacfg_page(&cfg);
	if (rc != 0) {
		LOG_ERR("Failed to program OTACFG page: %d", rc);
		return rc;
	}

	LOG_INF("OTA update scheduled for next boot");
	return 0;
}

int dfu_boot_confirm(void)
{
	/*
	 * In NXP ROM boot architecture, confirmation is implicit.
	 * Once the ROM successfully updates slot0 from slot1, the update
	 * is permanent. There's no revert mechanism.
	 *
	 * This function is a no-op for this backend.
	 */
	LOG_DBG("Confirm called - no-op for NXP ROM boot");
	return 0;
}

bool dfu_boot_is_confirmed(void)
{
	/*
	 * In NXP ROM boot architecture, the running image (slot0) is
	 * always considered confirmed since there's no revert mechanism.
	 */
	return true;
}

int dfu_boot_erase_slot(int slot)
{
	const struct flash_area *fa;
	int area_id;
	int rc;

	if (slot == 0) {
		LOG_ERR("Cannot erase active slot");
		return -EACCES;
	}

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

	rc = erase_otacfg_page();
	if (rc != 0) {
		LOG_ERR("Failed to erase OTACFG page: %d", rc);
	}

	LOG_INF("Erased slot %d", slot);
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