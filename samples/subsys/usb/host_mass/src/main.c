/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if CONFIG_FAT_FILESYSTEM_ELM
#include <ff.h>
#endif

/* USB Host Controller definition */
USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

#define USB_DISK_NAME "USB_MSC"
#define USB_MOUNT_POINT "/USB:"
#define MAX_RETRY_COUNT 3
#define DETECTION_TIMEOUT_MS 5000

/* Application state */
enum app_state {
	APP_STATE_INIT,
	APP_STATE_WAITING_DEVICE,
	APP_STATE_DEVICE_DETECTED,
	APP_STATE_DEVICE_READY,
	APP_STATE_FILESYSTEM_MOUNTED,
	APP_STATE_ERROR
};

struct app_context {
	enum app_state state;
	struct fs_mount_t usb_mount;
	bool device_mounted;
	struct k_timer detection_timer;
	struct k_work_delayable retry_work;
	int retry_count;
};

static struct app_context app_ctx;

/* Forward declarations */
static void detection_timer_handler(struct k_timer *timer);
static void retry_work_handler(struct k_work *work);

static void app_state_change(enum app_state new_state)
{
	enum app_state old_state = app_ctx.state;
	app_ctx.state = new_state;
	
	LOG_INF("State change: %d -> %d", old_state, new_state);
}

static void show_disk_info(const char *disk_name)
{
	uint32_t sector_count = 0;
	uint32_t sector_size = 0;
	uint64_t total_size;
	int ret;

	ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (ret != 0) {
		LOG_ERR("Failed to get sector count: %d", ret);
		return;
	}

	ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret != 0) {
		LOG_ERR("Failed to get sector size: %d", ret);
		return;
	}

	total_size = (uint64_t)sector_count * sector_size;
	LOG_INF("=== USB Disk Information ===");
	LOG_INF("Disk Name:    %s", disk_name);
	LOG_INF("Sector Count: %u", sector_count);
	LOG_INF("Sector Size:  %u bytes", sector_size);
	LOG_INF("Total Size:   %llu bytes (%.2f MB)",
		total_size, (double)total_size / (1024 * 1024));
}

static int test_disk_performance(const char *disk_name)
{
	uint8_t *test_buffer;
	uint32_t test_sectors = 8;  /* Test 4KB */
	int64_t start_time, end_time;
	int ret;

	test_buffer = k_malloc(test_sectors * 512);
	if (!test_buffer) {
		LOG_ERR("Failed to allocate test buffer");
		return -ENOMEM;
	}

	LOG_INF("=== Disk Performance Test ===");

	/* Read performance test */
	start_time = k_uptime_get();
	ret = disk_access_read(disk_name, test_buffer, 0, test_sectors);
	end_time = k_uptime_get();

	if (ret == 0) {
		int64_t duration = end_time - start_time;
		double speed = (test_sectors * 512.0) / (duration / 1000.0) / 1024.0; /* KB/s */
		LOG_INF("Read Speed: %.2f KB/s (%lld ms for %u sectors)",
			speed, duration, test_sectors);
	} else {
		LOG_ERR("Read performance test failed: %d", ret);
	}

	k_free(test_buffer);
	return ret;
}

static void show_filesystem_info(void)
{
	struct fs_statvfs stat;
	struct fs_dir_t dir;
	struct fs_dirent entry;
	int ret;
	int file_count = 0;

	ret = fs_statvfs(USB_MOUNT_POINT, &stat);
	if (ret == 0) {
		LOG_INF("=== Filesystem Information ===");
		LOG_INF("Block Size:   %lu bytes", stat.f_bsize);
		LOG_INF("Total Blocks: %lu", stat.f_blocks);
		LOG_INF("Free Blocks:  %lu", stat.f_bfree);
		LOG_INF("Used:         %.1f%%",
			100.0 * (stat.f_blocks - stat.f_bfree) / stat.f_blocks);
	}

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, USB_MOUNT_POINT);
	if (ret < 0) {
		LOG_ERR("Failed to open directory: %d", ret);
		return;
	}

	LOG_INF("=== Directory Contents ===");
	while (true) {
		ret = fs_readdir(&dir, &entry);
		if (ret < 0 || entry.name[0] == 0) {
			break;
		}

		LOG_INF("  %c %8u %s",
			(entry.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
			entry.size, entry.name);
		file_count++;
	}

	fs_closedir(&dir);
	LOG_INF("Total entries: %d", file_count);
}

static int mount_usb_disk(const char *disk_name)
{
#if CONFIG_FAT_FILESYSTEM_ELM
	static FATFS fat_fs;
	int ret;

	if (app_ctx.device_mounted) {
		LOG_WRN("Device already mounted");
		return 0;
	}

	memset(&app_ctx.usb_mount, 0, sizeof(app_ctx.usb_mount));
	app_ctx.usb_mount.type = FS_FATFS;
	app_ctx.usb_mount.fs_data = &fat_fs;
	app_ctx.usb_mount.mnt_point = USB_MOUNT_POINT;
	app_ctx.usb_mount.storage_dev = disk_name;

	ret = fs_mount(&app_ctx.usb_mount);
	if (ret < 0) {
		LOG_ERR("Failed to mount USB disk: %d", ret);
		return ret;
	}

	LOG_INF("USB disk mounted at %s", USB_MOUNT_POINT);
	app_ctx.device_mounted = true;
	app_state_change(APP_STATE_FILESYSTEM_MOUNTED);
	return 0;
#else
	LOG_ERR("No filesystem support enabled");
	return -ENOTSUP;
#endif
}

static void unmount_usb_disk(void)
{
	if (app_ctx.device_mounted) {
		int ret = fs_unmount(&app_ctx.usb_mount);
		if (ret < 0) {
			LOG_ERR("Failed to unmount USB disk: %d", ret);
		} else {
			LOG_INF("USB disk unmounted");
		}
		app_ctx.device_mounted = false;
	}
}

static int initialize_disk_with_retry(const char *disk_name)
{
	int ret;
	int attempts = 0;

	while (attempts < MAX_RETRY_COUNT) {
		ret = disk_access_init(disk_name);
		if (ret == 0) {
			LOG_INF("Disk initialized successfully on attempt %d", attempts + 1);
			break;
		}

		attempts++;
		LOG_WRN("Disk init attempt %d failed: %d", attempts, ret);
		
		if (attempts < MAX_RETRY_COUNT) {
			k_sleep(K_MSEC(100 * attempts));  /* Exponential backoff */
		}
	}

	return ret;
}

static void handle_disk_connected(const char *disk_name)
{
	int ret;

	LOG_INF("USB Mass Storage disk connected: %s", disk_name);
	app_state_change(APP_STATE_DEVICE_DETECTED);

	/* Stop detection timer */
	k_timer_stop(&app_ctx.detection_timer);

	/* Initialize disk with retry */
	ret = initialize_disk_with_retry(disk_name);
	if (ret < 0) {
		LOG_ERR("Failed to initialize disk after retries: %d", ret);
		app_state_change(APP_STATE_ERROR);
		return;
	}

	/* Check disk status */
	ret = disk_access_status(disk_name);
	if (ret != DISK_STATUS_OK) {
		LOG_ERR("Disk not ready, status: %d", ret);
		app_state_change(APP_STATE_ERROR);
		return;
	}

	app_state_change(APP_STATE_DEVICE_READY);

	/* Show disk information */
	show_disk_info(disk_name);

	/* Test disk performance */
	test_disk_performance(disk_name);

	/* Mount filesystem */
	ret = mount_usb_disk(disk_name);
	if (ret == 0) {
		show_filesystem_info();
	} else {
		LOG_ERR("Failed to mount filesystem, but disk is accessible");
	}
}

static void handle_disk_disconnected(const char *disk_name)
{
	LOG_INF("USB Mass Storage disk disconnected: %s", disk_name);

	/* Cancel any pending retry work */
	k_work_cancel_delayable(&app_ctx.retry_work);

	unmount_usb_disk();

	/* Deinitialize disk */
	bool force_deinit = true;
	disk_access_ioctl(disk_name, DISK_IOCTL_CTRL_DEINIT, &force_deinit);

	app_ctx.retry_count = 0;
	app_state_change(APP_STATE_WAITING_DEVICE);

	/* Restart detection timer */
	k_timer_start(&app_ctx.detection_timer, 
		      K_MSEC(DETECTION_TIMEOUT_MS), K_NO_WAIT);
}

static void detection_timer_handler(struct k_timer *timer)
{
	LOG_WRN("Device detection timeout - no USB storage device found");
	/* Could implement periodic detection retry here */
}

static void retry_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	
	if (app_ctx.retry_count < MAX_RETRY_COUNT) {
		app_ctx.retry_count++;
		LOG_INF("Retry attempt %d/%d", app_ctx.retry_count, MAX_RETRY_COUNT);
		
		/* Could trigger re-enumeration or other recovery actions here */
	} else {
		LOG_ERR("Max retry attempts reached");
		app_state_change(APP_STATE_ERROR);
	}
}

int main(void)
{
	struct k_poll_signal sig;
	struct k_poll_event evt[1];
	k_timeout_t timeout = K_FOREVER;
	int err;
	int signaled, result;

	LOG_INF("USB Host Mass Storage Sample - Enhanced Version");

	/* Initialize application context */
	memset(&app_ctx, 0, sizeof(app_ctx));
	app_state_change(APP_STATE_INIT);

	/* Initialize timers and work items */
	k_timer_init(&app_ctx.detection_timer, detection_timer_handler, NULL);
	k_work_init_delayable(&app_ctx.retry_work, retry_work_handler);

	/* Initialize USB Host */
	err = usbh_init(&uhs_ctx);
	if (err) {
		LOG_ERR("Failed to initialize USB host support: %d", err);
		return err;
	}

	err = usbh_enable(&uhs_ctx);
	if (err) {
		LOG_ERR("Failed to enable USB host support: %d", err);
		return err;
	}

	/* Setup polling for disk events */
	k_poll_signal_init(&sig);
	k_poll_event_init(&evt[0], K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

	err = disk_access_ioctl(USB_DISK_NAME, DISK_IOCTL_SET_SIGNAL, &sig);
	if (err != 0) {
		LOG_WRN("Failed to setup signal for %s: %d", USB_DISK_NAME, err);
		timeout = K_MSEC(1000);
	}

	app_state_change(APP_STATE_WAITING_DEVICE);
	
	/* Start detection timer */
	k_timer_start(&app_ctx.detection_timer, 
		      K_MSEC(DETECTION_TIMEOUT_MS), K_NO_WAIT);

	LOG_INF("Waiting for USB Mass Storage device...");

	while (true) {
		err = k_poll(evt, ARRAY_SIZE(evt), timeout);
		if (err != 0 && err != -EAGAIN) {
			LOG_WRN("Poll failed with error %d, retrying...", err);
			continue;
		}

		k_poll_signal_check(&sig, &signaled, &result);
		if (!signaled) {
			continue;
		}

		k_poll_signal_reset(&sig);

		switch (result) {
		case USBH_DEVICE_CONNECTED:
			handle_disk_connected(USB_DISK_NAME);
			break;

		case USBH_DEVICE_DISCONNECTED:
			handle_disk_disconnected(USB_DISK_NAME);
			break;

		default:
			LOG_DBG("Received signal: %d", result);
			break;
		}
	}

	return 0;
}
