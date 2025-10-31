/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_cdc_ecm_host

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/class/usb_cdc.h>
#include <zephyr/logging/log.h>

#include "usbh_device.h"
#include "usbh_ch9.h"
#include "usbh_cdc_ecm.h"

/* Fix log level configuration name */
LOG_MODULE_REGISTER(usbh_cdc_ecm, LOG_LEVEL_ERR);

uint8_t int_finished = 0;
static int usbh_cdc_ecm_submit_bulk_in(const struct device *cdc_dev);
static int usbh_cdc_ecm_submit_int_in(const struct device *cdc_dev);

/* CDC ECM Class-Specific Requests */
#define CDC_ECM_SET_ETHERNET_PACKET_FILTER	0x43
#define CDC_ECM_GET_ETHERNET_STATISTIC		0x44

/* CDC ECM Notifications */
#define CDC_ECM_NETWORK_CONNECTION		0x00
#define CDC_ECM_RESPONSE_AVAILABLE		0x01
#define CDC_ECM_CONNECTION_SPEED_CHANGE		0x2A

/**
 * @brief USB CDC ECM device code table for matching devices
 */
static const struct usbh_device_code_table cdc_ecm_device_code[] = {
	/* Generic CDC ECM interface match */
	{
		.match_type = USBH_MATCH_INTFACE,
		.interface_class_code = USB_BCC_CDC_CONTROL,
		.interface_subclass_code = ECM_SUBCLASS,
		.interface_protocol_code = 0,
	}
};

/**
 * @brief USB Host CDC ECM device statistics
 */
struct usbh_cdc_ecm_stats {
	uint32_t rx_packets;     /**< Number of received packets */
	uint32_t tx_packets;     /**< Number of transmitted packets */
	uint32_t rx_bytes;       /**< Number of received bytes */
	uint32_t tx_bytes;       /**< Number of transmitted bytes */
	uint32_t rx_errors;      /**< Number of receive errors */
	uint32_t tx_errors;      /**< Number of transmit errors */
	uint32_t rx_dropped;     /**< Number of dropped receive packets */
	uint32_t tx_dropped;     /**< Number of dropped transmit packets */
};

/**
 * @brief USB Host CDC ECM Host device data structure
 */
struct usbh_cdc_ecm_data {
	/** Associated USB device */
	struct usb_device *udev;
	/** Control interface descriptor */
	struct usb_if_descriptor *ctrl_if;
	/** Data interface descriptor (alternate 0 - no endpoints) */
	struct usb_if_descriptor *data_if_alt0;
	/** Data interface descriptor (alternate 1 - active) */
	struct usb_if_descriptor *data_if_alt1;
	/** ECM functional descriptor */
	struct cdc_ecm_descriptor *ecm_desc;
	/** Interrupt IN endpoint */
	struct usb_ep_descriptor *int_ep;
	/** Bulk IN endpoint */
	struct usb_ep_descriptor *bulk_in_ep;
	/** Bulk OUT endpoint */
	struct usb_ep_descriptor *bulk_out_ep;
	/** Network interface */
	struct net_if *iface;
	/** Device MAC address */
	uint8_t mac_addr[6];
	/** Device connection status */
	bool connected;
	/** Device network connection status */
	uint8_t device_network_connection;
	/** Device network downlink speed */
	uint32_t device_network_downlink_speed;
	/** Device network uplink speed */
	uint32_t device_network_uplink_speed;
	/** Device access synchronization */
	struct k_mutex lock;
	/** Transmit synchronization */
	struct k_sem tx_sem;
	/** Signal to alert application of device events */
	struct k_poll_signal *sig;
	/** Interrupt IN transfer (reusable) */
	struct uhc_transfer *int_xfer;
	/** Device statistics */
	struct usbh_cdc_ecm_stats stats;
};


/**
 * @brief Resubmit bulk IN transfer with new buffer
 */
static int usbh_cdc_ecm_resubmit_bulk_in(struct usbh_cdc_ecm_data *data,
					 struct uhc_transfer *xfer)
{
	struct net_buf *buf;
	int ret;

	if (!data->connected || !data->bulk_in_ep) {
		usbh_xfer_free(data->udev, xfer);
		return -ENODEV;
	}

	/* Allocate new buffer for next transfer */
	buf = usbh_xfer_buf_alloc(data->udev, 
				  sys_le16_to_cpu(data->bulk_in_ep->wMaxPacketSize));
	if (!buf) {
		LOG_ERR("Failed to allocate bulk IN buffer");
		usbh_xfer_free(data->udev, xfer);
		return -ENOMEM;
	}

	/* Reuse the same xfer with new buffer */
	xfer->buf = buf;

	/* Submit transfer */
	ret = usbh_xfer_enqueue(data->udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to resubmit bulk IN transfer: %d", ret);
		usbh_xfer_buf_free(data->udev, buf);
		usbh_xfer_free(data->udev, xfer);
		return ret;
	}

	return 0;
}

/**
 * @brief USB transfer completion callback for bulk IN (receive)
 */
static int usbh_cdc_ecm_bulk_in_cb(struct usb_device *const dev,
				   struct uhc_transfer *const xfer)
{
	const struct device *cdc_dev = (const struct device *)xfer->priv;
	struct usbh_cdc_ecm_data *data = cdc_dev->data;
	struct net_buf *buf = xfer->buf;
	struct net_pkt *pkt;
	int ret;

	if (!data->connected) {
		net_buf_unref(buf);
		usbh_xfer_free(data->udev, xfer);
		return 0;
	}

#if 0
	if (xfer->err != 0) {
		LOG_ERR("Bulk IN transfer failed: %d", xfer->err);
		data->stats.rx_errors++;
		net_buf_unref(buf);
		goto resubmit;
	}
#endif

	if (buf->len == 0) {
		net_buf_unref(buf);
		goto resubmit;
	}

	/* Update statistics */
	data->stats.rx_packets++;
	data->stats.rx_bytes += buf->len;

	/* Allocate network packet */
	pkt = net_pkt_rx_alloc_with_buffer(data->iface, buf->len,
					   AF_UNSPEC, 0, K_NO_WAIT);
	if (!pkt) {
		LOG_ERR("Failed to allocate network packet");
		data->stats.rx_dropped++;
		net_buf_unref(buf);
		goto resubmit;
	}

	/* Copy ethernet frame data to network packet */
	if (net_pkt_write(pkt, buf->data, buf->len)) {
		LOG_ERR("Failed to write data to network packet");
		data->stats.rx_errors++;
		net_pkt_unref(pkt);
		net_buf_unref(buf);
		goto resubmit;
	}

	net_buf_unref(buf);

	LOG_DBG("Received ethernet frame: %zu bytes", net_pkt_get_len(pkt));

	/* Pass to network stack */
	ret = net_recv_data(data->iface, pkt);
	if (ret < 0) {
		LOG_ERR("Failed to pass packet to network stack: %d", ret);
		data->stats.rx_errors++;
		net_pkt_unref(pkt);
	}

resubmit:
	/* Resubmit using the same xfer */
	if (data->connected) {
		usbh_cdc_ecm_resubmit_bulk_in(data, xfer);
	} else {
		usbh_xfer_free(data->udev, xfer);
	}
	return 0;
}


/**
 * @brief Submit bulk IN transfer for receiving data
 */
static int usbh_cdc_ecm_submit_bulk_in(const struct device *cdc_dev)
{
	struct usbh_cdc_ecm_data *data = cdc_dev->data;
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	int ret;

	if (!data->connected || !data->bulk_in_ep) {
		return -ENODEV;
	}

	/* Allocate USB transfer */
	xfer = usbh_xfer_alloc(data->udev, data->bulk_in_ep->bEndpointAddress,
			       usbh_cdc_ecm_bulk_in_cb, (void *)cdc_dev);
	if (!xfer) {
		LOG_ERR("Failed to allocate bulk IN transfer");
		return -ENOMEM;
	}

	/* Allocate transfer buffer */
	buf = usbh_xfer_buf_alloc(data->udev, 
				  sys_le16_to_cpu(data->bulk_in_ep->wMaxPacketSize));
	if (!buf) {
		LOG_ERR("Failed to allocate bulk IN buffer");
		usbh_xfer_free(data->udev, xfer);
		return -ENOMEM;
	}

	xfer->buf = buf;

	/* Submit transfer */
	ret = usbh_xfer_enqueue(data->udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to enqueue bulk IN transfer: %d", ret);
		usbh_xfer_buf_free(data->udev, buf);
		usbh_xfer_free(data->udev, xfer);
		return ret;
	}

	return 0;
}
/**
 * @brief Convert single hex character to number (NXP implementation style)
 */
static int _char_atoi16(const char *ch)
{
	char a[3] = {0};
	int i = 0;
	
	if (ch[0] >= '0' && ch[0] <= '9') {
		a[0] = ch[0];
		i = atoi(a);
	}
	else if (ch[0] >= 'A' && ch[0] <= 'F') {
		a[0] = '1';
		a[1] = '0' + ch[0] - 'A';
		i = atoi(a);
	}
	else if (ch[0] >= 'a' && ch[0] <= 'f') {
		a[0] = '1';
		a[1] = '0' + ch[0] - 'a';
		i = atoi(a);
	}
	else {
		i = -1;
	}
	return i;
}

/**
 * @brief Convert Unicode string to number array (NXP implementation)
 */
static int USB_HostCdcEcmUnicodeStrToNum(const uint16_t *strBuf, uint32_t strlength, uint8_t *const numBuf)
{
    int status = 0;
	uint32_t count = 0U;
	
	if (strBuf) {
		uint8_t str[3] = "";
		int8_t num;
		
		for (uint32_t index = 0; index < strlength; index++) {
			str[0] = *(const uint8_t *)(&strBuf[index]);
			str[1] = *((const uint8_t *)(&strBuf[index]) + 1);
			str[2] = '\0';
			
			num = _char_atoi16((char *)str);
			if (num != -1 && num >= 0) {
				numBuf[index] = (uint8_t)(num);
				count++;
			}
		}
	}
	
	if (count != strlength) {
		status = -1;
	}

	return status;
}

/**
 * @brief Convert Unicode MAC address string to MAC address bytes (NXP implementation)
 */
static void USB_HostCdcEcmUnicodeMacAddressStrToNum(const uint16_t *strBuf, uint8_t *const macBuf)
{
	uint32_t maclength = 6U;
	uint8_t macByte[12] = {0};
	
	USB_HostCdcEcmUnicodeStrToNum(strBuf, maclength * 2, macByte);
	
	for (uint32_t index = 0U; index < maclength; index++) {
		macBuf[index] = (uint8_t)(macByte[index * 2] << 4) | (macByte[index * 2 + 1]);
	}

	return;
}

/**
 * @brief Get and parse MAC address from USB string descriptor (Two-stage approach)
 */
static int usbh_cdc_ecm_get_mac_address(struct usbh_cdc_ecm_data *data)
{
	struct usb_string_descriptor *string_desc;
	struct net_buf *buf;
	int ret;
	uint8_t macStringDescBuffer[26];  /* Max string descriptor size for MAC */
	
	if (!data->ecm_desc || data->ecm_desc->iMACAddress == 0) {
		LOG_ERR("No MAC address string descriptor available");
		return -ENOENT;
	}
	
	/* Stage 1: Get string descriptor length (first 2 bytes) */
	buf = usbh_xfer_buf_alloc(data->udev, 2);
	if (!buf) {
		LOG_ERR("Failed to allocate buffer for string descriptor length");
		return -ENOMEM;
	}
	
	ret = usbh_req_setup(data->udev,
		(USB_REQTYPE_DIR_TO_HOST << 7) |
		(USB_REQTYPE_TYPE_STANDARD << 5) |
		(USB_REQTYPE_RECIPIENT_DEVICE << 0),
		USB_SREQ_GET_DESCRIPTOR,
		(USB_DESC_STRING << 8) | data->ecm_desc->iMACAddress,
		0x0409,  /* English language ID */
		2,       /* Only get length info */
		buf);
	
	if (ret < 0) {
		LOG_ERR("Failed to get MAC string descriptor length: %d", ret);
		usbh_xfer_buf_free(data->udev, buf);
		return ret;
	}
	
	if (buf->len < 2) {
		LOG_ERR("Invalid string descriptor length response");
		usbh_xfer_buf_free(data->udev, buf);
		return -EBADMSG;
	}
	
	/* Get the actual length from the descriptor */
	uint8_t desc_length = buf->data[0];
	usbh_xfer_buf_free(data->udev, buf);
	
	if (desc_length > sizeof(macStringDescBuffer)) {
		LOG_ERR("MAC string descriptor too long: %u", desc_length);
		return -EBADMSG;
	}
	
	/* Stage 2: Get complete string descriptor */
	buf = usbh_xfer_buf_alloc(data->udev, desc_length);
	if (!buf) {
		LOG_ERR("Failed to allocate buffer for complete string descriptor");
		return -ENOMEM;
	}
	
	ret = usbh_req_setup(data->udev,
		(USB_REQTYPE_DIR_TO_HOST << 7) |
		(USB_REQTYPE_TYPE_STANDARD << 5) |
		(USB_REQTYPE_RECIPIENT_DEVICE << 0),
		USB_SREQ_GET_DESCRIPTOR,
		(USB_DESC_STRING << 8) | data->ecm_desc->iMACAddress,
		0x0409,  /* English language ID */
		desc_length,  /* Complete descriptor length */
		buf);
	
	if (ret < 0) {
		LOG_ERR("Failed to get complete MAC string descriptor: %d", ret);
		goto cleanup;
	}
	
	if (buf->len < 4) {
		LOG_ERR("MAC string descriptor too short: %u", buf->len);
		ret = -EBADMSG;
		goto cleanup;
	}
	
	string_desc = (struct usb_string_descriptor *)buf->data;
	
	/* Validate string descriptor */
	if (string_desc->bDescriptorType != USB_DESC_STRING ||
	    string_desc->bLength < 4) {
		LOG_ERR("Invalid string descriptor: type=0x%02x, length=%u",
			string_desc->bDescriptorType, string_desc->bLength);
		ret = -EBADMSG;
		goto cleanup;
	}
	
	/* Copy to buffer for processing */
	memcpy(macStringDescBuffer, buf->data, buf->len);
	
	/* Convert Unicode MAC address string to MAC bytes (skip 2-byte header) */
	USB_HostCdcEcmUnicodeMacAddressStrToNum((uint16_t *)(&macStringDescBuffer[2]), data->mac_addr);
	
	LOG_ERR("Parsed device MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
		data->mac_addr[0], data->mac_addr[1], data->mac_addr[2],
		data->mac_addr[3], data->mac_addr[4], data->mac_addr[5]);
	
	ret = 0;

cleanup:
	usbh_xfer_buf_free(data->udev, buf);
	return ret;
}


/**
 * @brief Configure USB interfaces for CDC ECM device
 */
static int usbh_cdc_ecm_configure_interfaces(struct usbh_cdc_ecm_data *data)
{
	int ret;

	/* Set control interface to alternate 0 */
	ret = usbh_device_interface_set(data->udev,
					data->ctrl_if->bInterfaceNumber,
					0, false);
	if (ret) {
		LOG_ERR("Failed to set control interface: %d", ret);
		return ret;
	}

	/* Set data interface to alternate 1 (active with endpoints) */
	ret = usbh_device_interface_set(data->udev,
					data->data_if_alt1->bInterfaceNumber,
					1, false);
	if (ret) {
		LOG_ERR("Failed to set data interface to alt 1: %d", ret);
		return ret;
	}

	LOG_INF("CDC ECM interfaces configured successfully");
	return 0;
}

/**
 * @brief Parse USB interface descriptors for CDC ECM device
 */
static int usbh_cdc_ecm_parse_interfaces(struct usbh_cdc_ecm_data *data,
					 void *desc_start, void *desc_end)
{
	uint8_t *desc_buf = (uint8_t *)desc_start;
	uint8_t *desc_end_buf = (uint8_t *)desc_end;

	while (desc_buf < desc_end_buf) {
		struct usb_desc_header *header = (struct usb_desc_header *)desc_buf;

		if (header->bLength == 0) {
			break;
		}

		if (header->bDescriptorType == USB_DESC_INTERFACE) {
			struct usb_if_descriptor *if_desc = (struct usb_if_descriptor *)desc_buf;

			/* Look for CDC Control interface */
			if (if_desc->bInterfaceClass == USB_BCC_CDC_CONTROL &&
			    if_desc->bInterfaceSubClass == ECM_SUBCLASS) {
				data->ctrl_if = if_desc;
				LOG_DBG("Found CDC Control interface %u", if_desc->bInterfaceNumber);
			}

			/* Look for CDC Data interfaces */
			if (if_desc->bInterfaceClass == USB_BCC_CDC_DATA) {
				if (if_desc->bAlternateSetting == 0) {
					data->data_if_alt0 = if_desc;
					LOG_DBG("Found CDC Data interface %u alt 0", 
						if_desc->bInterfaceNumber);
				} else if (if_desc->bAlternateSetting == 1) {
					data->data_if_alt1 = if_desc;
					LOG_DBG("Found CDC Data interface %u alt 1", 
						if_desc->bInterfaceNumber);
				}
			}
		} else if (header->bDescriptorType == USB_DESC_CS_INTERFACE) {
			/* Look for ECM functional descriptor */
			struct cdc_header_descriptor *func_desc = 
				(struct cdc_header_descriptor *)desc_buf;
			
			if (func_desc->bDescriptorSubtype == ETHERNET_FUNC_DESC) {
				data->ecm_desc = (struct cdc_ecm_descriptor *)desc_buf;
				LOG_DBG("Found ECM functional descriptor");
			}
		} else if (header->bDescriptorType == USB_DESC_ENDPOINT) {
			struct usb_ep_descriptor *ep_desc = (struct usb_ep_descriptor *)desc_buf;
			uint8_t ep_addr = ep_desc->bEndpointAddress;
			uint8_t ep_type = ep_desc->bmAttributes & USB_EP_TRANSFER_TYPE_MASK;

			/* Interrupt IN endpoint (for notifications) */
			if ((ep_addr & USB_EP_DIR_MASK) == USB_EP_DIR_IN &&
			    ep_type == USB_EP_TYPE_INTERRUPT) {
				data->int_ep = ep_desc;
				LOG_DBG("Found interrupt IN endpoint 0x%02x", ep_addr);
			}

			/* Bulk endpoints (for data transfer) */
			if (ep_type == USB_EP_TYPE_BULK) {
				if ((ep_addr & USB_EP_DIR_MASK) == USB_EP_DIR_IN) {
					data->bulk_in_ep = ep_desc;
					LOG_DBG("Found bulk IN endpoint 0x%02x", ep_addr);
				} else {
					data->bulk_out_ep = ep_desc;
					LOG_DBG("Found bulk OUT endpoint 0x%02x", ep_addr);
				}
			}
		}

		desc_buf += header->bLength;
	}

	/* Validate required interfaces and endpoints */
	if (!data->ctrl_if || !data->data_if_alt0 || !data->data_if_alt1) {
		LOG_ERR("Missing required CDC ECM interfaces");
		return -ENODEV;
	}

	if (!data->bulk_in_ep || !data->bulk_out_ep) {
		LOG_ERR("Missing required bulk endpoints");
		return -ENODEV;
	}

	return 0;
}


/**
 * @brief USB transfer completion callback for bulk OUT (transmit)
 */
static int usbh_cdc_ecm_bulk_out_cb(struct usb_device *const dev,
				    struct uhc_transfer *const xfer)
{
	const struct device *cdc_dev = (const struct device *)xfer->priv;
	struct usbh_cdc_ecm_data *data = cdc_dev->data;

	if (xfer->err != 0) {
		LOG_ERR("Bulk OUT transfer failed: %d", xfer->err);
		data->stats.tx_errors++;
	} else {
		data->stats.tx_packets++;
		data->stats.tx_bytes += xfer->buf->len;
	}

	net_buf_unref(xfer->buf);
	usbh_xfer_free(data->udev, xfer); 
	k_sem_give(&data->tx_sem);

	return 0;
}

/**
 * @brief Ethernet API: Send ethernet frame
 */
static int usbh_cdc_ecm_send(const struct device *dev, struct net_pkt *pkt)
{
	struct usbh_cdc_ecm_data *data = dev->data;
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	size_t frame_len;
	int ret;

	if (!data->connected) {
		LOG_ERR("Device not connected");
		return -ENODEV;
	}

	frame_len = net_pkt_get_len(pkt);
	if (frame_len > NET_ETH_MAX_FRAME_SIZE) {
		LOG_ERR("Frame too large: %zu", frame_len);
		return -EINVAL;
	}

	/* Take transmit semaphore */
	ret = k_sem_take(&data->tx_sem, K_MSEC(1000));
	if (ret != 0) {
		LOG_ERR("Transmit timeout");
		return ret;
	}

	/* Allocate USB transfer */
	xfer = usbh_xfer_alloc(data->udev, data->bulk_out_ep->bEndpointAddress,
			       usbh_cdc_ecm_bulk_out_cb, (void *)dev);
	if (!xfer) {
		LOG_ERR("Failed to allocate bulk OUT transfer");
		k_sem_give(&data->tx_sem);
		return -ENOMEM;
	}

	/* Allocate transfer buffer */
	buf = usbh_xfer_buf_alloc(data->udev, frame_len);
	if (!buf) {
		LOG_ERR("Failed to allocate bulk OUT buffer");
		usbh_xfer_free(data->udev, xfer);
		k_sem_give(&data->tx_sem);
		return -ENOMEM;
	}

	/* Copy ethernet frame from network packet to USB buffer */
	ret = net_pkt_read(pkt, buf->data, frame_len);
	if (ret) {
		LOG_ERR("Failed to read from network packet");
		usbh_xfer_buf_free(data->udev, buf);
		usbh_xfer_free(data->udev, xfer);
		k_sem_give(&data->tx_sem);
		return ret;
	}

	net_buf_add(buf, frame_len);
	xfer->buf = buf;

	/* Submit transfer */
	ret = usbh_xfer_enqueue(data->udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to enqueue bulk OUT transfer: %d", ret);
		usbh_xfer_buf_free(data->udev, buf);
		usbh_xfer_free(data->udev, xfer);
		k_sem_give(&data->tx_sem);
		return ret;
	}

	LOG_DBG("Transmitted ethernet frame: %zu bytes", frame_len);
	return 0;
}

/**
 * @brief Set poll signal for device events
 * @param dev CDC ECM device
 * @param sig Poll signal to use for notifications
 * @return 0 on success, negative error code on failure
 */
int usbh_cdc_ecm_set_signal(const struct device *dev, struct k_poll_signal *sig)
{
	struct usbh_cdc_ecm_data *data = dev->data;

	if (!dev || !sig) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Store the signal for later use */
	data->sig = sig;

	k_mutex_unlock(&data->lock);

	LOG_DBG("Poll signal set for CDC ECM device");
	return 0;
}


/**
 * @brief Ethernet API: Start network interface
 */
static int usbh_cdc_ecm_start(const struct device *dev)
{
	struct usbh_cdc_ecm_data *data = dev->data;

	LOG_DBG("Starting CDC ECM interface");

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->connected) {
		usbh_cdc_ecm_submit_bulk_in(dev);
		net_if_carrier_on(data->iface);
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

/**
 * @brief Ethernet API: Stop network interface
 */
static int usbh_cdc_ecm_stop(const struct device *dev)
{
	struct usbh_cdc_ecm_data *data = dev->data;

	LOG_DBG("Stopping CDC ECM interface");

	k_mutex_lock(&data->lock, K_FOREVER);
	net_if_carrier_off(data->iface);
	k_mutex_unlock(&data->lock);

	return 0;
}

/**
 * @brief Ethernet API: Get device capabilities
 */
static enum ethernet_hw_caps usbh_cdc_ecm_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);
	return ETHERNET_LINK_10BASE;
}
static void usbh_cdc_ecm_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct usbh_cdc_ecm_data *data = dev->data;

	data->iface = iface;
	
	/* Initialize as ethernet interface */
	ethernet_init(iface);
	
	/* Don't set MAC address here - will be set when USB device connects */
	
	/* Start with carrier off */
	net_if_carrier_off(iface);

	LOG_INF("CDC ECM network interface initialized - waiting for USB device");
}

/**
 * @brief Ethernet API structure
 */
static const struct ethernet_api usbh_cdc_ecm_eth_api = {
	.iface_api.init = usbh_cdc_ecm_iface_init,
	.send = usbh_cdc_ecm_send,
	.start = usbh_cdc_ecm_start,
	.stop = usbh_cdc_ecm_stop,
	.get_capabilities = usbh_cdc_ecm_get_capabilities,
};

/**
 * @brief Set Ethernet packet filter
 */
static int usbh_cdc_ecm_set_packet_filter(struct usbh_cdc_ecm_data *data)
{
	struct net_buf *buf;
	uint16_t filter_bitmap;
	int ret;

	/* Set packet filter to accept directed, broadcast packets */
	filter_bitmap = PACKET_TYPE_DIRECTED | PACKET_TYPE_BROADCAST;

	LOG_INF("Setting Ethernet packet filter: 0x%04x", filter_bitmap);

	buf = usbh_xfer_buf_alloc(data->udev, 0);
	if (!buf) {
		LOG_ERR("Failed to allocate buffer for packet filter request");
		return -ENOMEM;
	}

	ret = usbh_req_setup(data->udev,
			(USB_REQTYPE_DIR_TO_DEVICE << 7) |
			(USB_REQTYPE_TYPE_CLASS << 5) |
			(USB_REQTYPE_RECIPIENT_INTERFACE << 0),
			CDC_ECM_SET_ETHERNET_PACKET_FILTER,
			filter_bitmap,
			data->ctrl_if->bInterfaceNumber,
			0,
			buf);

	usbh_xfer_buf_free(data->udev, buf);

	if (ret < 0) {
		LOG_ERR("Failed to set packet filter: %d", ret);
		return ret;
	}

	LOG_INF("Ethernet packet filter configured successfully");
	return 0;
}

/**
 * @brief Resubmit interrupt IN transfer with new buffer
 */
static int usbh_cdc_ecm_resubmit_int_in(struct usbh_cdc_ecm_data *data,
					struct uhc_transfer *xfer)
{
	struct net_buf *buf;
	int ret;

	if (!data->connected || !data->int_ep) {
		usbh_xfer_free(data->udev, xfer);
		return -ENODEV;
	}

	/* Allocate new buffer for next transfer */
	buf = usbh_xfer_buf_alloc(data->udev, 
				  sys_le16_to_cpu(data->int_ep->wMaxPacketSize));
	if (!buf) {
		LOG_ERR("Failed to allocate interrupt IN buffer");
		usbh_xfer_free(data->udev, xfer);
		return -ENOMEM;
	}

	/* Reuse the same xfer with new buffer */
	xfer->buf = buf;

	/* Submit transfer */
	ret = usbh_xfer_enqueue(data->udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to resubmit interrupt IN transfer: %d", ret);
		usbh_xfer_buf_free(data->udev, buf);
		usbh_xfer_free(data->udev, xfer);
		return ret;
	}
	return 0;
}

/**
	* @brief USB transfer completion callback for interrupt IN (notifications)
	*/
static int usbh_cdc_ecm_int_in_cb(struct usb_device *const dev,
						struct uhc_transfer *const xfer)
{
	const struct device *cdc_dev = (const struct device *)xfer->priv;
	struct usbh_cdc_ecm_data *data = cdc_dev->data; 
	
	
	if (!cdc_dev || !data) {
		LOG_ERR("Invalid pointers: cdc_dev=%p, data=%p", cdc_dev, data);
		if (xfer->buf) {
			net_buf_unref(xfer->buf);
		}
		return -EINVAL;
	}
	
	struct net_buf *buf = xfer->buf;


	if (!data->connected) {
		net_buf_unref(buf);
		usbh_xfer_free(data->udev, xfer);
		return 0;
	}

	if (xfer->err != 0) {
		LOG_ERR("CDC-ECM interrupt in transfer error: %d", xfer->err);
		net_buf_unref(buf);
		goto resubmit;
	}

	if (buf->len >= 8) {
		/* Parse notification according to CDC specification */
		uint8_t *notify_data = buf->data;
		uint8_t notify_bNotificationCode = *(notify_data + 1);
		uint16_t notify_wValue = *(uint16_t *)(notify_data + 2);

		switch (notify_bNotificationCode) {
		case CDC_ECM_NETWORK_CONNECTION:
			data->device_network_connection = (uint8_t)(notify_wValue);
			LOG_INF("Device network connection is %s",
				data->device_network_connection ? "connected" : "disconnected");
			break;

		case CDC_ECM_CONNECTION_SPEED_CHANGE:
			if (buf->len >= 16) {
				uint8_t *notify_Data = notify_data + 8;
				data->device_network_downlink_speed = *(uint32_t *)(notify_Data);
				data->device_network_uplink_speed = *(uint32_t *)(notify_Data + 4);
				LOG_INF("Network speed changed to DL %u / UL %u bps",
					data->device_network_downlink_speed,
					data->device_network_uplink_speed);
					int_finished = 1;
			}
			break;

		default:
			LOG_WRN("Unknown interrupt notification: %d", notify_bNotificationCode);
			break;
		}
	}

	net_buf_unref(buf);

resubmit:
	/* Resubmit using the same xfer */
	if (data->connected) {
		LOG_INF("About to resubmit interrupt transfer");
		int ret = usbh_cdc_ecm_resubmit_int_in(data, xfer);
		LOG_INF("Resubmit result: %d", ret);
	} else {
		usbh_xfer_free(data->udev, xfer);
	}

	return 0;
}

static int usbh_cdc_ecm_submit_int_in(const struct device *cdc_dev)
{
	struct usbh_cdc_ecm_data *data = cdc_dev->data;
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	int ret;

	if (!data->connected || !data->int_ep) {
		return -ENODEV;
	}

	/* Allocate USB transfer */
	xfer = usbh_xfer_alloc(data->udev, data->int_ep->bEndpointAddress,
			       usbh_cdc_ecm_int_in_cb, (void *)cdc_dev);
	if (!xfer) {
		LOG_ERR("Failed to allocate interrupt IN transfer");
		return -ENOMEM;
	}

	LOG_ERR("xfer=%p, err=%d, buf=%p, len=%u", xfer, xfer->err, xfer->buf, xfer->buf->len);

	/* Allocate transfer buffer */
	buf = usbh_xfer_buf_alloc(data->udev, 
				  sys_le16_to_cpu(data->int_ep->wMaxPacketSize));
	if (!buf) {
		LOG_ERR("Failed to allocate interrupt IN buffer");
		usbh_xfer_free(data->udev, xfer);
		return -ENOMEM;
	}

	xfer->buf = buf;

	/* Submit transfer */
	ret = usbh_xfer_enqueue(data->udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to enqueue interrupt IN transfer: %d", ret);
		usbh_xfer_buf_free(data->udev, buf);
		usbh_xfer_free(data->udev, xfer);
		return ret;
	}

	return 0;
}

/**
 * @brief Get network interface from CDC ECM device
 * @param dev CDC ECM device
 * @return Network interface pointer or NULL if not available
 */
struct net_if *usbh_cdc_ecm_get_iface(const struct device *dev)
{
	struct usbh_cdc_ecm_data *data;

	if (!dev) {
		return NULL;
	}

	data = dev->data;
	return data->iface;
}

/**
 * @brief Submit bulk IN transfer for CDC ECM device
 * @param dev CDC ECM device
 * @return 0 on success, negative error code on failure
 */
int usbh_cdc_ecm_submit_bulk_in_transfer(const struct device *dev)
{
	if (!dev) {
		return -EINVAL;
	}

	return usbh_cdc_ecm_submit_bulk_in(dev);
}

/**
 * @brief USB Host Class API: Device connected
 */
static int usbh_cdc_ecm_connected(struct usb_device *udev,
				  struct usbh_class_data *cdata,
				  void *desc_start_addr, void *desc_end_addr)
{
	const struct device *dev = cdata->priv;
	struct usbh_cdc_ecm_data *data = dev->data;
	int ret;

	if (cdata->class_matched) {
		return 0; /* Already processed */
	}
	cdata->class_matched = 1;

	LOG_INF("CDC ECM device connected");

	k_mutex_lock(&data->lock, K_FOREVER);

	data->udev = udev;

	/* Parse interface descriptors */
	ret = usbh_cdc_ecm_parse_interfaces(data, desc_start_addr, desc_end_addr);
	if (ret) {
		LOG_ERR("Failed to parse interfaces: %d", ret);
		goto error;
	}

	/* Parse MAC address from descriptors */
	ret = usbh_cdc_ecm_get_mac_address(data);
	if (ret) {
		LOG_ERR("Failed to parse MAC address: %d", ret);
		goto error;
	}

	/* Configure USB interfaces */
	ret = usbh_cdc_ecm_configure_interfaces(data);
	if (ret) {
		LOG_ERR("Failed to configure interfaces: %d", ret);
		goto error;
	}
#if 1
	/* Now we have the real MAC address, set it on the interface */
	if (data->iface) {
		net_if_set_link_addr(data->iface, data->mac_addr, 
				     sizeof(data->mac_addr), NET_LINK_ETHERNET);
		LOG_ERR("CDC ECM MAC address set: %02x:%02x:%02x:%02x:%02x:%02x",
			data->mac_addr[0], data->mac_addr[1], data->mac_addr[2],
			data->mac_addr[3], data->mac_addr[4], data->mac_addr[5]);
	}
#endif
	/* Set packet filter */
	ret = usbh_cdc_ecm_set_packet_filter(data);
	if (ret) {
		LOG_ERR("Failed to set packet filter: %d", ret);
		goto error;
	}

	data->connected = true;
	data->device_network_connection = 0;
	data->device_network_downlink_speed = 0;
	data->device_network_uplink_speed = 0;

	/* Start interrupt IN monitoring */
	ret = usbh_cdc_ecm_submit_int_in(dev);
	if (ret) {
		LOG_WRN("Failed to submit interrupt IN: %d", ret);
		/* Continue anyway */
	}

	if (data->sig) {
		k_poll_signal_raise(data->sig, USBH_DEVICE_CONNECTED);
		LOG_DBG("CDC ECM device connected signal raised");
	}

	k_mutex_unlock(&data->lock);

	LOG_INF("CDC ECM device configured successfully");
	return 0;

error:
	data->connected = false;
	data->udev = NULL;
	k_mutex_unlock(&data->lock);
	return ret;
}


/**
 * @brief USB Host Class API: Device removed
 */
static int usbh_cdc_ecm_removed(struct usb_device *udev,
				struct usbh_class_data *cdata)
{
	const struct device *dev = cdata->priv;
	struct usbh_cdc_ecm_data *data = dev->data;

	LOG_INF("CDC ECM device removed");

	k_mutex_lock(&data->lock, K_FOREVER);

	data->connected = false;
	
	/* Stop network operations */
	if (data->iface) {
		net_if_carrier_off(data->iface);
	}

	/* Clear USB device references */
	data->udev = NULL;
	data->ctrl_if = NULL;
	data->data_if_alt0 = NULL;
	data->data_if_alt1 = NULL;
	data->ecm_desc = NULL;
	data->int_ep = NULL;
	data->bulk_in_ep = NULL;
	data->bulk_out_ep = NULL;

	cdata->class_matched = 0;

	k_mutex_unlock(&data->lock);

	LOG_INF("CDC ECM device cleanup completed");
	return 0;
}

/**
 * @brief USB Host Class API: Device suspended
 */
static int usbh_cdc_ecm_suspended(struct usbh_context *const uhs_ctx)
{
	return 0;
}

/**
 * @brief USB Host Class API: Device resumed
 */
static int usbh_cdc_ecm_resumed(struct usbh_context *const uhs_ctx)
{
	return 0;
}

/**
 * @brief USB Host Class API: Remote wakeup
 */
static int usbh_cdc_ecm_rwup(struct usbh_context *const uhs_ctx)
{
	return 0;
}

/**
 * @brief USB Host Class API: Initialize
 */
static int usbh_cdc_ecm_init(struct usbh_class_data *cdata)
{
	const struct device *dev = cdata->priv;
	struct usbh_cdc_ecm_data *data = dev->data;

	LOG_DBG("Initializing CDC ECM host class");

	/* Initialize synchronization objects */
	k_mutex_init(&data->lock);
	k_sem_init(&data->tx_sem, 1, 1);

	/* Initialize device state */
	data->connected = false;
	data->udev = NULL;

	return 0;
}

/**
 * @brief USB Host Class API structure
 */
static const struct usbh_class_api usbh_cdc_ecm_class_api = {
	.init = usbh_cdc_ecm_init,
	.connected = usbh_cdc_ecm_connected,
	.removed = usbh_cdc_ecm_removed,
	.rwup = usbh_cdc_ecm_rwup,
	.suspended = usbh_cdc_ecm_suspended,
	.resumed = usbh_cdc_ecm_resumed,
};

/**
 * @brief Device initialization
 */
static int usbh_cdc_ecm_dev_init(const struct device *dev)
{
	struct usbh_cdc_ecm_data *data = dev->data;

	LOG_INF("USB Host CDC ECM device initialized");
	return 0;
}

#define USBH_CDC_ECM_DT_DEVICE_DEFINE(n)					\
	static struct usbh_cdc_ecm_data usbh_cdc_ecm_data_##n;			\
	ETH_NET_DEVICE_DT_INST_DEFINE(n,					\
			      usbh_cdc_ecm_dev_init,				\
			      NULL,						\
			      &usbh_cdc_ecm_data_##n, NULL,			\
			      CONFIG_ETH_INIT_PRIORITY,			\
			      &usbh_cdc_ecm_eth_api, NET_ETH_MTU);		\
	USBH_DEFINE_CLASS(usbh_cdc_ecm_class_##n, &usbh_cdc_ecm_class_api,	\
			  (void *)DEVICE_DT_INST_GET(n),			\
			  cdc_ecm_device_code, ARRAY_SIZE(cdc_ecm_device_code));

DT_INST_FOREACH_STATUS_OKAY(USBH_CDC_ECM_DT_DEVICE_DEFINE);
