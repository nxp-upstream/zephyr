/*
 * Copyright (c) 2018-2021 mcumgr authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#ifdef CONFIG_FLASH
#include <zephyr/drivers/flash.h>
#endif
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/dfu_boot.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/logging/log.h>
#include <assert.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>

#include <mgmt/mcumgr/grp/img_mgmt/img_mgmt_priv.h>

#if defined(CONFIG_MCUMGR_GRP_IMG_TOO_LARGE_BOOTLOADER_INFO) && defined(CONFIG_RETENTION)
#include <zephyr/retention/retention.h>
#include <zephyr/retention/blinfo.h>
#endif

LOG_MODULE_DECLARE(mcumgr_img_grp, CONFIG_MCUMGR_GRP_IMG_LOG_LEVEL);

#if CONFIG_MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER
#define SLOTS_PER_IMAGE 1
#else
#define SLOTS_PER_IMAGE 2
#endif

/**
 * Determines if the specified area of flash is completely unwritten.
 *
 * @param	fa	pointer to flash area to scan
 *
 * @return	0 when not empty, 1 when empty, negative errno code on error.
 */
static int img_mgmt_flash_check_empty_inner(const struct flash_area *fa)
{
	uint32_t data[16];
	off_t addr;
	off_t end;
	int bytes_to_read;
	int rc;
	int i;
	uint8_t erased_val;
	uint32_t erased_val_32;

	assert(fa->fa_size % 4 == 0);

	erased_val = flash_area_erased_val(fa);
	erased_val_32 = ERASED_VAL_32(erased_val);

	end = fa->fa_size;
	for (addr = 0; addr < end; addr += sizeof(data)) {
		if (end - addr < sizeof(data)) {
			bytes_to_read = end - addr;
		} else {
			bytes_to_read = sizeof(data);
		}

		rc = flash_area_read(fa, addr, data, bytes_to_read);
		if (rc < 0) {
			LOG_ERR("Failed to read data from flash area: %d", rc);
			return IMG_MGMT_ERR_FLASH_READ_FAILED;
		}

		for (i = 0; i < bytes_to_read / 4; i++) {
			if (data[i] != erased_val_32) {
				return 0;
			}
		}
	}

	return 1;
}

#ifndef CONFIG_IMG_ERASE_PROGRESSIVELY
/* Check if area is empty
 *
 * @param	fa_id	ID of flash area to scan.
 *
 * @return	0 when not empty, 1 when empty, negative errno code on error.
 */
static int img_mgmt_flash_check_empty(uint8_t fa_id)
{
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(fa_id, &fa);
	if (rc == 0) {
		rc = img_mgmt_flash_check_empty_inner(fa);

		flash_area_close(fa);
	} else {
		LOG_ERR("Failed to open flash area ID %u: %d", fa_id, rc);
		rc = IMG_MGMT_ERR_FLASH_OPEN_FAILED;
	}

	return rc;
}
#endif

/**
 * Get flash_area ID for a image number; actually the slots are images
 * for Zephyr, as slot 0 of image 0 is image_0, slot 0 of image 1 is
 * image_2 and so on. The function treats slot numbers as absolute
 * slot number starting at 0.
 */
int img_mgmt_flash_area_id(int slot)
{
	return dfu_boot_get_flash_area_id(slot);
}

#if CONFIG_MCUMGR_GRP_IMG_UPDATABLE_IMAGE_NUMBER == 1
/**
 * In normal operation this function will select between first two slot
 * (in reality it just checks whether second slot can be used), ignoring the
 * slot parameter.
 * When CONFIG_MCUMGR_GRP_IMG_DIRECT_UPLOAD is defined it will check if given
 * slot is available, and allowed, for DFU; providing 0 as a parameter means
 * find any unused and non-active available (auto-select); any other positive
 * value is direct (slot + 1) to be used; if checks are positive, then area
 * ID is returned, -1 is returned otherwise.
 * Note that auto-selection is performed only between the two first slots.
 */
static int img_mgmt_get_unused_slot_area_id(int slot)
{
#if defined(CONFIG_MCUMGR_GRP_IMG_DIRECT_UPLOAD)
	slot--;
	if (slot < -1) {
		return -1;
	} else if (slot == -1) {
#endif
		/*
		 * Auto select slot; note that this is performed only between two first
		 * slots, at this point, which will require fix when Direct-XIP, which
		 * may support more slots, gets support within Zephyr.
		 */
		for (slot = 0; slot < 2; slot++) {
			if (img_mgmt_slot_in_use(slot) == 0) {
				int area_id = img_mgmt_flash_area_id(slot);

				if (area_id >= 0) {
					return area_id;
				}
			}
		}
		return -1;
#if defined(CONFIG_MCUMGR_GRP_IMG_DIRECT_UPLOAD)
	}
	/*
	 * Direct selection; the first two slots are checked for being available
	 * and unused; the all other slots are just checked for availability.
	 */
	if (slot < 2) {
		slot = img_mgmt_slot_in_use(slot) == 0 ? slot : -1;
	}

	/* Return area ID for the slot or -1 */
	return slot != -1  ? img_mgmt_flash_area_id(slot) : -1;
#endif
}
#elif CONFIG_MCUMGR_GRP_IMG_UPDATABLE_IMAGE_NUMBER >= 2
static int img_mgmt_get_unused_slot_area_id(unsigned int image)
{
	int area_id = -1;
	int slot = 0;

	slot = img_mgmt_get_opposite_slot(img_mgmt_active_slot(image));

	if (!img_mgmt_slot_in_use(slot)) {
		area_id = img_mgmt_flash_area_id(slot);
	}

	return area_id;
}
#else
#error "Unsupported number of images"
#endif

int img_mgmt_vercmp(const struct image_version *a, const struct image_version *b)
{
	struct dfu_boot_img_version va = {
		.major = a->iv_major,
		.minor = a->iv_minor,
		.revision = a->iv_revision,
		.build_num = a->iv_build_num,
	};
	struct dfu_boot_img_version vb = {
		.major = b->iv_major,
		.minor = b->iv_minor,
		.revision = b->iv_revision,
		.build_num = b->iv_build_num,
	};

	return dfu_boot_vercmp(&va, &vb);
}

int img_mgmt_erase_slot(int slot)
{
	return dfu_boot_erase_slot(slot);
}

int img_mgmt_write_pending(int slot, bool permanent)
{
	int rc;

	if (slot != 1 && !(CONFIG_MCUMGR_GRP_IMG_UPDATABLE_IMAGE_NUMBER == 2 && slot == 3)) {
		return IMG_MGMT_ERR_INVALID_SLOT;
	}

	rc = dfu_boot_set_pending(slot, permanent);
	if (rc != 0) {
		LOG_ERR("Failed to write pending flag for slot %d: %d", slot, rc);
		return IMG_MGMT_ERR_FLASH_WRITE_FAILED;
	}

	return IMG_MGMT_ERR_OK;
}

int img_mgmt_write_confirmed(void)
{
	int rc;

	rc = dfu_boot_confirm();
	if (rc != 0) {
		LOG_ERR("Failed to write confirmed flag: %d", rc);
		return IMG_MGMT_ERR_FLASH_WRITE_FAILED;
	}

	return IMG_MGMT_ERR_OK;
}

int img_mgmt_read(int slot, unsigned int offset, void *dst, unsigned int num_bytes)
{
	int rc;

	rc = dfu_boot_read(slot, offset, dst, num_bytes);
	if (rc != 0) {
		LOG_ERR("Failed to read from slot %d: %d", slot, rc);
		return IMG_MGMT_ERR_FLASH_READ_FAILED;
	}

	return 0;
}

#if defined(CONFIG_MCUMGR_GRP_IMG_USE_HEAP_FOR_FLASH_IMG_CONTEXT)
int img_mgmt_write_image_data(unsigned int offset, const void *data, unsigned int num_bytes,
			      bool last)
{
	/* Even if K_HEAP_MEM_POOL_SIZE will be able to match size of the structure,
	 * keep in mind that when application will put the heap under pressure, obtaining
	 * of a flash image context may not be possible, so plan bigger heap size or
	 * make sure to limit application pressure on heap when DFU is expected.
	 */
	BUILD_ASSERT(K_HEAP_MEM_POOL_SIZE >= (sizeof(struct flash_img_context)),
		     "Not enough heap mem for flash_img_context.");

	int rc = IMG_MGMT_ERR_OK;
	static struct flash_img_context *ctx;

	if (offset != 0 && ctx == NULL) {
		return IMG_MGMT_ERR_FLASH_CONTEXT_NOT_SET;
	}

	if (offset == 0) {
		if (ctx != NULL) {
			return IMG_MGMT_ERR_FLASH_CONTEXT_ALREADY_SET;
		}
		ctx = k_malloc(sizeof(struct flash_img_context));

		if (ctx == NULL) {
			return IMG_MGMT_ERR_NO_FREE_MEMORY;
		}

		if (flash_img_init_id(ctx, g_img_mgmt_state.area_id) != 0) {
			rc = IMG_MGMT_ERR_FLASH_OPEN_FAILED;
			goto out;
		}
	}

	if (flash_img_buffered_write(ctx, data, num_bytes, last) != 0) {
		rc = IMG_MGMT_ERR_FLASH_WRITE_FAILED;
		goto out;
	}

out:
	if (last || rc != MGMT_ERR_EOK) {
		k_free(ctx);
		ctx = NULL;
	}

	return rc;
}
#else
int img_mgmt_write_image_data(unsigned int offset, const void *data, unsigned int num_bytes,
			      bool last)
{
	static struct flash_img_context ctx;

	if (offset == 0) {
		if (flash_img_init_id(&ctx, g_img_mgmt_state.area_id) != 0) {
			return IMG_MGMT_ERR_FLASH_OPEN_FAILED;
		}
	}

	if (flash_img_buffered_write(&ctx, data, num_bytes, last) != 0) {
		return IMG_MGMT_ERR_FLASH_WRITE_FAILED;
	}

	return IMG_MGMT_ERR_OK;
}
#endif

int img_mgmt_erase_image_data(unsigned int off, unsigned int num_bytes)
{
	const struct flash_area *fa;
	int rc;
	const struct device *dev;
	struct flash_pages_info page;
	off_t page_offset;
	size_t erase_size;
	size_t img_offset;

	if (off != 0) {
		rc = IMG_MGMT_ERR_INVALID_OFFSET;
		goto end;
	}

	rc = flash_area_open(g_img_mgmt_state.area_id, &fa);
	if (rc != 0) {
		LOG_ERR("Can't bind to the flash area (err %d)", rc);
		rc = IMG_MGMT_ERR_FLASH_OPEN_FAILED;
		goto end;
	}

	/* Align requested erase size to the erase-block-size */
	dev = flash_area_get_device(fa);

	if (dev == NULL) {
		rc = IMG_MGMT_ERR_FLASH_AREA_DEVICE_NULL;
		goto end_fa;
	}

	page_offset = fa->fa_off + num_bytes - 1;
	rc = flash_get_page_info_by_offs(dev, page_offset, &page);
	if (rc != 0) {
		LOG_ERR("bad offset (0x%lx)", (long)page_offset);
		rc = IMG_MGMT_ERR_INVALID_PAGE_OFFSET;
		goto end_fa;
	}

	erase_size = page.start_offset + page.size - fa->fa_off;

	/* Get image start offset for swap-using-offset mode */
	img_offset = dfu_boot_get_image_offset(
		img_mgmt_slot_to_image(g_img_mgmt_state.area_id) * 2 + 1);

	rc = flash_area_flatten(fa, img_offset, erase_size);

	if (rc != 0) {
		LOG_ERR("image slot erase of 0x%zx bytes failed (err %d)", erase_size,
				rc);
		rc = IMG_MGMT_ERR_FLASH_ERASE_FAILED;
		goto end_fa;
	}

	LOG_INF("Erased 0x%zx bytes of image slot", erase_size);

#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	/* Right now MCUmgr supports only mcuboot images.
	 * Above compilation swich might help to recognize mcuboot related
	 * code when supports for anothe bootloader will be introduced.
	 */

	/* erase the image trailer area if it was not erased */
	off = dfu_boot_get_trailer_status_offset(fa->fa_size);
	if (off >= erase_size) {
		rc = flash_get_page_info_by_offs(dev, fa->fa_off + off, &page);

		off = page.start_offset - fa->fa_off;
		erase_size = fa->fa_size - off;

		rc = flash_area_flatten(fa, off, erase_size);
		if (rc != 0) {
			LOG_ERR("image slot trailer erase of 0x%zx bytes failed (err %d)",
					erase_size, rc);
			rc = IMG_MGMT_ERR_FLASH_ERASE_FAILED;
			goto end_fa;
		}

		LOG_INF("Erased 0x%zx bytes of image slot trailer", erase_size);
	}
#endif
	rc = IMG_MGMT_ERR_OK;

end_fa:
	flash_area_close(fa);
end:
	return rc;
}

int img_mgmt_swap_type(int slot)
{
	return dfu_boot_get_swap_type(slot);
}

/**
 * Verifies an upload request and indicates the actions that should be taken
 * during processing of the request.  This is a "read only" function in the
 * sense that it doesn't write anything to flash and doesn't modify any global
 * variables.
 *
 * @param req		The upload request to inspect.
 * @param action	On success, gets populated with information about how to process
 *			the request.
 *
 * @return 0 if processing should occur; A MGMT_ERR code if an error response should be sent
 *	   instead.
 */
int img_mgmt_upload_inspect(const struct img_mgmt_upload_req *req,
			    struct img_mgmt_upload_action *action)
{
	struct dfu_boot_img_info hdr_info;
	struct image_version cur_ver;
	int rc;

	memset(action, 0, sizeof(*action));

	if (req->off == SIZE_MAX) {
		/* Request did not include an `off` field. */
		IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, img_mgmt_err_str_hdr_malformed);
		LOG_DBG("Request did not include an `off` field");
		return IMG_MGMT_ERR_INVALID_OFFSET;
	}

	if (req->off == 0) {
		/* First upload chunk. */
		const struct flash_area *fa;
#if defined(CONFIG_MCUMGR_GRP_IMG_TOO_LARGE_SYSBUILD) &&			\
	(defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_WITHOUT_SCRATCH) ||	\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_MOVE) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_SCRATCH) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_OVERWRITE_ONLY) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) ||			\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP) ||			\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT)) &&	\
	CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE > 0
		const struct flash_area *fa_current;
		int current_img_area;
#elif defined(CONFIG_MCUMGR_GRP_IMG_TOO_LARGE_BOOTLOADER_INFO)
		int max_image_size;
#endif

		if (req->img_data.len < sizeof(struct dfu_boot_img_header)) {
			/*  Image header is the first thing in the image */
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, img_mgmt_err_str_hdr_malformed);
			LOG_DBG("Image data too short: %u < %zu", req->img_data.len,
				sizeof(struct dfu_boot_img_header));
			return IMG_MGMT_ERR_INVALID_IMAGE_HEADER;
		}

		if (req->size == SIZE_MAX) {
			/* Request did not include a `len` field. */
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, img_mgmt_err_str_hdr_malformed);
			LOG_DBG("Request did not include a `len` field");
			return IMG_MGMT_ERR_INVALID_LENGTH;
		}

		action->size = req->size;

		/* Validate header magic using dfu_boot API */
		rc = dfu_boot_validate_header(req->img_data.value, req->img_data.len, &hdr_info);
		if (rc != 0) {
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, img_mgmt_err_str_magic_mismatch);
			LOG_DBG("Header validation failed: %d", rc);
			return IMG_MGMT_ERR_INVALID_IMAGE_HEADER_MAGIC;
		}

		if (req->data_sha.len > IMG_MGMT_DATA_SHA_LEN) {
			LOG_DBG("Invalid hash length: %u", req->data_sha.len);
			return IMG_MGMT_ERR_INVALID_HASH;
		}

		/*
		 * If request includes proper data hash we can check whether there is
		 * upload in progress (interrupted due to e.g. link disconnection) with
		 * the same data hash so we can just resume it by simply including
		 * current upload offset in response.
		 */
		if ((req->data_sha.len > 0) && (g_img_mgmt_state.area_id != -1)) {
			if ((g_img_mgmt_state.data_sha_len == req->data_sha.len) &&
			    !memcmp(g_img_mgmt_state.data_sha, req->data_sha.value,
				    req->data_sha.len)) {
				return IMG_MGMT_ERR_OK;
			}
		}

		action->area_id = img_mgmt_get_unused_slot_area_id(req->image);
		if (action->area_id < 0) {
			/* No slot available to upload to */
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, img_mgmt_err_str_no_slot);
			LOG_DBG("No slot available to upload to");
			return IMG_MGMT_ERR_NO_FREE_SLOT;
		}

		rc = flash_area_open(action->area_id, &fa);
		if (rc) {
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
				img_mgmt_err_str_flash_open_failed);
			LOG_ERR("Failed to open flash area ID %u: %d", action->area_id, rc);
			return IMG_MGMT_ERR_FLASH_OPEN_FAILED;
		}

		/* Check that the area is of sufficient size to store the new image */
		if (req->size > fa->fa_size) {
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
				img_mgmt_err_str_image_too_large);
			flash_area_close(fa);
			LOG_DBG("Upload too large for slot: %u > %u", req->size,
				fa->fa_size);
			return IMG_MGMT_ERR_INVALID_IMAGE_TOO_LARGE;
		}

#if defined(CONFIG_MCUMGR_GRP_IMG_TOO_LARGE_SYSBUILD) &&			\
	(defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_WITHOUT_SCRATCH) ||	\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_MOVE) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_SCRATCH) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_OVERWRITE_ONLY) ||		\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) ||			\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP) ||			\
	 defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT)) &&	\
	CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE > 0
		/* Check if slot1 is larger than slot0 by the update size, if so then the size
		 * check can be skipped because the devicetree partitions are okay
		 */
		current_img_area = img_mgmt_flash_area_id(req->image);

		if (current_img_area < 0) {
			/* Current slot cannot be determined */
			LOG_ERR("Failed to determine active slot for image %d: %d", req->image,
				current_img_area);
			return IMG_MGMT_ERR_ACTIVE_SLOT_NOT_KNOWN;
		}

		rc = flash_area_open(current_img_area, &fa_current);
		if (rc) {
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
				img_mgmt_err_str_flash_open_failed);
			LOG_ERR("Failed to open flash area ID %u: %d", current_img_area, rc);
			flash_area_close(fa);
			return IMG_MGMT_ERR_FLASH_OPEN_FAILED;
		}

		flash_area_close(fa_current);

		LOG_DBG("Primary size: %d, secondary size: %d, overhead: %d, max update size: %d",
			fa_current->fa_size, fa->fa_size, CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE,
			(fa->fa_size + CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE));

		if (fa_current->fa_size >= (fa->fa_size + CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE)) {
			/* Upgrade slot is of sufficient size, nothing to check */
			LOG_INF("Upgrade slots already sized appropriately, "
				"CONFIG_MCUMGR_GRP_IMG_TOO_LARGE_SYSBUILD is not needed");
			goto skip_size_check;
		}

		if (req->size > (fa->fa_size - CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE)) {
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
				img_mgmt_err_str_image_too_large);
			flash_area_close(fa);
			LOG_DBG("Upload too large for slot (with end offset): %u > %u", req->size,
				(fa->fa_size - CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE));
			return IMG_MGMT_ERR_INVALID_IMAGE_TOO_LARGE;
		}

skip_size_check:
#elif defined(CONFIG_MCUMGR_GRP_IMG_TOO_LARGE_BOOTLOADER_INFO)
		rc = blinfo_lookup(BLINFO_MAX_APPLICATION_SIZE, (char *)&max_image_size,
				   sizeof(max_image_size));

		if (rc == sizeof(max_image_size) && max_image_size > 0 &&
		    req->size > max_image_size) {
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
				img_mgmt_err_str_image_too_large);
			flash_area_close(fa);
			LOG_DBG("Upload too large for slot (with max image size): %u > %u",
				req->size, max_image_size);
			return IMG_MGMT_ERR_INVALID_IMAGE_TOO_LARGE;
		}
#endif

#if defined(CONFIG_MCUMGR_GRP_IMG_REJECT_DIRECT_XIP_MISMATCHED_SLOT)
		if (hdr_info.flags & DFU_BOOT_IMG_F_ROM_FIXED) {
			if (fa->fa_off != hdr_info.load_addr) {
				IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
					img_mgmt_err_str_image_bad_flash_addr);
				flash_area_close(fa);
				LOG_DBG("Invalid flash address: %08X, expected: %08X",
					hdr_info.load_addr, (int)fa->fa_off);
				return IMG_MGMT_ERR_INVALID_FLASH_ADDRESS;
			}
		}
#endif

		flash_area_close(fa);

		if (req->upgrade) {
			/* User specified upgrade-only. Make sure new image version is
			 * greater than that of the currently running image.
			 */
			rc = img_mgmt_my_version(&cur_ver);
			if (rc != 0) {
				LOG_DBG("Version get failed: %d", rc);
				return IMG_MGMT_ERR_VERSION_GET_FAILED;
			}

			struct image_version hdr_ver = {
				.iv_major = hdr_info.version.major,
				.iv_minor = hdr_info.version.minor,
				.iv_revision = hdr_info.version.revision,
				.iv_build_num = hdr_info.version.build_num,
			};

			if (img_mgmt_vercmp(&cur_ver, &hdr_ver) >= 0) {
				IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action,
					img_mgmt_err_str_downgrade);
				LOG_DBG("Downgrade: %d.%d.%d.%d, expected: %d.%d.%d.%d",
					cur_ver.iv_major, cur_ver.iv_minor, cur_ver.iv_revision,
					cur_ver.iv_build_num, hdr_ver.iv_major,
					hdr_ver.iv_minor, hdr_ver.iv_revision,
					hdr_ver.iv_build_num);
				return IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER;
			}
		}

#ifndef CONFIG_IMG_ERASE_PROGRESSIVELY
		rc = img_mgmt_flash_check_empty(action->area_id);
		if (rc < 0) {
			LOG_DBG("Flash check empty failed: %d", rc);
			return rc;
		}

		action->erase = (rc == 0);
#endif
	} else {
		/* Continuation of upload. */
		action->area_id = g_img_mgmt_state.area_id;
		action->size = g_img_mgmt_state.size;

		if (req->off != g_img_mgmt_state.off) {
			/*
			 * Invalid offset. Drop the data, and respond with the offset we're
			 * expecting data for.
			 */
			LOG_DBG("Invalid offset: %08x, expected: %08x", req->off,
				g_img_mgmt_state.off);
			return IMG_MGMT_ERR_OK;
		}

		if ((req->off + req->img_data.len) > action->size) {
			/* Data overrun, the amount of data written would be more than the size
			 * of the image that the client originally sent
			 */
			IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, img_mgmt_err_str_data_overrun);
			LOG_DBG("Data overrun: %u + %u > %llu", req->off, req->img_data.len,
				action->size);
			return IMG_MGMT_ERR_INVALID_IMAGE_DATA_OVERRUN;
		}
	}

	action->write_bytes = req->img_data.len;
	action->proceed = true;
	IMG_MGMT_UPLOAD_ACTION_SET_RC_RSN(action, NULL);

	return IMG_MGMT_ERR_OK;
}

int img_mgmt_erased_val(int slot, uint8_t *erased_val)
{
	return dfu_boot_get_erased_val(slot, erased_val);
}
