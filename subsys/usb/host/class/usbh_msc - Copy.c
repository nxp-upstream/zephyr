/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_msc_host

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/logging/log.h>

#include "usbh_msc_scsi.h"

#include "usbh_device.h"
#include "usbh_ch9.h"
#include "usbh_msc.h"


LOG_MODULE_REGISTER(usbh_msc, CONFIG_USBH_MSC_CLASS_LOG_LEVEL);

/* USB Mass Storage Class codes */
#define USB_CLASS_MASS_STORAGE    0x08
#define USB_SUBCLASS_SCSI         0x06
#define USB_PROTOCOL_BOT          0x50

/* BOT Protocol */
#define CBW_SIGNATURE             0x43425355
#define CSW_SIGNATURE             0x53425355
#define CBW_FLAGS_DATA_IN         0x80
#define CBW_FLAGS_DATA_OUT        0x00

/* Command Status */
#define CSW_STATUS_PASSED         0x00
#define CSW_STATUS_FAILED         0x01
#define CSW_STATUS_PHASE_ERROR    0x02

/* Transfer parameters */
#define USB_MSC_TIMEOUT_MS        5000
#define MAX_RETRY_COUNT           3

/* Device states */
enum msc_device_state {
	MSC_STATE_DISCONNECTED,
	MSC_STATE_CONNECTED,
	MSC_STATE_INITIALIZING,
	MSC_STATE_READY,
	MSC_STATE_ERROR
};

/* MSC device matching table */
static const struct usbh_device_code_table msc_device_code[] = {
	{
		.match_type = USBH_MATCH_INTFACE,
		.interface_class_code = USB_CLASS_MASS_STORAGE,
		.interface_subclass_code = USB_SUBCLASS_SCSI,
		.interface_protocol_code = USB_PROTOCOL_BOT,
	}
};

/* Command Block Wrapper (CBW) */
struct cbw {
	uint32_t dCBWSignature;
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
} __packed;

/* Command Status Wrapper (CSW) */
struct csw {
	uint32_t dCSWSignature;
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
} __packed;

/* MSC device state */
struct usbh_msc_data {
	struct usb_device *udev;
	struct disk_info disk_info;
	struct k_mutex lock;
	struct k_poll_signal *signal;
	
	/* USB endpoints */
	uint8_t bulk_in_ep;
	uint8_t bulk_out_ep;
	uint16_t max_packet_size;
	
	/* SCSI context */
	struct scsi_context scsi;
	
	/* Device state */
	enum msc_device_state state;
	bool initialized;
	
	/* Transfer tracking */
	uint32_t tag_counter;
	
	/* Statistics for debugging */
	struct {
		uint32_t read_count;
		uint32_t write_count;
		uint32_t error_count;
		uint32_t retry_count;
	} stats;
};

static struct usbh_msc_data msc_data = {
	.disk_info = {
		.name = "USB_MSC",
	},
	.state = MSC_STATE_DISCONNECTED,
};

/* Forward declarations */
static int msc_disk_init(struct disk_info *disk);
static int msc_disk_status(struct disk_info *disk);
static int msc_disk_read(struct disk_info *disk, uint8_t *data_buf,
			 uint32_t start_sector, uint32_t num_sectors);
static int msc_disk_write(struct disk_info *disk, const uint8_t *data_buf,
			  uint32_t start_sector, uint32_t num_sectors);
static int msc_disk_ioctl(struct disk_info *disk, uint8_t cmd, void *buff);

/* Disk operations */
static const struct disk_operations msc_disk_ops = {
	.init = msc_disk_init,
	.status = msc_disk_status,
	.read = msc_disk_read,
	.write = msc_disk_write,
	.ioctl = msc_disk_ioctl,
};

static void msc_state_change(enum msc_device_state new_state)
{
	if (msc_data.state != new_state) {
		LOG_DBG("MSC state: %d -> %d", msc_data.state, new_state);
		msc_data.state = new_state;
	}
}

/**
 * @brief Enhanced BOT transfer with retry mechanism
 */
static int msc_bot_transfer(struct usbh_msc_data *msc, uint8_t *cmd, uint8_t cmd_len,
			   uint8_t *data, uint32_t data_len, bool data_in)
{
	struct cbw cbw = {0};
	struct csw csw = {0};
	struct net_buf *buf = NULL;
	int ret;

	if (!msc || !msc->udev || !cmd) {
		return -EINVAL;
	}

	if (msc->state != MSC_STATE_READY && msc->state != MSC_STATE_INITIALIZING) {
		LOG_ERR("Device not ready for transfer, state: %d", msc->state);
		return -ENODEV;
	}

	/* Prepare CBW */
	cbw.dCBWSignature = sys_cpu_to_le32(CBW_SIGNATURE);
	cbw.dCBWTag = sys_cpu_to_le32(++msc->tag_counter);
	cbw.dCBWDataTransferLength = sys_cpu_to_le32(data_len);
	cbw.bmCBWFlags = data_in ? CBW_FLAGS_DATA_IN : CBW_FLAGS_DATA_OUT;
	cbw.bCBWLUN = 0;
	cbw.bCBWCBLength = cmd_len;
	memcpy(cbw.CBWCB, cmd, cmd_len);

	LOG_DBG("BOT Transfer: cmd=0x%02x, len=%u, dir=%s", 
		cmd[0], data_len, data_in ? "IN" : "OUT");

	/* Send CBW */
	buf = usbh_xfer_buf_alloc(msc->udev, sizeof(cbw));
	if (!buf) {
		return -ENOMEM;
	}

	memcpy(buf->data, &cbw, sizeof(cbw));
	net_buf_add(buf, sizeof(cbw));

	ret = usbh_ep_enqueue(msc->udev, buf, msc->bulk_out_ep);
	if (ret) {
		LOG_ERR("Failed to send CBW: %d", ret);
		usbh_xfer_buf_free(msc->udev, buf);
		return ret;
	}

	/* Handle data phase if present */
	if (data && data_len > 0) {
		buf = usbh_xfer_buf_alloc(msc->udev, data_len);
		if (!buf) {
			return -ENOMEM;
		}

		if (!data_in) {
			/* Data OUT phase */
			memcpy(buf->data, data, data_len);
			net_buf_add(buf, data_len);
			ret = usbh_ep_enqueue(msc->udev, buf, msc->bulk_out_ep);
		} else {
			/* Data IN phase */
			ret = usbh_ep_enqueue(msc->udev, buf, msc->bulk_in_ep);
			if (ret == 0 && buf->len > 0) {
				size_t copy_len = MIN(buf->len, data_len);
				memcpy(data, buf->data, copy_len);
			}
		}

		if (ret) {
			LOG_ERR("Data phase failed: %d", ret);
			usbh_xfer_buf_free(msc->udev, buf);
			return ret;
		}
	}

	/* Receive CSW */
	buf = usbh_xfer_buf_alloc(msc->udev, sizeof(csw));
	if (!buf) {
		return -ENOMEM;
	}

	ret = usbh_ep_enqueue(msc->udev, buf, msc->bulk_in_ep);
	if (ret) {
		LOG_ERR("Failed to receive CSW: %d", ret);
		usbh_xfer_buf_free(msc->udev, buf);
		return ret;
	}

	if (buf->len >= sizeof(csw)) {
		memcpy(&csw, buf->data, sizeof(csw));
		
		if (sys_le32_to_cpu(csw.dCSWSignature) != CSW_SIGNATURE) {
			LOG_ERR("Invalid CSW signature: 0x%08x", 
				sys_le32_to_cpu(csw.dCSWSignature));
			ret = -EIO;
		} else if (sys_le32_to_cpu(csw.dCSWTag) != msc->tag_counter) {
			LOG_ERR("CSW tag mismatch");
			ret = -EIO;
		} else if (csw.bCSWStatus != CSW_STATUS_PASSED) {
			LOG_WRN("SCSI command failed, status: %d", csw.bCSWStatus);
			ret = -EIO;
		}
	} else {
		LOG_ERR("Invalid CSW length: %u", buf->len);
		ret = -EIO;
	}

	usbh_xfer_buf_free(msc->udev, buf);
	return ret;
}

/**
 * @brief BOT transfer with retry and error handling
 */
static int msc_bot_transfer_with_retry(struct usbh_msc_data *msc, 
				      uint8_t *cmd, uint8_t cmd_len,
				      uint8_t *data, uint32_t data_len, 
				      bool data_in)
{
	int ret = -EIO;
	int retry_count = 0;

	while (retry_count < MAX_RETRY_COUNT) {
		ret = msc_bot_transfer(msc, cmd, cmd_len, data, data_len, data_in);
		
		if (ret == 0) {
			if (retry_count > 0) {
				msc->stats.retry_count++;
				LOG_DBG("Transfer succeeded on retry %d", retry_count);
			}
			return 0;
		}

		retry_count++;
		msc->stats.error_count++;
		
		if (retry_count < MAX_RETRY_COUNT) {
			LOG_WRN("Transfer failed (attempt %d/%d): %d", 
				retry_count, MAX_RETRY_COUNT, ret);
			k_sleep(K_MSEC(10 << retry_count));  /* Exponential backoff */
		}
	}

	LOG_ERR("Transfer failed after %d retries", MAX_RETRY_COUNT);
	return ret;
}

/**
 * @brief SCSI command execution callback
 */
static int msc_scsi_command_exec(void *user_data, const uint8_t *cdb, uint8_t cdb_len,
                                uint8_t *data, uint32_t data_len, bool data_in)
{
	struct usbh_msc_data *msc = (struct usbh_msc_data *)user_data;
	return msc_bot_transfer_with_retry(msc, (uint8_t *)cdb, cdb_len, 
					  data, data_len, data_in);
}

/**
 * @brief Parse interface descriptors to find bulk endpoints
 */
static int msc_parse_endpoints(struct usbh_msc_data *msc, 
			       struct usb_if_descriptor *if_desc)
{
	struct usb_ep_descriptor *ep_desc;
	uint8_t *desc_buf = (uint8_t *)if_desc + if_desc->bLength;
	bool found_in = false, found_out = false;

	for (int i = 0; i < if_desc->bNumEndpoints; i++) {
		ep_desc = (struct usb_ep_descriptor *)desc_buf;
		
		if (ep_desc->bDescriptorType != USB_DESC_ENDPOINT) {
			break;
		}

		if ((ep_desc->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) == USB_EP_TYPE_BULK) {
			if (ep_desc->bEndpointAddress & USB_EP_DIR_IN) {
				msc->bulk_in_ep = ep_desc->bEndpointAddress;
				found_in = true;
			} else {
				msc->bulk_out_ep = ep_desc->bEndpointAddress;
				found_out = true;
			}
			msc->max_packet_size = sys_le16_to_cpu(ep_desc->wMaxPacketSize);
		}

		desc_buf += ep_desc->bLength;
	}

	if (found_in && found_out) {
		LOG_DBG("Found endpoints: IN=0x%02x, OUT=0x%02x, MPS=%u",
			msc->bulk_in_ep, msc->bulk_out_ep, msc->max_packet_size);
		return 0;
	}

	LOG_ERR("Missing bulk endpoints");
	return -ENODEV;
}

/**
 * @brief Initialize MSC device
 */
static int msc_device_init(struct usbh_msc_data *msc)
{
	int ret;

	LOG_INF("Initializing MSC device");
	msc_state_change(MSC_STATE_INITIALIZING);

	scsi_init(&msc->scsi);

	ret = scsi_device_init_sequence(&msc->scsi, msc_scsi_command_exec, msc);
	if (ret) {
		LOG_ERR("SCSI device initialization failed: %d", ret);
		msc_state_change(MSC_STATE_ERROR);
		return ret;
	}

	/* Reset statistics */
	memset(&msc->stats, 0, sizeof(msc->stats));

	msc->initialized = true;
	msc_state_change(MSC_STATE_READY);
	LOG_INF("MSC device initialized successfully");
	return 0;
}

/* Disk subsystem interface implementation */

static int msc_disk_init(struct disk_info *disk)
{
	struct usbh_msc_data *msc = CONTAINER_OF(disk, struct usbh_msc_data, disk_info);
	int ret;

	k_mutex_lock(&msc->lock, K_FOREVER);

	if (msc->state == MSC_STATE_DISCONNECTED) {
		ret = -ENODEV;
		goto unlock;
	}

	if (msc->initialized) {
		ret = 0;
		goto unlock;
	}

	ret = msc_device_init(msc);

unlock:
	k_mutex_unlock(&msc->lock);
	return ret;
}

static int msc_disk_status(struct disk_info *disk)
{
	struct usbh_msc_data *msc = CONTAINER_OF(disk, struct usbh_msc_data, disk_info);

	switch (msc->state) {
	case MSC_STATE_DISCONNECTED:
		return DISK_STATUS_NOMEDIA;
	case MSC_STATE_CONNECTED:
	case MSC_STATE_INITIALIZING:
		return DISK_STATUS_UNINIT;
	case MSC_STATE_READY:
		return DISK_STATUS_OK;
	case MSC_STATE_ERROR:
	default:
		return DISK_STATUS_UNINIT;
	}
}

static int msc_disk_read(struct disk_info *disk, uint8_t *data_buf,
			 uint32_t start_sector, uint32_t num_sectors)
{
	struct usbh_msc_data *msc = CONTAINER_OF(disk, struct usbh_msc_data, disk_info);
	uint8_t cmd[10];
	uint32_t remaining_sectors = num_sectors;
	uint32_t current_lba = start_sector;
	uint8_t *current_buf = data_buf;
	int ret = 0;

	if (!msc->initialized || msc->state != MSC_STATE_READY) {
		return -ENODEV;
	}

	LOG_DBG("Read: LBA=%u, sectors=%u", start_sector, num_sectors);

	k_mutex_lock(&msc->lock, K_FOREVER);

	while (remaining_sectors > 0) {
		uint16_t transfer_sectors = scsi_calc_optimal_transfer_blocks(
			&msc->scsi, remaining_sectors);

		ret = scsi_validate_rw_params(&msc->scsi, current_lba, transfer_sectors);
		if (ret) {
			break;
		}

		scsi_build_read_10(cmd, current_lba, transfer_sectors);
		uint32_t transfer_bytes = transfer_sectors * msc->scsi.block_size;

		ret = msc_scsi_command_exec(msc, cmd, 10, current_buf, transfer_bytes, true);
		if (ret < 0) {
			LOG_ERR("Read failed: LBA=%u, sectors=%u, error=%d", 
				current_lba, transfer_sectors, ret);
			break;
		}

		current_lba += transfer_sectors;
		current_buf += transfer_bytes;
		remaining_sectors -= transfer_sectors;
		msc->stats.read_count++;
	}

	k_mutex_unlock(&msc->lock);
	return ret;
}

static int msc_disk_write(struct disk_info *disk, const uint8_t *data_buf,
			  uint32_t start_sector, uint32_t num_sectors)
{
	struct usbh_msc_data *msc = CONTAINER_OF(disk, struct usbh_msc_data, disk_info);
	uint8_t cmd[10];
	uint32_t remaining_sectors = num_sectors;
	uint32_t current_lba = start_sector;
	const uint8_t *current_buf = data_buf;
	int ret = 0;

	if (!msc->initialized || msc->state != MSC_STATE_READY) {
		return -ENODEV;
	}

	LOG_DBG("Write: LBA=%u, sectors=%u", start_sector, num_sectors);

	k_mutex_lock(&msc->lock, K_FOREVER);

	while (remaining_sectors > 0) {
		uint16_t transfer_sectors = scsi_calc_optimal_transfer_blocks(
			&msc->scsi, remaining_sectors);

		ret = scsi_validate_rw_params(&msc->scsi, current_lba, transfer_sectors);
		if (ret) {
			break;
		}

		scsi_build_write_10(cmd, current_lba, transfer_sectors);
		uint32_t transfer_bytes = transfer_sectors * msc->scsi.block_size;

		ret = msc_scsi_command_exec(msc, cmd, 10, (uint8_t *)current_buf, 
					   transfer_bytes, false);
		if (ret < 0) {
			LOG_ERR("Write failed: LBA=%u, sectors=%u, error=%d", 
				current_lba, transfer_sectors, ret);
			break;
		}

		current_lba += transfer_sectors;
		current_buf += transfer_bytes;
		remaining_sectors -= transfer_sectors;
		msc->stats.write_count++;
	}

	k_mutex_unlock(&msc->lock);
	return ret;
}

static int msc_disk_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
	struct usbh_msc_data *msc = CONTAINER_OF(disk, struct usbh_msc_data, disk_info);

	switch (cmd) {
	case DISK_IOCTL_GET_SECTOR_COUNT:
		if (!msc->initialized) {
			return -ENODEV;
		}
		*(uint32_t *)buff = msc->scsi.total_blocks;
		return 0;

	case DISK_IOCTL_GET_SECTOR_SIZE:
		if (!msc->initialized) {
			return -ENODEV;
		}
		*(uint32_t *)buff = msc->scsi.block_size;
		return 0;

	case DISK_IOCTL_CTRL_SYNC:
		/* USB MSC typically doesn't require explicit sync */
		return 0;

	case DISK_IOCTL_SET_SIGNAL:
		msc->signal = (struct k_poll_signal *)buff;
		return 0;

	default:
		return -ENOTSUP;
	}
}

/* USB Host Class API implementation */

static int usbh_msc_init(struct usbh_class_data *cdata)
{
	LOG_DBG("MSC host class init");
	
	k_mutex_init(&msc_data.lock);
	msc_data.disk_info.ops = &msc_disk_ops;
	msc_state_change(MSC_STATE_DISCONNECTED);
	
	return disk_access_register(&msc_data.disk_info);
}

static int usbh_msc_connected(struct usb_device *udev, 
			      struct usbh_class_data *cdata,
			      void *desc_start, void *desc_end)
{
	struct usb_if_descriptor *if_desc;
	uint8_t *desc_buf = (uint8_t *)desc_start;
	int ret;

	LOG_INF("MSC device connected");

	if (cdata->class_matched) {
		return 0;
	}
	cdata->class_matched = 1;

	/* Find MSC interface */
	while (desc_buf < (uint8_t *)desc_end) {
		struct usb_desc_header *header = (struct usb_desc_header *)desc_buf;
		
		if (header->bLength == 0) {
			break;
		}

		if (header->bDescriptorType == USB_DESC_INTERFACE) {
			if_desc = (struct usb_if_descriptor *)desc_buf;
			
			if (if_desc->bInterfaceClass == USB_CLASS_MASS_STORAGE &&
			    if_desc->bInterfaceSubClass == USB_SUBCLASS_SCSI &&
			    if_desc->bInterfaceProtocol == USB_PROTOCOL_BOT) {
				
				ret = msc_parse_endpoints(&msc_data, if_desc);
				if (ret) {
					LOG_ERR("Failed to parse endpoints: %d", ret);
					return ret;
				}
				break;
			}
		}

		desc_buf += header->bLength;
	}

	k_mutex_lock(&msc_data.lock, K_FOREVER);
	msc_data.udev = udev;
	msc_data.initialized = false;
	msc_state_change(MSC_STATE_CONNECTED);
	k_mutex_unlock(&msc_data.lock);

	/* Signal device connection */
	if (msc_data.signal) {
		k_poll_signal_raise(msc_data.signal, USBH_DEVICE_CONNECTED);
	}

	return 0;
}

static int usbh_msc_removed(struct usb_device *udev,
			    struct usbh_class_data *cdata)
{
	LOG_INF("MSC device disconnected");

	k_mutex_lock(&msc_data.lock, K_FOREVER);
	msc_data.udev = NULL;
	msc_data.initialized = false;
	msc_state_change(MSC_STATE_DISCONNECTED);
	msc_data.bulk_in_ep = 0;
	msc_data.bulk_out_ep = 0;
	k_mutex_unlock(&msc_data.lock);

	/* Signal device disconnection */
	if (msc_data.signal) {
		k_poll_signal_raise(msc_data.signal, USBH_DEVICE_DISCONNECTED);
	}

	cdata->class_matched = 0;
	return 0;
}

static int usbh_msc_suspended(struct usbh_context *const uhs_ctx)
{
	return 0;
}

static int usbh_msc_resumed(struct usbh_context *const uhs_ctx)
{
	return 0;
}

static int usbh_msc_rwup(struct usbh_context *const uhs_ctx)
{
	return 0;
}

static const struct usbh_class_api msc_class_api = {
	.init = usbh_msc_init,
	.connected = usbh_msc_connected,
	.removed = usbh_msc_removed,
	.suspended = usbh_msc_suspended,
	.resumed = usbh_msc_resumed,
	.rwup = usbh_msc_rwup,
};

USBH_DEFINE_CLASS(msc_class_data, &msc_class_api, NULL,
		  msc_device_code, ARRAY_SIZE(msc_device_code));
