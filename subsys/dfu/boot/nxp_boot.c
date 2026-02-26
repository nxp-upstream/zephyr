/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/dfu/dfu_boot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dfu_boot_nxp, CONFIG_IMG_MANAGER_LOG_LEVEL);

/*
 * Slot partition definitions
 */
#define SLOT0_PARTITION slot0_partition
#define SLOT1_PARTITION slot1_partition

#define ERASED_VAL_32(x) (((x) << 24) | ((x) << 16) | ((x) << 8) | (x))

/*
 * IFR0 OTACFG page configuration
 */
#define IFR0_FLASH_NODE      DT_CHOSEN(zephyr_flash_controller)
#define IFR0_BASE            DT_REG_ADDR(DT_NODELABEL(ifr0))
#define IFR_SECTOR_SIZE      DT_PROP(DT_NODELABEL(ifr0), erase_block_size)
#define IFR_SECTOR_OFFSET(n) ((n) * IFR_SECTOR_SIZE)
#define IFR_SECT_OTA_CFG     DT_PROP(DT_NODELABEL(ifr0), ota_cfg_sector)
#define IFR_OTA_CFG_ADDR     (IFR0_BASE + IFR_SECTOR_OFFSET(IFR_SECT_OTA_CFG))

/*
 * OTA Configuration (stored in User IFR0 OTACFG page)
 */
#define OTA_UPDATE_AVAILABLE_MAGIC 0x746f4278U
#define OTA_EXTERNAL_FLASH_MAGIC   0x74784578U
#define OTA_UPDATE_SUCCESS         0x5ac3c35aU
#define OTA_UPDATE_FAIL_SB3        0x4412d283U
#define OTA_UPDATE_FAIL_ERASE      0x2d61e1acU

/* Feature unlock key */
static const uint8_t ota_unlock_key[16] = {0x61, 0x63, 0x74, 0x69, 0x76, 0x61, 0x74, 0x65,
					   0x53, 0x65, 0x63, 0x72, 0x65, 0x74, 0x78, 0x4d};

/*
 * OTA Configuration structure (matches IFR0 OTACFG page layout)
 */
__packed struct nxp_ota_config {
	uint32_t update_available; /* 0x00: OTA_UPDATE_AVAILABLE_MAGIC if update pending */
	uint32_t reserved1;        /* 0x04: Reserved */
	uint32_t reserved2;        /* 0x08: Reserved */
	uint32_t reserved3;        /* 0x0C: Reserved */
	uint32_t dump_location;    /* 0x10: OTA_EXTERNAL_FLASH_MAGIC or internal */
	uint32_t baud_rate;        /* 0x14: LPSPI baud rate for external flash */
	uint32_t dump_address;     /* 0x18: Start address of SB3 file */
	uint32_t file_size;        /* 0x1C: Size of SB3 file in bytes */
	uint32_t update_status;    /* 0x20: Status after update attempt */
	uint32_t reserved4;        /* 0x24: Reserved */
	uint32_t reserved5;        /* 0x28: Reserved */
	uint32_t reserved6;        /* 0x2C: Reserved */
	uint8_t unlock_key[16];    /* 0x30: Feature unlock key */
};

/*
 * SB3 Image Header definitions
 */
#define IMAGE_HEADER_MAGIC_SB3 0x33766273U /* "sbv3" in little-endian */

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

size_t dfu_boot_get_image_start_offset(int slot)
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

	if ((hdr->sb3_version_major != 3) && (hdr->sb3_version_minor != 1)) {
		LOG_DBG("Header has unsupported SB3 version: %u.%u", hdr->sb3_version_major,
			hdr->sb3_version_minor);
		return -ENOENT;
	}

	if ((hdr->block_size != 292) && (hdr->block_size != 308)) {
		LOG_DBG("Header has invalid block size: %u, unable to determine hash type",
			hdr->block_size);
		return -ENOENT;
	}

	if (info != NULL) {
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

		if (hdr->block_size == 292) {
			/* SHA-256 */
			info->hash_len = 32;
		} else {
			/* SHA-384 */
			info->hash_len = 48;
		}

		info->valid = true;
	}

	return 0;
}

int dfu_boot_read_img_info(int slot, struct dfu_boot_img_info *info)
{
	struct image_header hdr;
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

	/* If an update just occurred, clear the OTA configuration page and erase the slot1
	 * to simulate a confirmed state
	 */
	if (slot == 1) {
		if (get_ota_update_status() == OTA_UPDATE_SUCCESS) {
			erase_otacfg_page();
			dfu_boot_erase_slot(slot);
		}
	}

	/* Read image header */
	rc = dfu_boot_read(slot, 0, &hdr, sizeof(hdr));
	if (rc != 0) {
		return rc;
	}

	erased_val_32 = ERASED_VAL_32(erased_val);

	if (slot == 0) {
		/* slot0 is always the active image and is not in SB3 format but plain binary
		 * so we need to fake the slot information
		 */
		info->img_size = FIXED_PARTITION_SIZE(SLOT0_PARTITION);
		info->hdr_size = 0;
		info->load_addr = 0;
		info->hash_len = DFU_BOOT_IMG_SHA_LEN;
		info->valid = true;
	} else {
		rc = parse_sb3_header(&hdr, info);
		if (rc != 0) {
			LOG_DBG("Failed to parse SB3 header from slot %d", slot);
			return rc;
		}

		rc = dfu_boot_read(slot, info->hdr_size, info->hash, info->hash_len);
		if (rc != 0) {
			LOG_DBG("Failed to read image hash from slot %d", slot);
			return rc;
		}
	}

	if (slot == 0) {
		info->flags = DFU_BOOT_STATE_F_CONFIRMED | DFU_BOOT_STATE_F_ACTIVE;
	} else {
		/* Check if OTACFG has a pending update */
		if (is_ota_update_pending()) {
			info->flags = DFU_BOOT_STATE_F_PENDING;
		}
		/* If no pending update, flags remain 0 (slot is available) */
	}

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
	}

	/* parse the header (info can be NULL) */
	rc = parse_sb3_header(hdr, info);
	if (rc != 0) {
		LOG_DBG("sb3 header is invalid.");
		return rc;
	}

	return rc;
}

int dfu_boot_get_swap_type(int image_index)
{
	ARG_UNUSED(image_index);

	/*
	 * NXP ROM boot architecture:
	 * - slot0: Always the active/running image, no swap
	 * - slot1: Staging area, check OTACFG for pending update
	 */

	/* Check if there's a pending update in OTACFG */
	if (is_ota_update_pending()) {
		return DFU_BOOT_SWAP_TYPE_TEST;
	}

	return DFU_BOOT_SWAP_TYPE_NONE;
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
	cfg.dump_location = 0;         /* Internal flash */
	cfg.baud_rate = 0;             /* Not used for internal flash */
	cfg.dump_address = fa->fa_off; /* Physical address of slot1 */
	cfg.file_size = info.img_size;
	cfg.update_status = 0;
	memcpy(cfg.unlock_key, ota_unlock_key, sizeof(ota_unlock_key));

	flash_area_close(fa);

	LOG_INF("Setting pending for slot %d: addr=0x%08x, size=%u", slot, cfg.dump_address,
		cfg.file_size);

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
