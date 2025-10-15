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

#include "usbh_device.h"
#include "usbh_ch9.h"
#include "usbh_msc.h"

LOG_MODULE_REGISTER(usbh_msc, CONFIG_USBH_MSC_CLASS_LOG_LEVEL);

/* MSC device state */
struct usbh_msc_data {
	struct usb_device *udev;
	struct disk_info disk_info;
	struct k_mutex lock;
	struct k_poll_signal *signal;
	
	/** Collection of all available alternate interfaces */
	struct usb_if_descriptor *ifaces[CONFIG_USBH_MSC_MAX_INTERFACE];
	uint8_t num_ifaces;  /* Number of MSC interfaces found */
	uint8_t current_iface_idx;  /* Index of currently used interface */
	
	/* Currently used USB endpoint descriptors */
	struct usb_ep_descriptor *bulk_in_ep_desc;
	struct usb_ep_descriptor *bulk_out_ep_desc;
	
	/* BOT protocol buffers */
	struct cbw cbw;
	struct csw csw;

	/* Device state */
	enum msc_device_state state;
	bool initialized;
	
	/* Transfer tracking */
	uint32_t tag_counter;
	
	/* Device info */
	uint32_t sector_count;
	uint32_t sector_size;
	
	/* Statistics */
	struct {
		uint32_t read_count;
		uint32_t write_count;
		uint32_t error_count;
		uint32_t retry_count;
	} stats;
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
 * @brief Send basic SCSI command via BOT
 */
static int msc_send_scsi_command(struct usbh_msc_data *msc, 
				 const uint8_t *cmd, uint8_t cmd_len,
				 uint8_t *data, uint32_t data_len, 
				 bool data_in)
{
	struct net_buf *buf = NULL;
	struct uhc_transfer *xfer = NULL;
	int ret;

	if (!msc || !msc->udev || !cmd) {
		return -EINVAL;
	}

	/* Prepare CBW in structure buffer */
	memset(&msc->cbw, 0, sizeof(msc->cbw));
	msc->cbw.dCBWSignature = sys_cpu_to_le32(CBW_SIGNATURE);
	msc->cbw.dCBWTag = sys_cpu_to_le32(++msc->tag_counter);
	msc->cbw.dCBWDataTransferLength = sys_cpu_to_le32(data_len);
	msc->cbw.bmCBWFlags = data_in ? CBW_FLAGS_DATA_IN : CBW_FLAGS_DATA_OUT;
	msc->cbw.bCBWLUN = 0;
	msc->cbw.bCBWCBLength = cmd_len;
	memcpy(msc->cbw.CBWCB, cmd, cmd_len);

	/* Send CBW */
	xfer = usbh_xfer_alloc(msc->udev, msc->bulk_out_ep_desc->bEndpointAddress, NULL, msc);
	if (!xfer) {
		return -ENOMEM;
	}

	buf = usbh_xfer_buf_alloc(msc->udev, sizeof(msc->cbw));
	if (!buf) {
		usbh_xfer_free(msc->udev, xfer);
		return -ENOMEM;
	}

	memcpy(buf->data, &msc->cbw, sizeof(msc->cbw));
	net_buf_add(buf, sizeof(msc->cbw));
	xfer->buf = buf;

	ret = usbh_xfer_enqueue(msc->udev, xfer);
	if (ret) {
		net_buf_unref(buf);
		usbh_xfer_free(msc->udev, xfer);
		return ret;
	}

	/* Handle data phase if present */
	if (data && data_len > 0) {
		uint8_t ep = data_in ? msc->bulk_in_ep_desc->bEndpointAddress : 
				       msc->bulk_out_ep_desc->bEndpointAddress;
		
		xfer = usbh_xfer_alloc(msc->udev, ep, NULL, msc);
		if (!xfer) {
			return -ENOMEM;
		}

		buf = usbh_xfer_buf_alloc(msc->udev, data_len);
		if (!buf) {
			usbh_xfer_free(msc->udev, xfer);
			return -ENOMEM;
		}

		if (!data_in) {
			memcpy(buf->data, data, data_len);
			net_buf_add(buf, data_len);
		}
		xfer->buf = buf;

		ret = usbh_xfer_enqueue(msc->udev, xfer);
		if (ret) {
			net_buf_unref(buf);
			usbh_xfer_free(msc->udev, xfer);
			return ret;
		}

		if (data_in && buf->len > 0) {
			size_t copy_len = MIN(buf->len, data_len);
			memcpy(data, buf->data, copy_len);
		}
	}

	/* Receive CSW */
	xfer = usbh_xfer_alloc(msc->udev, msc->bulk_in_ep_desc->bEndpointAddress, NULL, msc);
	if (!xfer) {
		return -ENOMEM;
	}

	buf = usbh_xfer_buf_alloc(msc->udev, sizeof(msc->csw));
	if (!buf) {
		usbh_xfer_free(msc->udev, xfer);
		return -ENOMEM;
	}

	xfer->buf = buf;

	ret = usbh_xfer_enqueue(msc->udev, xfer);
	if (ret) {
		net_buf_unref(buf);
		usbh_xfer_free(msc->udev, xfer);
		return ret;
	}

	if (buf->len >= sizeof(msc->csw)) {
		memcpy(&msc->csw, buf->data, sizeof(msc->csw));
		
		if (sys_le32_to_cpu(msc->csw.dCSWSignature) != CSW_SIGNATURE ||
		    sys_le32_to_cpu(msc->csw.dCSWTag) != msc->tag_counter ||
		    msc->csw.bCSWStatus != CSW_STATUS_PASSED) {
			ret = -EIO;
		}
	} else {
		ret = -EIO;
	}

	return ret;
}

/**
 * @brief Basic SCSI READ CAPACITY command
 */
static int msc_read_capacity(struct usbh_msc_data *msc)
{
	uint8_t cmd[10] = {0x25}; /* READ CAPACITY(10) */
	uint8_t data[8];
	int ret;

	ret = msc_send_scsi_command(msc, cmd, 10, data, 8, true);
	if (ret == 0) {
		msc->sector_count = sys_be32_to_cpu(*(uint32_t *)&data[0]) + 1;
		msc->sector_size = sys_be32_to_cpu(*(uint32_t *)&data[4]);
		
		LOG_INF("Capacity: %u sectors x %u bytes", 
			msc->sector_count, msc->sector_size);
	}

	return ret;
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
				msc->bulk_in_ep_desc = ep_desc;
				found_in = true;
			} else {
				msc->bulk_out_ep_desc = ep_desc;
				found_out = true;
			}
		}

		desc_buf += ep_desc->bLength;
	}

	if (found_in && found_out) {
		LOG_DBG("Found endpoints: IN=0x%02x, OUT=0x%02x", 
			msc->bulk_in_ep_desc->bEndpointAddress,
			msc->bulk_out_ep_desc->bEndpointAddress);
		return 0;
	}

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

	/* Test unit ready */
	uint8_t test_cmd[6] = {0};
	ret = msc_send_scsi_command(msc, test_cmd, 6, NULL, 0, false);
	if (ret) {
		LOG_WRN("Test unit ready failed: %d", ret);
	}

	/* Read capacity */
	ret = msc_read_capacity(msc);
	if (ret) {
		LOG_ERR("Read capacity failed: %d", ret);
		msc_state_change(MSC_STATE_ERROR);
		return ret;
	}

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
	int ret;

	if (!msc->initialized || msc->state != MSC_STATE_READY) {
		return -ENODEV;
	}

	k_mutex_lock(&msc->lock, K_FOREVER);

	/* READ(10) command */
	cmd[0] = 0x28;
	cmd[1] = 0;
	sys_put_be32(start_sector, &cmd[2]);
	cmd[6] = 0;
	sys_put_be16(num_sectors, &cmd[7]);
	cmd[9] = 0;

	ret = msc_send_scsi_command(msc, cmd, 10, data_buf, 
				   num_sectors * msc->sector_size, true);
	if (ret == 0) {
		msc->stats.read_count++;
	} else {
		msc->stats.error_count++;
	}

	k_mutex_unlock(&msc->lock);
	return ret;
}

static int msc_disk_write(struct disk_info *disk, const uint8_t *data_buf,
			  uint32_t start_sector, uint32_t num_sectors)
{
	struct usbh_msc_data *msc = CONTAINER_OF(disk, struct usbh_msc_data, disk_info);
	uint8_t cmd[10];
	int ret;

	if (!msc->initialized || msc->state != MSC_STATE_READY) {
		return -ENODEV;
	}

	k_mutex_lock(&msc->lock, K_FOREVER);

	/* WRITE(10) command */
	cmd[0] = 0x2A;
	cmd[1] = 0;
	sys_put_be32(start_sector, &cmd[2]);
	cmd[6] = 0;
	sys_put_be16(num_sectors, &cmd[7]);
	cmd[9] = 0;

	ret = msc_send_scsi_command(msc, cmd, 10, (uint8_t *)data_buf,
				   num_sectors * msc->sector_size, false);
	if (ret == 0) {
		msc->stats.write_count++;
	} else {
		msc->stats.error_count++;
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
		*(uint32_t *)buff = msc->sector_count;
		return 0;

	case DISK_IOCTL_GET_SECTOR_SIZE:
		if (!msc->initialized) {
			return -ENODEV;
		}
		*(uint32_t *)buff = msc->sector_size;
		return 0;

	case DISK_IOCTL_CTRL_SYNC:
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
	uint8_t interface_number = 0;
	int selected_iface_idx = -1;

	LOG_INF("MSC device connected");

	if (cdata->class_matched) {
		return 0;
	}
	cdata->class_matched = 1;

	/* Reset interface collection */
	msc_data.num_ifaces = 0;
	msc_data.current_iface_idx = 0;

	/* Find and collect all MSC interfaces */
	while (desc_buf < (uint8_t *)desc_end && msc_data.num_ifaces < CONFIG_USBH_MSC_MAX_INTERFACE) {
		struct usb_desc_header *header = (struct usb_desc_header *)desc_buf;
		
		if (header->bLength == 0) {
			break;
		}

		if (header->bDescriptorType == USB_DESC_INTERFACE) {
			if_desc = (struct usb_if_descriptor *)desc_buf;
			
			if (if_desc->bInterfaceClass == USB_CLASS_MASS_STORAGE &&
			    if_desc->bInterfaceSubClass == USB_SUBCLASS_SCSI &&
			    if_desc->bInterfaceProtocol == USB_PROTOCOL_BOT) {
				
				/* Save this MSC interface */
				msc_data.ifaces[msc_data.num_ifaces] = if_desc;
				
				LOG_DBG("Found MSC interface %d: alt=%d", 
					if_desc->bInterfaceNumber, if_desc->bAlternateSetting);
				
				/* Use the first interface found */
				if (selected_iface_idx == -1) {
					selected_iface_idx = msc_data.num_ifaces;
					interface_number = if_desc->bInterfaceNumber;
				}
				
				msc_data.num_ifaces++;
			}
		}

		desc_buf += header->bLength;
	}

	if (msc_data.num_ifaces == 0) {
		LOG_ERR("No MSC interfaces found");
		return -ENODEV;
	}

	if (selected_iface_idx == -1) {
		LOG_ERR("No suitable MSC interface found");
		return -ENODEV;
	}

	/* Use the selected interface */
	msc_data.current_iface_idx = selected_iface_idx;
	if_desc = msc_data.ifaces[selected_iface_idx];

	LOG_INF("Using MSC interface %d (found %d total interfaces)",
		interface_number, msc_data.num_ifaces);

	/* Parse endpoints from selected interface */
	ret = msc_parse_endpoints(&msc_data, if_desc);
	if (ret) {
		LOG_ERR("Failed to parse endpoints: %d", ret);
		return ret;
	}

	/* Verify endpoints were found */
	if (!msc_data.bulk_in_ep_desc || !msc_data.bulk_out_ep_desc) {
		LOG_ERR("Required bulk endpoints not found");
		return -ENODEV;
	}

	/* Set interface with current alternate setting */
	ret = usbh_device_interface_set(udev, interface_number, if_desc->bAlternateSetting, false);
	if (ret) {
		LOG_ERR("Failed to set interface %d alt setting %d: %d", 
			interface_number, if_desc->bAlternateSetting, ret);
		return ret;
	}

	LOG_DBG("Interface %d alt setting %d set successfully", 
		interface_number, if_desc->bAlternateSetting);
	LOG_INF("MSC endpoints: IN=0x%02x (maxpkt=%d), OUT=0x%02x (maxpkt=%d)",
		msc_data.bulk_in_ep_desc->bEndpointAddress,
		sys_le16_to_cpu(msc_data.bulk_in_ep_desc->wMaxPacketSize),
		msc_data.bulk_out_ep_desc->bEndpointAddress,
		sys_le16_to_cpu(msc_data.bulk_out_ep_desc->wMaxPacketSize));

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
	msc_data.bulk_in_ep_desc = NULL;
	msc_data.bulk_out_ep_desc = NULL;
	
	/* Clear interface collection */
	memset(msc_data.ifaces, 0, sizeof(msc_data.ifaces));
	msc_data.num_ifaces = 0;
	msc_data.current_iface_idx = 0;
	
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
