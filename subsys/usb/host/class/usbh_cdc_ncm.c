/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/usb_cdc.h>
#include <zephyr/usb/usbh.h>

#include "usbh_ch9.h"
#include "usbh_class.h"
#include "usbh_desc.h"

LOG_MODULE_REGISTER(usbh_cdc_ncm, CONFIG_USBH_CDC_NCM_LOG_LEVEL);

#define CDC_NCM_MAX_SUPPORTED_LANGID_COUNT ((UINT8_MAX - 2) / 2)
#define CDC_NCM_MAC_ADDR_CHARS             (NET_ETH_ADDR_LEN * 2)
#define CDC_NCM_TX_TIMEOUT                 K_MSEC(10)

/* Control request buffers, served from the UHC buffer pool */
#define CDC_NCM_MAC_STRING_DESC_REQ_BUF_SIZE (CDC_NCM_MAC_ADDR_CHARS * sizeof(uint16_t) + 2)
#define CDC_NCM_LANGID_STRING_DESC_REQ_BUF_SIZE                                                    \
	(CDC_NCM_MAX_SUPPORTED_LANGID_COUNT * sizeof(uint16_t) + 2)
#define CDC_NCM_SET_ETHERNET_MULTICAST_FILTER_REQ_BUF_SIZE                                         \
	(NET_ETH_MCAST_FILTER_COUNT * NET_ETH_ADDR_LEN)
#define CDC_NCM_NTB_PARAMS_REQ_BUF_SIZE  sizeof(struct usb_cdc_ncm_ntb_parameters)
#define CDC_NCM_NET_ADDR_REQ_BUF_SIZE    NET_ETH_ADDR_LEN
#define CDC_NCM_NTB_IN_SIZE_REQ_BUF_SIZE sizeof(struct usb_cdc_ncm_ntb_input_size)
#define CDC_NCM_REQ_BUF_MAX_SIZE                                                                   \
	MAX(MAX(CDC_NCM_MAC_STRING_DESC_REQ_BUF_SIZE, CDC_NCM_LANGID_STRING_DESC_REQ_BUF_SIZE),    \
	    MAX(MAX(CDC_NCM_SET_ETHERNET_MULTICAST_FILTER_REQ_BUF_SIZE,                            \
		     CDC_NCM_NTB_PARAMS_REQ_BUF_SIZE),                                             \
		CDC_NCM_NTB_IN_SIZE_REQ_BUF_SIZE))

#define CDC_NCM_NOTIF_BUF_MAX_SIZE (sizeof(struct usb_setup_packet) + 8)

#define CDC_NCM_CTRL_XFER_MIN_COUNT 1
#define CDC_NCM_COMM_XFER_MIN_COUNT 1
#define CDC_NCM_DATA_XFER_MIN_COUNT                                                                \
	(CONFIG_USBH_CDC_NCM_RX_PIPELINE_DEPTH + CONFIG_USBH_CDC_NCM_TX_PIPELINE_DEPTH)

#define CDC_NCM_UHC_XFER_MIN_COUNT                                                                 \
	((CDC_NCM_CTRL_XFER_MIN_COUNT + CDC_NCM_COMM_XFER_MIN_COUNT +                              \
	  CDC_NCM_DATA_XFER_MIN_COUNT) *                                                           \
	 CONFIG_USBH_CDC_NCM_INSTANCES_COUNT)
BUILD_ASSERT(CONFIG_UHC_XFER_COUNT >= CDC_NCM_UHC_XFER_MIN_COUNT,
	     "CONFIG_UHC_XFER_COUNT is too small for CDC-NCM. "
	     "Increase it to at least " STRINGIFY(CDC_NCM_UHC_XFER_MIN_COUNT) ".");

#define CDC_NCM_UHC_BUF_MIN_COUNT                                                                  \
	((CDC_NCM_CTRL_XFER_MIN_COUNT + CDC_NCM_COMM_XFER_MIN_COUNT) *                             \
	 CONFIG_USBH_CDC_NCM_INSTANCES_COUNT)
BUILD_ASSERT(CONFIG_UHC_BUF_COUNT >= CDC_NCM_UHC_BUF_MIN_COUNT,
	     "CONFIG_UHC_BUF_COUNT is too small for CDC-NCM. "
	     "Increase it to at least " STRINGIFY(CDC_NCM_UHC_BUF_MIN_COUNT) ".");

#define CDC_NCM_UHC_BUF_POOL_MIN_SIZE                                                              \
	((CDC_NCM_REQ_BUF_MAX_SIZE * CDC_NCM_CTRL_XFER_MIN_COUNT +                                 \
	  CDC_NCM_NOTIF_BUF_MAX_SIZE * CDC_NCM_COMM_XFER_MIN_COUNT) *                              \
	 CONFIG_USBH_CDC_NCM_INSTANCES_COUNT)
BUILD_ASSERT(CONFIG_UHC_BUF_POOL_SIZE >= CDC_NCM_UHC_BUF_POOL_MIN_SIZE,
	     "CONFIG_UHC_BUF_POOL_SIZE is too small for CDC-NCM. "
	     "Increase it to at least " STRINGIFY(CDC_NCM_UHC_BUF_POOL_MIN_SIZE) ".");

/*
 * Data path buffers hold a complete NTB in either direction. A single datagram
 * is packed into each outgoing NTB for now.
 */
USB_BUF_POOL_DEFINE(usbh_cdc_ncm_pool,
		    ((CDC_NCM_DATA_XFER_MIN_COUNT + CONFIG_NET_PKT_RX_COUNT) *
		     CONFIG_USBH_CDC_NCM_INSTANCES_COUNT),
		    CONFIG_USBH_CDC_NCM_MAX_NTB_SIZE, 0, NULL);

/* NCM 1.0 NTB layout used when one datagram is packed into a single NTB */
#define CDC_NCM_NTH16_SIZE sizeof(struct usb_cdc_ncm_nth16)
#define CDC_NCM_DPE_COUNT  2 /* One datagram entry plus the mandatory null entry */
#define CDC_NCM_NDP16_SIZE                                                                         \
	(sizeof(struct usb_cdc_ncm_ndp16) +                                                        \
	 CDC_NCM_DPE_COUNT * sizeof(struct usb_cdc_ncm_ndp16_dpe))
#define CDC_NCM_ETH_HDR_LEN sizeof(struct net_eth_hdr)
#define CDC_NCM_FCS_LEN     4

#define CDC_NCM_ALIGNMENT   4
#define CDC_NCM_MAX_NTB_SIZE ((uint32_t)CONFIG_USBH_CDC_NCM_MAX_NTB_SIZE)

/* Class-specific requests: type class, recipient interface */
#define CDC_NCM_REQTYPE_TO_HOST                                                                    \
	((USB_REQTYPE_DIR_TO_HOST << 7) | (USB_REQTYPE_TYPE_CLASS << 5) |                          \
	 USB_REQTYPE_RECIPIENT_INTERFACE)
#define CDC_NCM_REQTYPE_TO_DEVICE                                                                  \
	((USB_REQTYPE_DIR_TO_DEVICE << 7) | (USB_REQTYPE_TYPE_CLASS << 5) |                        \
	 USB_REQTYPE_RECIPIENT_INTERFACE)

#define CDC_NCM_DEVICE_FLAG_CONNECTED  BIT(0)
#define CDC_NCM_DEVICE_FLAG_FORWARDING BIT(1)

struct cdc_ncm_comm_descriptors {
	const struct usb_if_descriptor *iface;
	const struct cdc_header_descriptor *cdc_header;
	const struct cdc_union_descriptor *cdc_union;
	const struct cdc_ecm_descriptor *cdc_ecm;
	const struct cdc_ncm_descriptor *cdc_ncm;
	const struct usb_ep_descriptor *ep_in;
};

struct cdc_ncm_data_descriptors {
	const struct usb_if_descriptor *iface;
	const struct usb_ep_descriptor *ep_in;
	const struct usb_ep_descriptor *ep_out;
};

struct cdc_ncm_descriptors {
	struct cdc_ncm_comm_descriptors comm;
	struct cdc_ncm_data_descriptors data;
};

struct cdc_ncm_host_data {
	struct k_mutex mutex;
	atomic_t flags;
	struct usb_device *udev;
	struct cdc_ncm_descriptors desc;
	struct net_if *iface;
	uint16_t pkt_filter_bitmap;
	uint8_t bm_caps;
	uint16_t tx_seq;
	uint16_t rx_seq;
	uint32_t ntb_in_max_size;
	uint32_t ntb_out_max_size;
	uint16_t tx_max_datagrams;
	uint16_t tx_divisor;
	uint16_t tx_remainder;
	uint16_t ndp_out_alignment;
	struct uhc_transfer *comm_in_xfer;
	struct uhc_transfer *data_in_xfer[CONFIG_USBH_CDC_NCM_RX_PIPELINE_DEPTH];
	struct k_sem data_out_sem;
};

struct cdc_ncm_mcast_filter_ctx {
	struct net_buf *buf;
	uint16_t count;
};

#define CDC_NCM_DESC_COMM_IF_NUM(desc)     ((desc)->comm.iface->bInterfaceNumber)
#define CDC_NCM_DESC_COMM_EP_IN_ADDR(desc) ((desc)->comm.ep_in->bEndpointAddress)

#define CDC_NCM_DESC_DATA_IF_NUM(desc)      ((desc)->data.iface->bInterfaceNumber)
#define CDC_NCM_DESC_DATA_IF_ALT(desc)      ((desc)->data.iface->bAlternateSetting)
#define CDC_NCM_DESC_DATA_EP_IN_ADDR(desc)  ((desc)->data.ep_in->bEndpointAddress)
#define CDC_NCM_DESC_DATA_EP_OUT_ADDR(desc) ((desc)->data.ep_out->bEndpointAddress)
#define CDC_NCM_DESC_DATA_EP_OUT_MPS(desc)                                                         \
	((uint16_t)(sys_le16_to_cpu((desc)->data.ep_out->wMaxPacketSize) & 0x7FF))

#define CDC_NCM_DESC_MAC_ADDR_INDEX(desc) ((desc)->comm.cdc_ecm->iMACAddress)
#define CDC_NCM_DESC_MAX_SEGMENT_SIZE(desc)                                                        \
	((uint16_t)sys_le16_to_cpu((desc)->comm.cdc_ecm->wMaxSegmentSize))
#define CDC_NCM_DESC_MC_FILTER_COUNT(desc)                                                         \
	((uint16_t)(sys_le16_to_cpu((desc)->comm.cdc_ecm->wNumberMCFilters) & 0x7FFF))
#define CDC_NCM_DESC_MC_FILTER_IMPERFECT(desc)                                                     \
	((bool)((sys_le16_to_cpu((desc)->comm.cdc_ecm->wNumberMCFilters) & BIT(15)) != 0))
#define CDC_NCM_DESC_CAPS(desc) ((desc)->comm.cdc_ncm->bmNetworkCapabilities)

static struct usbh_class_filter cdc_ncm_filters[] = {
	{
		.flags = USBH_CLASS_MATCH_CODE_TRIPLE,
		.class = USB_BCC_CDC_CONTROL,
		.sub = NCM_SUBCLASS,
		.proto = 0,
	},
	{
		/* NCM Communications Interface with an external command set */
		.flags = USBH_CLASS_MATCH_CODE_TRIPLE,
		.class = USB_BCC_CDC_CONTROL,
		.sub = NCM_SUBCLASS,
		.proto = 0xFE,
	},
	{0},
};

static int interrupt_in_req_cb(struct usb_device *const udev, struct uhc_transfer *const xfer);
static int bulk_in_req_cb(struct usb_device *const udev, struct uhc_transfer *const xfer);
static int bulk_out_req_cb(struct usb_device *const udev, struct uhc_transfer *const xfer);

static bool cdc_ncm_is_power_of_two(const uint32_t value)
{
	return value != 0U && (value & (value - 1U)) == 0U;
}

static bool desc_is_valid_comm_iface(const void *const desc)
{
	const struct usb_if_descriptor *if_desc;

	if (!usbh_desc_is_valid_interface(desc)) {
		return false;
	}

	if_desc = (const struct usb_if_descriptor *)desc;

	if (if_desc->bInterfaceClass != USB_BCC_CDC_CONTROL) {
		return false;
	}

	if (if_desc->bInterfaceSubClass != NCM_SUBCLASS) {
		return false;
	}

	/* 0: no encapsulated commands, 0xFE: external (OEM) command set */
	if (if_desc->bInterfaceProtocol != 0 && if_desc->bInterfaceProtocol != 0xFE) {
		return false;
	}

	if (if_desc->bNumEndpoints != 1) {
		return false;
	}

	return true;
}

static bool desc_is_valid_data_iface(const void *const desc)
{
	const struct usb_if_descriptor *if_desc;

	if (!usbh_desc_is_valid_interface(desc)) {
		return false;
	}

	if_desc = (const struct usb_if_descriptor *)desc;

	if (if_desc->bInterfaceClass != USB_BCC_CDC_DATA) {
		return false;
	}

	if (if_desc->bInterfaceSubClass != 0) {
		return false;
	}

	/* NCM data interface protocol: Network Transfer Block (NCM100.pdf, 5.3) */
	if (if_desc->bInterfaceProtocol != NCM_DATA_PROTOCOL) {
		return false;
	}

	if (if_desc->bNumEndpoints != 2) {
		return false;
	}

	return true;
}

static bool desc_is_valid_cdc_header(const void *const desc)
{
	if (!usbh_desc_is_valid(desc, sizeof(struct cdc_header_descriptor),
				USB_DESC_CS_INTERFACE)) {
		return false;
	}

	return ((const struct cdc_header_descriptor *)desc)->bDescriptorSubtype == HEADER_FUNC_DESC;
}

static bool desc_is_valid_cdc_union(const void *const desc)
{
	if (!usbh_desc_is_valid(desc, sizeof(struct cdc_union_descriptor), USB_DESC_CS_INTERFACE)) {
		return false;
	}

	return ((const struct cdc_union_descriptor *)desc)->bDescriptorSubtype == UNION_FUNC_DESC;
}

static bool desc_is_valid_cdc_ecm_func(const void *const desc)
{
	if (!usbh_desc_is_valid(desc, sizeof(struct cdc_ecm_descriptor), USB_DESC_CS_INTERFACE)) {
		return false;
	}

	return ((const struct cdc_ecm_descriptor *)desc)->bDescriptorSubtype == ETHERNET_FUNC_DESC;
}

static bool desc_is_valid_cdc_ncm_func(const void *const desc)
{
	if (!usbh_desc_is_valid(desc, sizeof(struct cdc_ncm_descriptor), USB_DESC_CS_INTERFACE)) {
		return false;
	}

	return ((const struct cdc_ncm_descriptor *)desc)->bDescriptorSubtype ==
	       ETHERNET_FUNC_DESC_NCM;
}

static bool comm_desc_is_valid(const struct cdc_ncm_comm_descriptors *const comm_desc)
{
	if (comm_desc->iface == NULL) {
		LOG_ERR("Failed to get NCM communication interface descriptor");
		return false;
	}

	if (comm_desc->cdc_header == NULL) {
		LOG_ERR("Failed to get CDC Header Functional Descriptor");
		return false;
	}

	if (comm_desc->cdc_union == NULL) {
		LOG_ERR("Failed to get CDC Union Functional Descriptor");
		return false;
	}

	if (comm_desc->cdc_ecm == NULL) {
		LOG_ERR("Failed to get CDC Ethernet Networking Functional Descriptor");
		return false;
	}

	if (comm_desc->cdc_ncm == NULL) {
		LOG_ERR("Failed to get NCM Functional Descriptor");
		return false;
	}

	if (comm_desc->ep_in == NULL) {
		LOG_ERR("Failed to get NCM communication endpoint descriptor");
		return false;
	}

	if (comm_desc->cdc_union->bFunctionLength != 5) {
		LOG_ERR("Not supported CDC Union Functional Descriptor length %u (only 1 "
			"subordinate interface supported)",
			comm_desc->cdc_union->bFunctionLength);
		return false;
	}

	if (comm_desc->cdc_union->bControlInterface != comm_desc->iface->bInterfaceNumber) {
		LOG_ERR("CDC Union Functional Descriptor bControlInterface does not match "
			"NCM communication interface number");
		return false;
	}

	if (comm_desc->cdc_ecm->iMACAddress == 0) {
		LOG_DBG("NCM function does not provide a permanent MAC address string");
	}

	return true;
}

static bool data_desc_is_valid(const struct cdc_ncm_data_descriptors *const data_desc)
{
	if (data_desc->iface == NULL) {
		LOG_ERR("Failed to get NCM data interface descriptor");
		return false;
	}

	if (data_desc->ep_in == NULL || data_desc->ep_out == NULL) {
		LOG_ERR("Failed to get NCM data endpoint descriptor");
		return false;
	}

	if (data_desc->ep_out->wMaxPacketSize == 0) {
		LOG_ERR("NCM data OUT endpoint has invalid wMaxPacketSize of 0");
		return false;
	}

	return true;
}

static void parse_comm_if_desc(struct cdc_ncm_comm_descriptors *const comm_desc,
			       const struct usb_desc_header *const desc)
{
	const struct usb_if_descriptor *if_desc = (const struct usb_if_descriptor *)desc;

	if (!desc_is_valid_comm_iface(desc)) {
		return;
	}

	/* Keep the interface descriptor with the highest alternate setting. */
	if (comm_desc->iface == NULL ||
	    if_desc->bAlternateSetting > comm_desc->iface->bAlternateSetting) {
		comm_desc->iface = if_desc;
		comm_desc->cdc_header = NULL;
		comm_desc->cdc_union = NULL;
		comm_desc->cdc_ecm = NULL;
		comm_desc->cdc_ncm = NULL;
		comm_desc->ep_in = NULL;
	}
}

static void parse_comm_cs_if_desc(struct cdc_ncm_comm_descriptors *const comm_desc,
				  const struct usb_desc_header *const desc)
{
	if (comm_desc->iface == NULL) {
		return;
	}

	if (desc_is_valid_cdc_header(desc)) {
		comm_desc->cdc_header = (const struct cdc_header_descriptor *)desc;
	} else if (desc_is_valid_cdc_union(desc)) {
		comm_desc->cdc_union = (const struct cdc_union_descriptor *)desc;
	} else if (desc_is_valid_cdc_ecm_func(desc)) {
		comm_desc->cdc_ecm = (const struct cdc_ecm_descriptor *)desc;
	} else if (desc_is_valid_cdc_ncm_func(desc)) {
		comm_desc->cdc_ncm = (const struct cdc_ncm_descriptor *)desc;
	} else {
		LOG_DBG("Unknown CDC class-specific Interface descriptor subtype (0x%02x)",
			((const struct cdc_header_descriptor *)desc)->bDescriptorSubtype);
	}
}

static void parse_comm_ep_desc(struct cdc_ncm_comm_descriptors *const comm_desc,
			       const struct usb_desc_header *const desc)
{
	const struct usb_ep_descriptor *ep_desc;

	if (comm_desc->iface == NULL) {
		return;
	}

	if (usbh_desc_is_valid_endpoint(desc)) {
		ep_desc = (const struct usb_ep_descriptor *)desc;
		if ((ep_desc->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) != USB_EP_TYPE_INTERRUPT) {
			return;
		}

		if (USB_EP_DIR_IS_IN(ep_desc->bEndpointAddress)) {
			comm_desc->ep_in = (const struct usb_ep_descriptor *)desc;
		}
	}
}

static int parse_comm_descriptors(struct cdc_ncm_host_data *const host_data, const uint8_t iface)
{
	struct cdc_ncm_comm_descriptors comm_desc = {0};
	bool alt_is_supported = false;
	const struct usb_if_descriptor *if_desc;
	const struct usb_desc_header *desc;

	desc = usbh_desc_get_iface(host_data->udev, iface);
	if (desc == NULL) {
		return -EINVAL;
	}

	for (; desc != NULL; desc = usbh_desc_get_next(desc)) {
		switch (desc->bDescriptorType) {
		case USB_DESC_INTERFACE:
			if_desc = (const struct usb_if_descriptor *)desc;
			if (if_desc->bInterfaceNumber != iface) {
				goto parse_done;
			}

			/*
			 * Only collect descriptors of the alternate settings that are valid for
			 * this class. Other settings of the same function (e.g. the MBIM
			 * alternate setting of an NCM/MBIM function) must be skipped together
			 * with their endpoints.
			 */
			alt_is_supported = desc_is_valid_comm_iface(desc);
			if (alt_is_supported) {
				parse_comm_if_desc(&comm_desc, desc);
			}
			break;

		case USB_DESC_CS_INTERFACE:
			if (alt_is_supported) {
				parse_comm_cs_if_desc(&comm_desc, desc);
			}
			break;

		case USB_DESC_ENDPOINT:
			if (alt_is_supported) {
				parse_comm_ep_desc(&comm_desc, desc);
			}
			break;

		default:
			break;
		}
	}

parse_done:
	host_data->desc.comm = comm_desc;

	return 0;
}

static void parse_data_if_desc(struct cdc_ncm_data_descriptors *const data_desc,
			       const struct usb_desc_header *const desc)
{
	const struct usb_if_descriptor *if_desc = (const struct usb_if_descriptor *)desc;

	if (!desc_is_valid_data_iface(desc)) {
		return;
	}

	/* Keep the interface descriptor with the highest alternate setting. */
	if (data_desc->iface == NULL ||
	    if_desc->bAlternateSetting > data_desc->iface->bAlternateSetting) {
		data_desc->iface = if_desc;
		data_desc->ep_in = NULL;
		data_desc->ep_out = NULL;
	}
}

static void parse_data_ep_desc(struct cdc_ncm_data_descriptors *const data_desc,
			       const struct usb_desc_header *const desc)
{
	const struct usb_ep_descriptor *ep_desc;

	if (data_desc->iface == NULL) {
		return;
	}

	if (usbh_desc_is_valid_endpoint(desc)) {
		ep_desc = (const struct usb_ep_descriptor *)desc;
		if ((ep_desc->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) != USB_EP_TYPE_BULK) {
			return;
		}

		if (USB_EP_DIR_IS_IN(ep_desc->bEndpointAddress)) {
			data_desc->ep_in = ep_desc;
		} else {
			data_desc->ep_out = ep_desc;
		}
	}
}

static int parse_data_descriptors(struct cdc_ncm_host_data *const host_data, const uint8_t iface)
{
	struct cdc_ncm_data_descriptors data_desc = {0};
	bool alt_is_supported = false;
	const struct usb_if_descriptor *if_desc;
	const struct usb_desc_header *desc;

	desc = usbh_desc_get_iface(host_data->udev, iface);
	if (desc == NULL) {
		return -EINVAL;
	}

	for (; desc != NULL; desc = usbh_desc_get_next(desc)) {
		switch (desc->bDescriptorType) {
		case USB_DESC_INTERFACE:
			if_desc = (const struct usb_if_descriptor *)desc;
			if (if_desc->bInterfaceNumber != iface) {
				goto parse_done;
			}

			/* Skip the alternate settings without the bulk endpoints */
			alt_is_supported = desc_is_valid_data_iface(desc);
			if (alt_is_supported) {
				parse_data_if_desc(&data_desc, desc);
			}
			break;

		case USB_DESC_ENDPOINT:
			if (alt_is_supported) {
				parse_data_ep_desc(&data_desc, desc);
			}
			break;

		default:
			break;
		}
	}

parse_done:
	host_data->desc.data = data_desc;

	return 0;
}

static int parse_descriptors(struct cdc_ncm_host_data *const host_data, const uint8_t iface)
{
	const struct usb_association_descriptor *iad_desc =
		(const struct usb_association_descriptor *)usbh_desc_get_iad(host_data->udev,
									     iface);
	const uint8_t comm_iface = (iad_desc != NULL) ? iad_desc->bFirstInterface : iface;
	const struct cdc_union_descriptor *cdc_union_desc;
	int ret;

	ret = parse_comm_descriptors(host_data, comm_iface);
	if (ret != 0) {
		LOG_ERR("Failed to parse NCM communication interface %u descriptor", comm_iface);
		return ret;
	}

	if (!comm_desc_is_valid(&host_data->desc.comm)) {
		return -EBADMSG;
	}

	cdc_union_desc = host_data->desc.comm.cdc_union;
	if (cdc_union_desc == NULL) {
		return -EBADMSG;
	}

	ret = parse_data_descriptors(host_data, cdc_union_desc->bSubordinateInterface0);
	if (ret != 0) {
		LOG_ERR("Failed to parse NCM data interface %u descriptor",
			cdc_union_desc->bSubordinateInterface0);
		return ret;
	}

	if (!data_desc_is_valid(&host_data->desc.data)) {
		return -EBADMSG;
	}

	return 0;
}

static void reset_states(struct cdc_ncm_host_data *const host_data)
{
	host_data->udev = NULL;
	host_data->pkt_filter_bitmap = 0;
	host_data->bm_caps = 0;
	host_data->tx_seq = 0;
	host_data->rx_seq = 0;
	host_data->ntb_in_max_size = CDC_NCM_MAX_NTB_SIZE;
	host_data->ntb_out_max_size = CDC_NCM_MAX_NTB_SIZE;
	host_data->tx_max_datagrams = 0;
	host_data->tx_divisor = CDC_NCM_ALIGNMENT;
	host_data->tx_remainder = 0;
	host_data->ndp_out_alignment = CDC_NCM_ALIGNMENT;

	memset(&host_data->desc, 0, sizeof(struct cdc_ncm_descriptors));
}

static int parse_mac_address_string(const struct net_buf *const desc_buf,
				    struct net_eth_addr *const eth_mac)
{
	char mac_str[CDC_NCM_MAC_ADDR_CHARS + 1];
	int ret;

	ret = usbh_desc_str_to_ascii(desc_buf->data, desc_buf->len, mac_str, ARRAY_SIZE(mac_str));
	if (ret != 0) {
		LOG_DBG("Failed to convert MAC address to ASCII encoded string");
		return ret;
	}

	if (hex2bin(mac_str, strlen(mac_str), eth_mac->addr, NET_ETH_ADDR_LEN) !=
	    NET_ETH_ADDR_LEN) {
		LOG_DBG("Failed to parse MAC address string (%s)", mac_str);
		return -EINVAL;
	}

	if (!net_eth_is_addr_valid(eth_mac)) {
		LOG_DBG("Invalid MAC address (%s)", mac_str);
		return -EINVAL;
	}

	LOG_DBG("Parse MAC address success");
	return 0;
}

static int get_valid_mac_address(const struct cdc_ncm_host_data *const host_data,
				 const uint16_t lang_id, struct net_eth_addr *const eth_mac)
{
	struct net_buf *buf;
	int ret;

	buf = usbh_xfer_buf_alloc(host_data->udev, CDC_NCM_MAC_STRING_DESC_REQ_BUF_SIZE);
	if (buf == NULL) {
		return -ENOMEM;
	}

	ret = usbh_req_desc_str(host_data->udev, CDC_NCM_DESC_MAC_ADDR_INDEX(&host_data->desc),
				lang_id, buf);
	if (ret == 0) {
		ret = parse_mac_address_string(buf, eth_mac);
	}

	usbh_xfer_buf_free(host_data->udev, buf);

	return ret;
}

static int get_supported_lang_ids(const struct cdc_ncm_host_data *const host_data,
				  uint16_t *const lang_ids, const uint8_t lang_ids_len,
				  uint8_t *const count)
{
	struct net_buf *buf;
	uint8_t lang_id_count;
	int ret;

	buf = usbh_xfer_buf_alloc(host_data->udev, CDC_NCM_LANGID_STRING_DESC_REQ_BUF_SIZE);
	if (buf == NULL) {
		return -ENOMEM;
	}

	ret = usbh_req_desc_str(host_data->udev, 0, 0, buf);
	if (ret != 0) {
		goto cleanup;
	}

	ret = usbh_desc_get_lang_ids(buf->data, buf->len, lang_ids, lang_ids_len);
	if (ret < 0) {
		goto cleanup;
	}

	lang_id_count = ret;
	if (lang_id_count == 0) {
		ret = -ENODATA;
		goto cleanup;
	}

	if (count != NULL) {
		*count = MIN(lang_id_count, lang_ids_len);
	}

cleanup:
	usbh_xfer_buf_free(host_data->udev, buf);

	return ret;
}

static int get_mac_address_from_string(const struct cdc_ncm_host_data *const host_data,
				       struct net_eth_addr *const eth_mac)
{
	uint16_t lang_ids[CDC_NCM_MAX_SUPPORTED_LANGID_COUNT];
	uint8_t lang_id_count = 0;
	int ret;

	ret = get_valid_mac_address(host_data, CONFIG_USBH_CDC_NCM_DEFAULT_MAC_ADDR_UNICODE_LANGID,
				    eth_mac);
	if (ret == 0) {
		return 0;
	}

	LOG_WRN("Failed to get MAC address string descriptor with default LANGID (0x%04x), trying "
		"alternatives",
		CONFIG_USBH_CDC_NCM_DEFAULT_MAC_ADDR_UNICODE_LANGID);

	ret = get_supported_lang_ids(host_data, lang_ids, ARRAY_SIZE(lang_ids), &lang_id_count);
	if (ret != 0) {
		LOG_ERR("Failed to get supported language IDs: %d", ret);
		return ret;
	}

	ret = -ENOENT;
	for (unsigned int i = 0; i < lang_id_count; i++) {
		if (lang_ids[i] == CONFIG_USBH_CDC_NCM_DEFAULT_MAC_ADDR_UNICODE_LANGID) {
			continue;
		}

		ret = get_valid_mac_address(host_data, lang_ids[i], eth_mac);
		if (ret == 0) {
			return 0;
		}
	}

	LOG_ERR("Not found available MAC address");
	return ret;
}

static int get_mac_address_from_net_address(struct cdc_ncm_host_data *const host_data,
					    struct net_eth_addr *const eth_mac)
{
	struct net_buf *buf;
	int ret;

	if (!(host_data->bm_caps & USB_CDC_NCM_NCAP_NET_ADDRESS)) {
		return -ENOTSUP;
	}

	buf = usbh_xfer_buf_alloc(host_data->udev, CDC_NCM_NET_ADDR_REQ_BUF_SIZE);
	if (buf == NULL) {
		return -ENOMEM;
	}

	ret = usbh_req_setup(host_data->udev, CDC_NCM_REQTYPE_TO_HOST, GET_NET_ADDRESS, 0,
			     CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc),
			     CDC_NCM_NET_ADDR_REQ_BUF_SIZE, buf);
	if (ret == 0) {
		if (buf->len < NET_ETH_ADDR_LEN) {
			ret = -EMSGSIZE;
		} else {
			memcpy(eth_mac->addr, buf->data, NET_ETH_ADDR_LEN);
			if (!net_eth_is_addr_valid(eth_mac)) {
				ret = -EINVAL;
			}
		}
	}

	usbh_xfer_buf_free(host_data->udev, buf);

	return ret;
}

static int get_mac_address(struct cdc_ncm_host_data *const host_data)
{
	struct net_eth_addr eth_mac;
	int ret = -ENOTSUP;

	if (CDC_NCM_DESC_MAC_ADDR_INDEX(&host_data->desc) != 0) {
		ret = get_mac_address_from_string(host_data, &eth_mac);
	}

	if (ret != 0) {
		ret = get_mac_address_from_net_address(host_data, &eth_mac);
	}

	if (ret != 0) {
		/* Keep the random locally administered address set at interface init */
		LOG_WRN("Failed to retrieve a MAC address (%d), keeping the random one", ret);
		return 0;
	}

	ret = net_if_set_link_addr(host_data->iface, eth_mac.addr, NET_ETH_ADDR_LEN,
				   NET_LINK_ETHERNET);
	if (ret != 0) {
		LOG_ERR("Failed to set MAC address: %d", ret);
	}

	return ret;
}

static int ntb_req_set_value(struct cdc_ncm_host_data *const host_data, const uint8_t request,
			     const uint16_t value)
{
	return usbh_req_setup(host_data->udev, CDC_NCM_REQTYPE_TO_DEVICE, request, value,
			      CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc), 0, NULL);
}

static int ntb_req_set_data(struct cdc_ncm_host_data *const host_data, const uint8_t request,
			    const void *const data, const size_t len)
{
	struct net_buf *buf;
	int ret;

	buf = usbh_xfer_buf_alloc(host_data->udev, len);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_add_mem(buf, data, len);

	ret = usbh_req_setup(host_data->udev, CDC_NCM_REQTYPE_TO_DEVICE, request, 0,
			     CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc), len, buf);

	usbh_xfer_buf_free(host_data->udev, buf);

	return ret;
}

static int get_ntb_parameters(struct cdc_ncm_host_data *const host_data,
			      struct usb_cdc_ncm_ntb_parameters *const params)
{
	struct net_buf *buf;
	int ret;

	buf = usbh_xfer_buf_alloc(host_data->udev, sizeof(*params));
	if (buf == NULL) {
		return -ENOMEM;
	}

	ret = usbh_req_setup(host_data->udev, CDC_NCM_REQTYPE_TO_HOST, GET_NTB_PARAMETERS, 0,
			     CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc), sizeof(*params), buf);
	if (ret == 0) {
		if (buf->len < sizeof(*params)) {
			LOG_ERR("GetNtbParameters returned only %u bytes",
				(unsigned int)buf->len);
			ret = -EMSGSIZE;
		} else {
			memcpy(params, buf->data, sizeof(*params));
		}
	}

	usbh_xfer_buf_free(host_data->udev, buf);

	if (ret == 0) {
		params->wLength = sys_le16_to_cpu(params->wLength);
		params->bmNtbFormatsSupported =
			sys_le16_to_cpu(params->bmNtbFormatsSupported);
		params->dwNtbInMaxSize = sys_le32_to_cpu(params->dwNtbInMaxSize);
		params->wNdpInDivisor = sys_le16_to_cpu(params->wNdpInDivisor);
		params->wNdpInPayloadRemainder =
			sys_le16_to_cpu(params->wNdpInPayloadRemainder);
		params->wNdpInAlignment = sys_le16_to_cpu(params->wNdpInAlignment);
		params->wReserved = sys_le16_to_cpu(params->wReserved);
		params->dwNtbOutMaxSize = sys_le32_to_cpu(params->dwNtbOutMaxSize);
		params->wNdpOutDivisor = sys_le16_to_cpu(params->wNdpOutDivisor);
		params->wNdpOutPayloadRemainder =
			sys_le16_to_cpu(params->wNdpOutPayloadRemainder);
		params->wNdpOutAlignment = sys_le16_to_cpu(params->wNdpOutAlignment);
		params->wNtbOutMaxDatagrams =
			sys_le16_to_cpu(params->wNtbOutMaxDatagrams);
	}

	return ret;
}

static int set_ntb_input_size(struct cdc_ncm_host_data *const host_data, const uint32_t size,
			      const uint16_t max_datagrams)
{
	struct usb_cdc_ncm_ntb_input_size input = {
		.dwNtbInMaxSize = sys_cpu_to_le32(size),
		.wNtbInMaxDatagrams = sys_cpu_to_le16(max_datagrams),
		.wReserved = 0,
	};
	size_t len;

	/* The 8-byte form is only valid if the function supports it */
	if (host_data->bm_caps & USB_CDC_NCM_NCAP_NTB_INPUT_SIZE) {
		len = sizeof(input);
	} else {
		len = sizeof(input.dwNtbInMaxSize);
	}

	return ntb_req_set_data(host_data, SET_NTB_INPUT_SIZE, &input, len);
}

static int set_max_datagram_size(struct cdc_ncm_host_data *const host_data, const uint16_t size)
{
	uint16_t value = sys_cpu_to_le16(size);

	return ntb_req_set_data(host_data, SET_MAX_DATAGRAM_SIZE, &value, sizeof(value));
}

static int configure_ntb(struct cdc_ncm_host_data *const host_data)
{
	struct usb_cdc_ncm_ntb_parameters params;
	uint32_t in_max, out_max;
	uint16_t divisor, remainder, alignment;
	int ret;

	ret = get_ntb_parameters(host_data, &params);
	if (ret != 0) {
		LOG_ERR("Failed to get NTB parameters: %d", ret);
		return ret;
	}

	if (!(params.bmNtbFormatsSupported & USB_CDC_NCM_NTB16_SUPPORTED)) {
		LOG_ERR("The device does not support 16-bit NTBs");
		return -ENOTSUP;
	}

	/* Select 16-bit NTBs if the function supports both formats */
	if (params.bmNtbFormatsSupported & USB_CDC_NCM_NTB32_SUPPORTED) {
		ret = ntb_req_set_value(host_data, SET_NTB_FORMAT, USB_CDC_NCM_NTB16_FORMAT);
		if (ret != 0) {
			LOG_ERR("Failed to select the 16-bit NTB format: %d", ret);
			return ret;
		}
	}

	/*
	 * Tell the function the maximum size of the NTBs it may send to the host.
	 * The host shall select at least USB_CDC_NCM_NTB_INPUT_SIZE_MIN bytes and
	 * not more than the function supports.
	 */
	in_max = MIN(CDC_NCM_MAX_NTB_SIZE, params.dwNtbInMaxSize);
	if (in_max < USB_CDC_NCM_NTB_INPUT_SIZE_MIN) {
		LOG_WRN("dwNtbInMaxSize (%u) is too small, using %u", params.dwNtbInMaxSize,
			USB_CDC_NCM_NTB_INPUT_SIZE_MIN);
		in_max = USB_CDC_NCM_NTB_INPUT_SIZE_MIN;
	}

	ret = set_ntb_input_size(host_data, in_max, 0);
	if (ret != 0) {
		LOG_ERR("Failed to set the NTB input size: %d", ret);
		return ret;
	}

	host_data->ntb_in_max_size = in_max;

	/*
	 * Limit the size of the NTBs sent by the host. dwNtbOutMaxSize may be
	 * zero when the function imposes no limit.
	 */
	out_max = params.dwNtbOutMaxSize;
	if (out_max == 0U) {
		out_max = CDC_NCM_MAX_NTB_SIZE;
	}

	host_data->ntb_out_max_size = MIN(out_max, CDC_NCM_MAX_NTB_SIZE);

	/* Sanitize the OUT datagram alignment preferences, the host shall honor them */
	divisor = params.wNdpOutDivisor;
	remainder = params.wNdpOutPayloadRemainder;
	if (divisor < CDC_NCM_ALIGNMENT || !cdc_ncm_is_power_of_two(divisor) ||
	    divisor >= host_data->ntb_out_max_size) {
		LOG_DBG("Using default OUT payload alignment: divisor 4, remainder 0");
		divisor = CDC_NCM_ALIGNMENT;
		remainder = 0;
	}

	if (remainder >= divisor) {
		LOG_DBG("Invalid OUT payload remainder %u, using 0", remainder);
		remainder = 0;
	}

	host_data->tx_divisor = divisor;
	host_data->tx_remainder = remainder;

	alignment = params.wNdpOutAlignment;
	if (alignment < CDC_NCM_ALIGNMENT || !cdc_ncm_is_power_of_two(alignment) ||
	    alignment >= host_data->ntb_out_max_size) {
		alignment = CDC_NCM_ALIGNMENT;
	}

	host_data->ndp_out_alignment = alignment;

	/* Zero means that the function imposes no limit on datagrams per NTB */
	host_data->tx_max_datagrams = params.wNtbOutMaxDatagrams;

	/* Optionally negotiate the maximum datagram size */
	if (host_data->bm_caps & USB_CDC_NCM_NCAP_MAX_DATAGRAM_SIZE) {
		uint16_t seg_max = MIN(CDC_NCM_DESC_MAX_SEGMENT_SIZE(&host_data->desc),
				       CONFIG_USBH_CDC_NCM_MAX_SEGMENT_SIZE);

		if (seg_max >= 1514) {
			ret = set_max_datagram_size(host_data, seg_max);
			if (ret != 0) {
				LOG_WRN("Failed to set the maximum datagram size: %d", ret);
			}
		}
	} else if (CDC_NCM_DESC_MAX_SEGMENT_SIZE(&host_data->desc) >
		   CONFIG_USBH_CDC_NCM_MAX_SEGMENT_SIZE) {
		LOG_WRN("Device may send datagrams up to %u bytes, host supports %u",
			CDC_NCM_DESC_MAX_SEGMENT_SIZE(&host_data->desc),
			CONFIG_USBH_CDC_NCM_MAX_SEGMENT_SIZE);
	}

	/* Keep datagrams free of the L2 FCS if the function lets the host choose */
	if (host_data->bm_caps & USB_CDC_NCM_NCAP_CRC_MODE) {
		ret = ntb_req_set_value(host_data, SET_CRC_MODE, USB_CDC_NCM_CRC_NOT_APPENDED);
		if (ret != 0) {
			LOG_WRN("Failed to disable the CRC mode: %d", ret);
		}
	}

	LOG_DBG("NTB parameters: formats 0x%02x, IN max %u, OUT max %u, "
		"OUT payload align [divisor %u remainder %u], NDP align %u, OUT max datagrams %u",
		params.bmNtbFormatsSupported, host_data->ntb_in_max_size,
		host_data->ntb_out_max_size, host_data->tx_divisor,
		host_data->tx_remainder, host_data->ndp_out_alignment,
		host_data->tx_max_datagrams);

	return 0;
}

static int set_packet_filter(struct cdc_ncm_host_data *const host_data, const uint16_t bitmap,
			     const bool enable)
{
	struct usb_device *udev;
	uint16_t current_bitmap;
	uint16_t updated_bitmap;
	int ret;

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED)) {
		return -ENODEV;
	}

	udev = host_data->udev;
	if (udev == NULL) {
		return -ENODEV;
	}

	current_bitmap = host_data->pkt_filter_bitmap;
	if (enable) {
		updated_bitmap = current_bitmap | bitmap;
	} else {
		updated_bitmap = current_bitmap & ~bitmap;
	}

	if (updated_bitmap == current_bitmap) {
		LOG_DBG("Packet filter unchanged (0x%04x)", current_bitmap);
		return 0;
	}

	ret = usbh_req_setup(udev,
			     (USB_REQTYPE_DIR_TO_DEVICE << 7) | (USB_REQTYPE_TYPE_CLASS << 5) |
				     USB_REQTYPE_RECIPIENT_INTERFACE,
			     SET_ETHERNET_PACKET_FILTER, updated_bitmap,
			     CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc), 0, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to set Ethernet Packet Filter (0x%04x -> 0x%04x): %d",
			current_bitmap, updated_bitmap, ret);
		return ret;
	}

	host_data->pkt_filter_bitmap = updated_bitmap;

	LOG_DBG("Packet filter updated: 0x%04x -> 0x%04x", current_bitmap, updated_bitmap);
	return 0;
}

static void collect_mcast_addr(struct net_if *iface, const struct net_eth_mcast_addr *addr,
			       void *user_data)
{
	struct cdc_ncm_mcast_filter_ctx *ctx = (struct cdc_ncm_mcast_filter_ctx *)user_data;

	ARG_UNUSED(iface);

	if (ctx->buf != NULL) {
		net_buf_add_mem(ctx->buf, addr->addr.addr, NET_ETH_ADDR_LEN);
	}

	ctx->count++;
}

static int set_multicast_filters(struct cdc_ncm_host_data *const host_data)
{
	struct cdc_ncm_mcast_filter_ctx ctx = {0};
	struct usb_device *udev;
	struct net_buf *buf = NULL;
	uint16_t supported_filters;
	bool receive_all_multicast = false;
	bool receive_multicast = false;
	bool update_mcast_filter = false;
	int ret = 0;

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED)) {
		return -ENODEV;
	}

	udev = host_data->udev;
	if (udev == NULL) {
		return -ENODEV;
	}

	if (!(host_data->bm_caps & USB_CDC_NCM_NCAP_ETH_FILTER)) {
		/* Without packet filtering the function always forwards everything */
		return 0;
	}

	supported_filters = CDC_NCM_DESC_MC_FILTER_COUNT(&host_data->desc);
	if (supported_filters > 0) {
		buf = usbh_xfer_buf_alloc(udev,
					  CDC_NCM_SET_ETHERNET_MULTICAST_FILTER_REQ_BUF_SIZE);
		if (buf == NULL) {
			return -ENOMEM;
		}

		ctx.buf = buf;
	}

	net_eth_mcast_addr_foreach(host_data->iface, collect_mcast_addr, &ctx);

	/*
	 * Decide how the device should receive multicast:
	 * - update_mcast_filter: the filter list is reprogrammed, either with the joined
	 *   addresses or cleared once the last multicast group is left;
	 * - receive_multicast: the joined groups fit into the device filter list, so only
	 *   the listed addresses are received;
	 * - receive_all_multicast: there is no filter list, or more groups are joined than
	 *   the device supports, so all multicast is received instead of failing.
	 */
	update_mcast_filter = supported_filters > 0 && ctx.count <= supported_filters;
	receive_multicast = ctx.count > 0 && update_mcast_filter;
	receive_all_multicast = ctx.count > 0 && !receive_multicast;

	ret = set_packet_filter(host_data, PACKET_TYPE_ALL_MULTICAST, receive_all_multicast);
	if (ret != 0) {
		goto done;
	}

	ret = set_packet_filter(host_data, PACKET_TYPE_MULTICAST, receive_multicast);
	if (ret != 0) {
		goto done;
	}

	if (update_mcast_filter) {
		/*
		 * Without any joined group the request only clears the filter list,
		 * so it carries no data stage.
		 */
		ret = usbh_req_setup(udev,
				     (USB_REQTYPE_DIR_TO_DEVICE << 7) |
					     (USB_REQTYPE_TYPE_CLASS << 5) |
					     USB_REQTYPE_RECIPIENT_INTERFACE,
				     SET_ETHERNET_MULTICAST_FILTERS, ctx.count,
				     CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc),
				     ctx.count * NET_ETH_ADDR_LEN,
				     (ctx.count > 0) ? buf : NULL);
		if (ret != 0) {
			LOG_ERR("Failed to set multicast filters (count=%u): %d", ctx.count, ret);
			goto done;
		}

		LOG_DBG("Multicast filters updated (count=%u)", ctx.count);
	}

done:
	if (buf != NULL) {
		usbh_xfer_buf_free(udev, buf);
	}

	return ret;
}

static int initialize_interrupt_in_xfer(struct cdc_ncm_host_data *const host_data)
{
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	int ret;

	xfer = usbh_xfer_alloc(host_data->udev, CDC_NCM_DESC_COMM_EP_IN_ADDR(&host_data->desc),
			       interrupt_in_req_cb, host_data);
	if (xfer == NULL) {
		LOG_ERR("Failed to allocate interrupt IN transfer");
		return -ENOMEM;
	}

	buf = usbh_xfer_buf_alloc(host_data->udev, CDC_NCM_NOTIF_BUF_MAX_SIZE);
	if (buf == NULL) {
		LOG_ERR("Failed to allocate buffer for interrupt IN transfer");
		usbh_xfer_free(host_data->udev, xfer);
		return -ENOMEM;
	}

	ret = usbh_xfer_buf_add(host_data->udev, xfer, buf);
	if (ret != 0) {
		LOG_ERR("Failed to add buffer to interrupt IN transfer: %d", ret);
		usbh_xfer_free(host_data->udev, xfer);
		net_buf_unref(buf);
		return ret;
	}

	host_data->comm_in_xfer = xfer;

	return 0;
}

static void release_xfer(struct cdc_ncm_host_data *const host_data, struct uhc_transfer *const xfer)
{
	if (xfer->buf != NULL) {
		net_buf_unref(xfer->buf);
	}

	usbh_xfer_free(host_data->udev, xfer);
}

static void deinitialize_interrupt_in_xfer(struct cdc_ncm_host_data *const host_data)
{
	struct uhc_transfer *xfer = host_data->comm_in_xfer;
	int err;

	if (xfer == NULL) {
		return;
	}

	host_data->comm_in_xfer = NULL;

	if (xfer->queued) {
		err = usbh_xfer_dequeue(host_data->udev, xfer);
		if (err == 0) {
			/* Dequeued; the completion callback frees the transfer. */
			return;
		}
		LOG_ERR("Failed to dequeue interrupt IN transfer: %d", err);
	}

	/*
	 * Never enqueued, or dequeue failed: the completion callback will not fire, so the
	 * transfer and its buffer are released here.
	 */
	release_xfer(host_data, xfer);
}

static int start_interrupt_in_xfer(struct cdc_ncm_host_data *const host_data)
{
	int ret;

	ret = usbh_xfer_enqueue(host_data->udev, host_data->comm_in_xfer);
	if (ret != 0) {
		LOG_ERR("Failed to start interrupt IN transfer");
		return ret;
	}

	return 0;
}

static void parse_notifications(struct cdc_ncm_host_data *const host_data,
				const struct net_buf *const buf)
{
	const struct usb_setup_packet *notif;

	notif = (const struct usb_setup_packet *)buf->data;
	switch (notif->bRequest) {
	case USB_CDC_NETWORK_CONNECTION:
		if (buf->len != sizeof(struct usb_setup_packet)) {
			LOG_ERR("Wrong CDC Network Connection message");
			return;
		}

		if (sys_le16_to_cpu(notif->wValue) == 1) {
			net_eth_carrier_on(host_data->iface);
		} else if (sys_le16_to_cpu(notif->wValue) == 0) {
			net_eth_carrier_off(host_data->iface);
		} else {
			LOG_WRN("Unknown CDC Network Connection value 0x%02x",
				sys_le16_to_cpu(notif->wValue));
		}
		break;

	case USB_CDC_CONNECTION_SPEED_CHANGE:
		if (buf->len != sizeof(struct usb_setup_packet) + 8) {
			LOG_ERR("Wrong CDC Connection Speed Change message");
			return;
		}
		break;

	case USB_CDC_RESPONSE_AVAILABLE:
		/* Not used by NCM */
		break;

	default:
		LOG_WRN("Unknown CDC Notification bRequest: 0x%02x", notif->bRequest);
		break;
	}
}

static int interrupt_in_req_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct cdc_ncm_host_data *host_data = xfer->priv;
	struct net_buf *buf = xfer->buf;
	int ret = 0;

	if (xfer->err == -ECONNRESET) {
		LOG_INF("The interrupt IN transfer is cancelled");
		net_buf_unref(buf);
		usbh_xfer_free(udev, xfer);
		goto done;
	}

	if (xfer->err == 0 && buf->len >= sizeof(struct usb_setup_packet)) {
		parse_notifications(host_data, buf);
	}

	net_buf_reset(buf);

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED) ||
	    !atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		goto done;
	}

	ret = usbh_xfer_enqueue(udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to continue interrupt IN transfer: %d", ret);
	}

done:
	return ret;
}

static int initialize_bulk_in_xfer(struct cdc_ncm_host_data *const host_data)
{
	struct net_buf *buf;
	int ret;

	for (unsigned int i = 0; i < ARRAY_SIZE(host_data->data_in_xfer); i++) {
		host_data->data_in_xfer[i] = usbh_xfer_alloc(
			host_data->udev, CDC_NCM_DESC_DATA_EP_IN_ADDR(&host_data->desc),
			bulk_in_req_cb, host_data);
		if (host_data->data_in_xfer[i] == NULL) {
			LOG_ERR("Failed to allocate bulk IN transfer %u", i);
			ret = -ENOMEM;
			goto cleanup;
		}

		buf = net_buf_alloc(&usbh_cdc_ncm_pool, K_NO_WAIT);
		if (buf == NULL) {
			LOG_ERR("Failed to allocate buffer for bulk IN transfer %u", i);
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = usbh_xfer_buf_add(host_data->udev, host_data->data_in_xfer[i], buf);
		if (ret != 0) {
			LOG_ERR("Failed to add buffer to bulk IN transfer %u: %d", i, ret);
			net_buf_unref(buf);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	for (unsigned int i = 0; i < ARRAY_SIZE(host_data->data_in_xfer); i++) {
		if (host_data->data_in_xfer[i] != NULL) {
			if (host_data->data_in_xfer[i]->buf != NULL) {
				net_buf_unref(host_data->data_in_xfer[i]->buf);
			}
			usbh_xfer_free(host_data->udev, host_data->data_in_xfer[i]);
			host_data->data_in_xfer[i] = NULL;
		}
	}

	return ret;
}

static void deinitialize_bulk_in_xfer(struct cdc_ncm_host_data *const host_data)
{
	struct uhc_transfer *xfer;
	int err;

	for (unsigned int i = 0; i < ARRAY_SIZE(host_data->data_in_xfer); i++) {
		xfer = host_data->data_in_xfer[i];
		if (xfer == NULL) {
			continue;
		}

		host_data->data_in_xfer[i] = NULL;

		if (xfer->queued) {
			err = usbh_xfer_dequeue(host_data->udev, xfer);
			if (err == 0) {
				/* Dequeued; the completion callback frees it. */
				continue;
			}
			LOG_ERR("Failed to dequeue bulk IN transfer %u: %d", i, err);
		}

		/*
		 * Never enqueued, or dequeue failed: the completion callback will not fire,
		 * so the transfer and its buffer are released here.
		 */
		release_xfer(host_data, xfer);
	}
}

static int start_bulk_in_xfer(struct cdc_ncm_host_data *const host_data)
{
	struct uhc_transfer *xfer;
	int ret;

	for (unsigned int i = 0; i < ARRAY_SIZE(host_data->data_in_xfer); i++) {
		xfer = host_data->data_in_xfer[i];

		ret = usbh_xfer_enqueue(host_data->udev, xfer);
		if (ret != 0) {
			LOG_ERR("Failed to start bulk IN transfer %u", i);
			return ret;
		}
	}

	return 0;
}

static int forward_datagram(struct cdc_ncm_host_data *const host_data, const uint8_t *const data,
			    const uint16_t len)
{
	struct net_pkt *pkt;
	int ret;

	pkt = net_pkt_rx_alloc_with_buffer(host_data->iface, len, NET_AF_UNSPEC, 0, K_NO_WAIT);
	if (pkt == NULL) {
		return -ENOMEM;
	}

	ret = net_pkt_write(pkt, data, len);
	if (ret != 0) {
		net_pkt_unref(pkt);
		return ret;
	}

	ret = net_recv_data(host_data->iface, pkt);
	if (ret != 0) {
		net_pkt_unref(pkt);
	}

	return ret;
}

static int forward_received_ntb(struct cdc_ncm_host_data *const host_data,
				const struct net_buf *const buf)
{
	const struct usb_cdc_ncm_nth16 *nth;
	uint16_t block_len;
	uint16_t ndp_off;
	uint16_t seq;
	int ret;

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED)) {
		return -ENODEV;
	}

	if (buf->len < sizeof(struct usb_cdc_ncm_nth16)) {
		LOG_DBG("NTB shorter than NTH16 (%u bytes)", (unsigned int)buf->len);
		return -EINVAL;
	}

	nth = (const struct usb_cdc_ncm_nth16 *)buf->data;

	if (sys_le32_to_cpu(nth->dwSignature) != USB_CDC_NCM_NTH16_SIGNATURE) {
		LOG_DBG("Invalid NTH16 signature 0x%08x", sys_le32_to_cpu(nth->dwSignature));
		return -EINVAL;
	}

	if (sys_le16_to_cpu(nth->wHeaderLength) != sizeof(struct usb_cdc_ncm_nth16)) {
		LOG_DBG("Invalid NTH16 header length %u", sys_le16_to_cpu(nth->wHeaderLength));
		return -EINVAL;
	}

	block_len = sys_le16_to_cpu(nth->wBlockLength);
	if (block_len > buf->len) {
		LOG_DBG("NTH16 block length %u exceeds transfer length %u", block_len,
			(unsigned int)buf->len);
		return -EINVAL;
	}

	seq = sys_le16_to_cpu(nth->wSequence);
	if (seq != host_data->rx_seq) {
		LOG_DBG("Unexpected NTH16 sequence %u (expected %u)", seq, host_data->rx_seq);
	}
	host_data->rx_seq = seq + 1;

	ndp_off = sys_le16_to_cpu(nth->wNdpIndex);
	if (ndp_off < sizeof(struct usb_cdc_ncm_nth16) ||
	    (ndp_off & (CDC_NCM_ALIGNMENT - 1)) != 0) {
		LOG_DBG("Invalid NDP16 offset %u", ndp_off);
		return -EINVAL;
	}

	while (ndp_off != 0) {
		const struct usb_cdc_ncm_ndp16 *ndp;
		uint16_t ndp_len;
		uint32_t ndp_sig;
		unsigned int dpe_count;
		bool crc_present;

		if ((uint32_t)ndp_off + sizeof(struct usb_cdc_ncm_ndp16) > block_len) {
			LOG_DBG("NDP16 at offset %u outside of NTB (%u bytes)", ndp_off, block_len);
			return -EINVAL;
		}

		ndp = (const struct usb_cdc_ncm_ndp16 *)(buf->data + ndp_off);
		ndp_sig = sys_le32_to_cpu(ndp->dwSignature);

		if (ndp_sig != USB_CDC_NCM_NDP16_SIGNATURE_NOCRC &&
		    ndp_sig != USB_CDC_NCM_NDP16_SIGNATURE_CRC) {
			LOG_DBG("Invalid NDP16 signature 0x%08x", ndp_sig);
			return -EINVAL;
		}

		crc_present = (ndp_sig == USB_CDC_NCM_NDP16_SIGNATURE_CRC);

		ndp_len = sys_le16_to_cpu(ndp->wLength);
		if (ndp_len <
			    sizeof(struct usb_cdc_ncm_ndp16) +
				    sizeof(struct usb_cdc_ncm_ndp16_dpe) ||
		    (ndp_len & (CDC_NCM_ALIGNMENT - 1)) != 0 ||
		    (uint32_t)ndp_off + ndp_len > block_len) {
			LOG_DBG("Invalid NDP16 length %u", ndp_len);
			return -EINVAL;
		}

		dpe_count = (ndp_len - sizeof(struct usb_cdc_ncm_ndp16)) /
			    sizeof(struct usb_cdc_ncm_ndp16_dpe);

		for (unsigned int i = 0; i < dpe_count; i++) {
			uint16_t dgram_off = sys_le16_to_cpu(ndp->dpe[i].wDatagramIndex);
			uint16_t dgram_len = sys_le16_to_cpu(ndp->dpe[i].wDatagramLength);

			/* The first null entry terminates the datagram list */
			if (dgram_off == 0 || dgram_len == 0) {
				break;
			}

			if ((uint32_t)dgram_off + dgram_len > block_len) {
				LOG_DBG("Datagram (%u, %u) outside of NTB (%u bytes)",
					dgram_off, dgram_len, block_len);
				return -EINVAL;
			}

			if (crc_present) {
				if (dgram_len < CDC_NCM_FCS_LEN + CDC_NCM_ETH_HDR_LEN) {
					LOG_DBG("Datagram too short for the CRC (%u bytes)",
						dgram_len);
					return -EINVAL;
				}

				/* Strip the CRC before handing the frame to the stack */
				dgram_len -= CDC_NCM_FCS_LEN;
			}

			if (dgram_len > CONFIG_USBH_CDC_NCM_MAX_SEGMENT_SIZE) {
				LOG_DBG("Datagram length %u exceeds the supported maximum",
					dgram_len);
				return -EINVAL;
			}

			ret = forward_datagram(host_data, buf->data + dgram_off, dgram_len);
			if (ret != 0) {
				LOG_DBG("Failed to forward datagram: %d", ret);
				return ret;
			}
		}

		ndp_off = sys_le16_to_cpu(ndp->wNextNdpIndex);
	}

	return 0;
}

static int bulk_in_req_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct cdc_ncm_host_data *host_data = xfer->priv;
	struct net_buf *buf = xfer->buf;
	int ret = 0;

	if (xfer->err == -ECONNRESET) {
		LOG_INF("The bulk IN transfer is cancelled");
		net_buf_unref(buf);
		usbh_xfer_free(udev, xfer);
		goto done;
	}

	if (xfer->err == 0) {
		forward_received_ntb(host_data, buf);
	}

	/* The buffer is reused for the next transfer, the datagrams are copied out */
	net_buf_reset(buf);

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED) ||
	    !atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		goto done;
	}

	ret = usbh_xfer_enqueue(udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to continue bulk IN transfer: %d", ret);
	}

done:
	return ret;
}

static int submit_bulk_out_xfer(struct cdc_ncm_host_data *const host_data,
				struct net_buf *const buf)
{
	struct usb_device *udev;
	struct uhc_transfer *xfer = NULL;
	int ret;

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED) ||
	    !atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		return -ENODEV;
	}

	udev = host_data->udev;
	if (udev == NULL) {
		return -ENODEV;
	}

	if (buf->len > host_data->ntb_out_max_size) {
		LOG_WRN("NTB length (%u) exceeds device dwNtbOutMaxSize (%u)",
			(unsigned int)buf->len, host_data->ntb_out_max_size);
		return -EMSGSIZE;
	}

	ret = k_sem_take(&host_data->data_out_sem, CDC_NCM_TX_TIMEOUT);
	if (ret != 0) {
		return ret;
	}

	xfer = usbh_xfer_alloc(udev, CDC_NCM_DESC_DATA_EP_OUT_ADDR(&host_data->desc),
			       bulk_out_req_cb, host_data);
	if (xfer == NULL) {
		LOG_ERR("Failed to allocate bulk OUT transfer");
		ret = -ENOMEM;
		goto done;
	}

	ret = usbh_xfer_buf_add(udev, xfer, buf);
	if (ret != 0) {
		LOG_ERR("Failed to add buffer to bulk OUT transfer: %d", ret);
		goto done;
	}

	ret = usbh_xfer_enqueue(udev, xfer);
	if (ret != 0) {
		LOG_ERR("Failed to enqueue bulk OUT transfer: %d", ret);
		goto done;
	}

	return 0;

done:
	if (xfer != NULL) {
		xfer->buf = NULL;
		usbh_xfer_free(udev, xfer);
	}

	k_sem_give(&host_data->data_out_sem);
	return ret;
}

static void wait_bulk_out_xfer_complete(struct cdc_ncm_host_data *const host_data)
{
	/*
	 * Drain the semaphore to wait for all in-flight TX transfers to complete, then
	 * restore the permits.
	 */
	for (unsigned int i = 0; i < CONFIG_USBH_CDC_NCM_TX_PIPELINE_DEPTH; i++) {
		k_sem_take(&host_data->data_out_sem, K_FOREVER);
	}

	for (unsigned int i = 0; i < CONFIG_USBH_CDC_NCM_TX_PIPELINE_DEPTH; i++) {
		k_sem_give(&host_data->data_out_sem);
	}
}

static bool check_zlp(struct cdc_ncm_host_data *const host_data, const uint16_t len)
{
	const uint16_t ep_mps = CDC_NCM_DESC_DATA_EP_OUT_MPS(&host_data->desc);

	return (len > 0 && (len % ep_mps) == 0);
}

static uint32_t align_datagram_offset(struct cdc_ncm_host_data *const host_data,
				      const uint32_t offset)
{
	const uint16_t divisor = host_data->tx_divisor;
	uint32_t rem;

	if (divisor <= 1) {
		return offset;
	}

	/*
	 * The device aligns the datagram payload, which starts after the Ethernet
	 * header: (offset + ETH_HDR_LEN) % divisor == wNdpOutPayloadRemainder.
	 */
	rem = (host_data->tx_remainder - CDC_NCM_ETH_HDR_LEN) & (divisor - 1);

	return offset + ((rem - (offset % divisor)) & (divisor - 1));
}

static int build_ntb(struct cdc_ncm_host_data *const host_data, struct net_buf *const buf,
		     const uint16_t frame_len)
{
	struct usb_cdc_ncm_nth16 *nth;
	struct usb_cdc_ncm_ndp16 *ndp;
	uint32_t ndp_off;
	uint32_t dgram_off;
	uint32_t total_len;

	/* A single datagram per NTB for now, which always fits the limits */
	ndp_off = ROUND_UP(CDC_NCM_NTH16_SIZE, host_data->ndp_out_alignment);
	dgram_off = align_datagram_offset(host_data, ndp_off + CDC_NCM_NDP16_SIZE);
	total_len = dgram_off + frame_len;

	if (total_len > MIN(host_data->ntb_out_max_size, CDC_NCM_MAX_NTB_SIZE) ||
	    total_len > buf->size || total_len > UINT16_MAX) {
		return -EMSGSIZE;
	}

	memset(buf->data, 0, dgram_off);

	nth = (struct usb_cdc_ncm_nth16 *)buf->data;
	nth->dwSignature = sys_cpu_to_le32(USB_CDC_NCM_NTH16_SIGNATURE);
	nth->wHeaderLength = sys_cpu_to_le16(sizeof(struct usb_cdc_ncm_nth16));
	nth->wSequence = sys_cpu_to_le16(host_data->tx_seq++);
	nth->wBlockLength = sys_cpu_to_le16((uint16_t)total_len);
	nth->wNdpIndex = sys_cpu_to_le16((uint16_t)ndp_off);

	ndp = (struct usb_cdc_ncm_ndp16 *)(buf->data + ndp_off);
	ndp->dwSignature = sys_cpu_to_le32(USB_CDC_NCM_NDP16_SIGNATURE_NOCRC);
	ndp->wLength = sys_cpu_to_le16(CDC_NCM_NDP16_SIZE);
	ndp->wNextNdpIndex = 0;
	ndp->dpe[0].wDatagramIndex = sys_cpu_to_le16((uint16_t)dgram_off);
	ndp->dpe[0].wDatagramLength = sys_cpu_to_le16(frame_len);
	ndp->dpe[1].wDatagramIndex = 0;
	ndp->dpe[1].wDatagramLength = 0;

	net_buf_add(buf, dgram_off);

	return 0;
}

static int bulk_out_req_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct cdc_ncm_host_data *host_data = xfer->priv;
	struct net_buf *buf = xfer->buf;
	uint16_t buf_len = 0;
	int ret = 0;

	if (buf != NULL) {
		buf_len = buf->len;
		net_buf_unref(buf);
		xfer->buf = NULL;
	}

	if (xfer->err != 0) {
		if (xfer->err == -ECONNRESET) {
			LOG_INF("The bulk OUT transfer is cancelled");
		}
		goto cleanup;
	}

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED) ||
	    !atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		goto cleanup;
	}

	if (check_zlp(host_data, buf_len)) {
		ret = usbh_xfer_enqueue(udev, xfer);
		if (ret != 0) {
			LOG_ERR("Failed to continue bulk OUT transfer (ZLP): %d", ret);
			goto cleanup;
		}
		goto done;
	}

cleanup:
	usbh_xfer_free(udev, xfer);
	k_sem_give(&host_data->data_out_sem);

done:
	return ret;
}

static int enable_function(struct cdc_ncm_host_data *const host_data)
{
	uint16_t pkt_filter_bitmap = PACKET_TYPE_DIRECTED | PACKET_TYPE_BROADCAST;
	int ret;

	if (atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		return 0;
	}

	/* The first NTB after a function reset shall carry sequence number 0 */
	host_data->tx_seq = 0;
	host_data->rx_seq = 0;

	ret = usbh_device_interface_set(host_data->udev, CDC_NCM_DESC_DATA_IF_NUM(&host_data->desc),
					CDC_NCM_DESC_DATA_IF_ALT(&host_data->desc), false);
	if (ret != 0) {
		LOG_ERR("Failed to set data interface alternate setting: %d", ret);
		return ret;
	}

	ret = initialize_interrupt_in_xfer(host_data);
	if (ret != 0) {
		goto error;
	}

	ret = initialize_bulk_in_xfer(host_data);
	if (ret != 0) {
		goto error;
	}

	atomic_set_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING);

	if (host_data->bm_caps & USB_CDC_NCM_NCAP_ETH_FILTER) {
#if defined(CONFIG_NET_PROMISCUOUS_MODE)
		/*
		 * Apply the promiscuous mode that may have been requested while no USB
		 * device was connected.
		 */
		if (net_if_flag_is_set(host_data->iface, NET_IF_PROMISC)) {
			pkt_filter_bitmap |= PACKET_TYPE_PROMISCUOUS;
		}
#endif

		ret = set_packet_filter(host_data, pkt_filter_bitmap, true);
		if (ret != 0) {
			goto error;
		}

		/*
		 * Reprogram the multicast filtering from the addresses tracked by the
		 * network subsystem. The L2 only notifies the driver on the first join
		 * or the last leave, so after a device re-plug or an iface down/up the
		 * groups it keeps tracking must be applied again here.
		 */
		ret = set_multicast_filters(host_data);
		if (ret != 0) {
			goto error;
		}
	} else {
		LOG_DBG("Function does not support the Ethernet packet filter");
	}

	ret = start_interrupt_in_xfer(host_data);
	if (ret != 0) {
		goto error;
	}

	ret = start_bulk_in_xfer(host_data);
	if (ret != 0) {
		goto error;
	}

	return 0;

error:
	deinitialize_interrupt_in_xfer(host_data);
	deinitialize_bulk_in_xfer(host_data);

	usbh_device_interface_set(host_data->udev, CDC_NCM_DESC_DATA_IF_NUM(&host_data->desc), 0,
				  false);

	atomic_clear_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING);

	return ret;
}

static void disable_function(struct cdc_ncm_host_data *const host_data)
{
	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		return;
	}

	deinitialize_interrupt_in_xfer(host_data);
	deinitialize_bulk_in_xfer(host_data);

	usbh_device_interface_set(host_data->udev, CDC_NCM_DESC_DATA_IF_NUM(&host_data->desc), 0,
				  false);

	atomic_clear_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING);
}

static int usbh_cdc_ncm_init(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static int usbh_cdc_ncm_probe(struct usbh_class_data *const c_data, struct usb_device *const udev,
			      const uint8_t iface)
{
	struct cdc_ncm_host_data *host_data = c_data->priv;
	const struct net_linkaddr *link_addr;
	int ret;

	if (iface == USBH_CLASS_IFNUM_DEVICE) {
		return -ENOTSUP;
	}

	k_mutex_lock(&host_data->mutex, K_FOREVER);

	host_data->udev = udev;

	ret = parse_descriptors(host_data, iface);
	if (ret != 0) {
		ret = -ENOTSUP;
		goto cleanup;
	}

	host_data->bm_caps = CDC_NCM_DESC_CAPS(&host_data->desc);

	ret = get_mac_address(host_data);
	if (ret != 0) {
		goto cleanup;
	}

	ret = configure_ntb(host_data);
	if (ret != 0) {
		goto cleanup;
	}

	link_addr = net_if_get_link_addr(host_data->iface);

	LOG_INF("The USB device information is summarized below\n"
		"Device Information:\n"
		"\tCommunication: interface %u, endpoint [IN 0x%02x]\n"
		"\tData: interface %u (alt %d), endpoint [IN 0x%02x, OUT 0x%02x (MPS %u)]\n"
		"\tMAC: %02X-%02X-%02X-%02X-%02X-%02X (string descriptor index %u)\n"
		"\tCapabilities: 0x%02x\n"
		"\tMax Segment Size: %u bytes\n"
		"\tNTB IN/OUT max size: %u/%u bytes\n"
		"\tHardware Multicast Filters: %u (%s)",
		CDC_NCM_DESC_COMM_IF_NUM(&host_data->desc),
		CDC_NCM_DESC_COMM_EP_IN_ADDR(&host_data->desc),
		CDC_NCM_DESC_DATA_IF_NUM(&host_data->desc),
		CDC_NCM_DESC_DATA_IF_ALT(&host_data->desc),
		CDC_NCM_DESC_DATA_EP_IN_ADDR(&host_data->desc),
		CDC_NCM_DESC_DATA_EP_OUT_ADDR(&host_data->desc),
		CDC_NCM_DESC_DATA_EP_OUT_MPS(&host_data->desc), link_addr->addr[0],
		link_addr->addr[1], link_addr->addr[2], link_addr->addr[3], link_addr->addr[4],
		link_addr->addr[5], CDC_NCM_DESC_MAC_ADDR_INDEX(&host_data->desc),
		host_data->bm_caps, CDC_NCM_DESC_MAX_SEGMENT_SIZE(&host_data->desc),
		host_data->ntb_in_max_size, host_data->ntb_out_max_size,
		CDC_NCM_DESC_MC_FILTER_COUNT(&host_data->desc),
		CDC_NCM_DESC_MC_FILTER_IMPERFECT(&host_data->desc) ? "imperfect" : "perfect");

	atomic_set_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED);

	if (net_if_is_admin_up(host_data->iface)) {
		ret = enable_function(host_data);
		if (ret != 0) {
			goto cleanup;
		}
	}

	k_mutex_unlock(&host_data->mutex);

	return 0;

cleanup:
	atomic_clear_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED);

	reset_states(host_data);

	k_mutex_unlock(&host_data->mutex);

	return ret;
}

static int usbh_cdc_ncm_removed(struct usbh_class_data *const c_data)
{
	struct cdc_ncm_host_data *host_data = c_data->priv;

	atomic_clear_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED);

	net_eth_carrier_off(host_data->iface);

	/*
	 * Wait for in-flight TX transfers without holding the mutex: their
	 * completion callbacks run in the USB host event thread, which must
	 * not be blocked on the mutex while we wait for them to release the
	 * semaphore.
	 */
	wait_bulk_out_xfer_complete(host_data);

	k_mutex_lock(&host_data->mutex, K_FOREVER);

	if (net_if_is_admin_up(host_data->iface)) {
		disable_function(host_data);
	}

	reset_states(host_data);

	k_mutex_unlock(&host_data->mutex);

	return 0;
}

static void eth_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct cdc_ncm_host_data *host_data = dev->data;

	/*
	 * Start with a locally administered random MAC (RFC 7042); it is replaced by the
	 * function-provided address once the device is probed.
	 */
	uint8_t mac_addr[NET_ETH_ADDR_LEN] = {0x00, 0x00, 0x5E, 0x00, 0x53, sys_rand8_get()};

	k_mutex_lock(&host_data->mutex, K_FOREVER);
	host_data->iface = iface;
	k_mutex_unlock(&host_data->mutex);

	net_if_set_link_addr(host_data->iface, mac_addr, NET_ETH_ADDR_LEN, NET_LINK_ETHERNET);

	ethernet_init(iface);
	net_if_carrier_off(iface);
}

static int eth_start(const struct device *dev, struct net_if *iface)
{
	struct cdc_ncm_host_data *host_data = dev->data;
	int ret;

	ARG_UNUSED(iface);

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED)) {
		return 0;
	}

	k_mutex_lock(&host_data->mutex, K_FOREVER);
	ret = enable_function(host_data);
	k_mutex_unlock(&host_data->mutex);

	return ret;
}

static int eth_stop(const struct device *dev, struct net_if *iface)
{
	struct cdc_ncm_host_data *host_data = dev->data;

	ARG_UNUSED(iface);

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED)) {
		return 0;
	}

	/* See usbh_cdc_ncm_removed(): wait outside the mutex. */
	wait_bulk_out_xfer_complete(host_data);

	k_mutex_lock(&host_data->mutex, K_FOREVER);
	disable_function(host_data);
	k_mutex_unlock(&host_data->mutex);

	return 0;
}

static enum ethernet_hw_caps eth_get_capabilities(const struct device *dev, struct net_if *iface)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(iface);

	return ETHERNET_LINK_10BASE | ETHERNET_LINK_100BASE | ETHERNET_HW_FILTERING
#if defined(CONFIG_NET_PROMISCUOUS_MODE)
	       | ETHERNET_PROMISC_MODE
#endif
		;
}

static int eth_set_config(const struct device *dev, struct net_if *iface,
			  enum ethernet_config_type type, const struct ethernet_config *config)
{
	struct cdc_ncm_host_data *host_data = dev->data;
	int ret = 0;

	ARG_UNUSED(iface);

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED)) {
		return 0;
	}

	k_mutex_lock(&host_data->mutex, K_FOREVER);

	switch (type) {
	case ETHERNET_CONFIG_TYPE_FILTER:
		ret = set_multicast_filters(host_data);
		break;

#if defined(CONFIG_NET_PROMISCUOUS_MODE)
	case ETHERNET_CONFIG_TYPE_PROMISC_MODE:
		if (!(host_data->bm_caps & USB_CDC_NCM_NCAP_ETH_FILTER)) {
			ret = -ENOTSUP;
			break;
		}

		ret = set_packet_filter(host_data, PACKET_TYPE_PROMISCUOUS, config->promisc_mode);
		break;
#endif

	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&host_data->mutex);

	return ret;
}

static int eth_send(const struct device *dev, struct net_pkt *pkt)
{
	struct cdc_ncm_host_data *host_data = dev->data;
	struct net_buf *buf = NULL;
	size_t total_len;
	int ret;

	if (!atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_CONNECTED) ||
	    !atomic_test_bit(&host_data->flags, CDC_NCM_DEVICE_FLAG_FORWARDING)) {
		return -ENETDOWN;
	}

	total_len = net_pkt_get_len(pkt);
	if (total_len == 0) {
		return -ENODATA;
	}

	if (total_len > CONFIG_USBH_CDC_NCM_MAX_SEGMENT_SIZE) {
		return -EMSGSIZE;
	}

	buf = net_buf_alloc(&usbh_cdc_ncm_pool, CDC_NCM_TX_TIMEOUT);
	if (buf == NULL) {
		LOG_WRN("Failed to allocate data transmitting buffer");
		return -ENOMEM;
	}

	k_mutex_lock(&host_data->mutex, K_FOREVER);

	ret = build_ntb(host_data, buf, (uint16_t)total_len);
	if (ret != 0) {
		goto unlock;
	}

	ret = net_pkt_read(pkt, buf->data + buf->len, total_len);
	if (ret != 0) {
		LOG_ERR("Failed to copy packet to data transmitting buffer: %d", ret);
		goto unlock;
	}

	net_buf_add(buf, total_len);

	ret = submit_bulk_out_xfer(host_data, buf);
	if (ret != 0) {
		goto unlock;
	}

	buf = NULL;

unlock:
	k_mutex_unlock(&host_data->mutex);

	if (buf != NULL) {
		net_buf_unref(buf);
	}

	return ret;
}

static struct usbh_class_api usbh_cdc_ncm_api = {
	.init = usbh_cdc_ncm_init,
	.probe = usbh_cdc_ncm_probe,
	.removed = usbh_cdc_ncm_removed,
};

static const struct ethernet_api eth_api = {
	.iface_api.init = eth_iface_init,
	.start = eth_start,
	.stop = eth_stop,
	.get_capabilities = eth_get_capabilities,
	.set_config = eth_set_config,
	.send = eth_send,
};

static int eth_net_device_init_fn(const struct device *dev)
{
	struct cdc_ncm_host_data *host_data = dev->data;

	k_mutex_init(&host_data->mutex);

	atomic_clear(&host_data->flags);

	reset_states(host_data);

	k_sem_init(&host_data->data_out_sem, CONFIG_USBH_CDC_NCM_TX_PIPELINE_DEPTH,
		   CONFIG_USBH_CDC_NCM_TX_PIPELINE_DEPTH);

	return 0;
}

#define USBH_CDC_NCM_DEVICE_DEFINE(x, _)                                                           \
	static struct cdc_ncm_host_data cdc_ncm_host_data_##x;                                     \
                                                                                                   \
	ETH_NET_DEVICE_INIT(usbh_cdc_ncm_##x, CONFIG_USBH_CDC_NCM_ETH_DRV_NAME #x,                 \
			    eth_net_device_init_fn, NULL, &cdc_ncm_host_data_##x, NULL,            \
			    CONFIG_ETH_INIT_PRIORITY, &eth_api, NET_ETH_MTU);                      \
                                                                                                   \
	USBH_DEFINE_CLASS(cdc_ncm_c_data_##x, &usbh_cdc_ncm_api, &cdc_ncm_host_data_##x,           \
			  cdc_ncm_filters)

LISTIFY(CONFIG_USBH_CDC_NCM_INSTANCES_COUNT, USBH_CDC_NCM_DEVICE_DEFINE, (;), _);
