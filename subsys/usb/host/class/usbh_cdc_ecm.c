/*
 * Copyright (c) 2025 NXP
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/usb/class/usb_cdc.h>

#include "usbh_class.h"
#include "usbh_desc.h"
#include "usbh_ch9.h"

#define DT_DRV_COMPAT zephyr_cdc_ecm_host

#define USBH_CDC_ECM_INSTANCE_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

struct usbh_cdc_ecm_ctx {
	struct k_mutex lock;
	struct usb_device *udev;
	uint8_t comm_if_num;
	uint8_t data_if_num;
	uint8_t data_alt_num;
	uint8_t comm_in_ep_addr;
	uint8_t data_in_ep_addr;
	uint8_t data_out_ep_addr;
	uint16_t data_out_ep_mps;
	uint8_t mac_str_desc_idx;
	uint16_t max_segment_size;
	bool link_state;
	uint32_t upload_speed;
	uint32_t download_speed;
	uint32_t active_data_rx_xfers;
	struct net_if *iface;
	struct net_eth_addr eth_mac;
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	struct net_stats_eth stats;
#endif
};

struct usbh_cdc_ecm_req_params {
	uint16_t if_num;
	uint8_t bRequest;
	union {
		struct {
			uint8_t (*m_addr)[6];
			uint16_t len;
		} multicast_filter_list;
		struct {
			uint16_t num;
			uint16_t mask_size;
			uint8_t *mask_bitmask;
			uint8_t *pattern;
			uint16_t pattern_size;
		} pm_pattern_filter;
		struct {
			uint16_t num;
			uint16_t active;
		} pm_pattern_activation;
		uint16_t eth_pkt_filter_bitmap;
		struct {
			uint16_t feature_sel;
			uint32_t data;
		} eth_stats;
	};
};

struct usbh_cdc_ecm_xfer_params {
	uint8_t ep_addr;
	struct net_buf *buf;
	usbh_udev_cb_t cb;
	void *cb_priv;
	struct uhc_transfer *xfer;
};

enum usbh_cdc_ecm_event_code {
	CDC_ECM_EVENT_TASK_START,
	CDC_ECM_EVENT_COMM_RX,
	CDC_ECM_EVENT_DATA_RX,
};

struct usbh_cdc_ecm_msg {
	struct usbh_cdc_ecm_ctx *ctx;
	enum usbh_cdc_ecm_event_code event;
};

LOG_MODULE_REGISTER(usbh_cdc_ecm, CONFIG_USBH_CDC_ECM_LOG_LEVEL);

NET_BUF_POOL_DEFINE(usbh_cdc_ecm_data_tx_pool,
		    USBH_CDC_ECM_INSTANCE_COUNT *CONFIG_USBH_CDC_ECM_DATA_TX_BUF_COUNT,
		    CONFIG_USBH_CDC_ECM_DATA_BUF_POOL_SIZE, 0, NULL);

NET_BUF_POOL_DEFINE(usbh_cdc_ecm_data_rx_pool,
		    USBH_CDC_ECM_INSTANCE_COUNT *CONFIG_USBH_CDC_ECM_DATA_RX_BUF_COUNT,
		    CONFIG_USBH_CDC_ECM_DATA_BUF_POOL_SIZE, 0, NULL);

K_MSGQ_DEFINE(usbh_cdc_ecm_msgq, sizeof(struct usbh_cdc_ecm_msg),
	      USBH_CDC_ECM_INSTANCE_COUNT *CONFIG_USBH_CDC_ECM_MSG_QUEUE_DEPTH, 4);

static bool usbh_cdc_ecm_is_configured(struct usbh_cdc_ecm_ctx *const ctx)
{
	if (!ctx || !ctx->udev) {
		return false;
	}

	if (ctx->udev->state != USB_STATE_CONFIGURED) {
		return false;
	}

	return true;
}

static int usbh_cdc_ecm_msgq_put(struct usbh_cdc_ecm_msg const *msg)
{
	return k_msgq_put(&usbh_cdc_ecm_msgq, msg, K_NO_WAIT);
}

static int usbh_cdc_ecm_req(struct usbh_cdc_ecm_ctx *const ctx,
			    struct usbh_cdc_ecm_req_params *const param)
{
	uint8_t bmRequestType = USB_REQTYPE_TYPE_CLASS << 5 | USB_REQTYPE_RECIPIENT_INTERFACE;
	uint16_t wValue = 0;
	uint16_t wLength;
	struct net_buf *req_buf = NULL;
	uint16_t pm_pattern_filter_mask_size;
	int ret = 0;

	if (!ctx || !param) {
		return -EINVAL;
	}

	if (!usbh_cdc_ecm_is_configured(ctx)) {
		return -ENODEV;
	}

	switch (param->bRequest) {
	case SET_ETHERNET_MULTICAST_FILTERS:
		if (param->multicast_filter_list.len > UINT16_MAX / 6) {
			return -EINVAL;
		}
		bmRequestType |= USB_REQTYPE_DIR_TO_DEVICE << 7;
		wValue = param->multicast_filter_list.len;
		wLength = param->multicast_filter_list.len * 6;
		req_buf = usbh_xfer_buf_alloc(ctx->udev, wLength);
		if (!req_buf) {
			return -ENOMEM;
		}
		if (!net_buf_add_mem(req_buf, param->multicast_filter_list.m_addr, wLength)) {
			ret = -ENOMEM;
			goto cleanup;
		}
		break;
	case SET_ETHERNET_PM_FILTER:
		if (param->pm_pattern_filter.mask_size > UINT16_MAX - 2 ||
		    param->pm_pattern_filter.pattern_size >
			    UINT16_MAX - 2 - param->pm_pattern_filter.mask_size) {
			return -EINVAL;
		}
		bmRequestType |= USB_REQTYPE_DIR_TO_DEVICE << 7;
		wValue = param->pm_pattern_filter.num;
		wLength = 2 + param->pm_pattern_filter.mask_size +
			  param->pm_pattern_filter.pattern_size;
		req_buf = usbh_xfer_buf_alloc(ctx->udev, wLength);
		if (!req_buf) {
			return -ENOMEM;
		}
		pm_pattern_filter_mask_size = sys_cpu_to_le16(param->pm_pattern_filter.mask_size);
		if (!net_buf_add_mem(req_buf, &pm_pattern_filter_mask_size, 2)) {
			ret = -ENOMEM;
			goto cleanup;
		}
		if (!net_buf_add_mem(req_buf, param->pm_pattern_filter.mask_bitmask,
				     param->pm_pattern_filter.mask_size)) {
			ret = -ENOMEM;
			goto cleanup;
		}
		if (!net_buf_add_mem(req_buf, param->pm_pattern_filter.pattern,
				     param->pm_pattern_filter.pattern_size)) {
			ret = -ENOMEM;
			goto cleanup;
		}
		break;
	case GET_ETHERNET_PM_FILTER:
		bmRequestType |= USB_REQTYPE_DIR_TO_HOST << 7;
		wValue = param->pm_pattern_activation.num;
		wLength = 2;
		req_buf = usbh_xfer_buf_alloc(ctx->udev, wLength);
		if (!req_buf) {
			return -ENOMEM;
		}
		break;
	case SET_ETHERNET_PACKET_FILTER:
		bmRequestType |= USB_REQTYPE_DIR_TO_DEVICE << 7;
		wValue = param->eth_pkt_filter_bitmap;
		wLength = 0;
		req_buf = NULL;
		break;
	case GET_ETHERNET_STATISTIC:
		bmRequestType |= USB_REQTYPE_DIR_TO_HOST << 7;
		wValue = param->eth_stats.feature_sel;
		wLength = 4;
		req_buf = usbh_xfer_buf_alloc(ctx->udev, wLength);
		if (!req_buf) {
			return -ENOMEM;
		}
		break;
	default:
		return -ENOTSUP;
	}

	ret = usbh_req_setup(ctx->udev, bmRequestType, param->bRequest, wValue, param->if_num,
			     wLength, req_buf);

	if (!ret && req_buf) {
		switch (param->bRequest) {
		case GET_ETHERNET_PM_FILTER:
			if (req_buf->len == 2 && !req_buf->frags) {
				param->pm_pattern_activation.active = sys_get_le16(req_buf->data);
			} else {
				ret = -EIO;
			}
			break;
		case GET_ETHERNET_STATISTIC:
			if (req_buf->len == 4 && !req_buf->frags) {
				param->eth_stats.data = sys_get_le32(req_buf->data);
			} else {
				ret = -EIO;
			}
			break;
		}
	}

cleanup:
	if (req_buf) {
		usbh_xfer_buf_free(ctx->udev, req_buf);
	}

	return ret;
}

static int usbh_cdc_ecm_xfer(struct usbh_cdc_ecm_ctx *const ctx,
			     struct usbh_cdc_ecm_xfer_params *const param)
{
	int ret;

	param->xfer = NULL;

	if (!ctx || !param) {
		return -EINVAL;
	}

	if (!usbh_cdc_ecm_is_configured(ctx)) {
		return -ENODEV;
	}

	param->xfer = usbh_xfer_alloc(ctx->udev, param->ep_addr, param->cb, param->cb_priv);
	if (!param->xfer) {
		return -ENOMEM;
	}

	ret = usbh_xfer_buf_add(ctx->udev, param->xfer, param->buf);
	if (ret) {
		(void)usbh_xfer_free(ctx->udev, param->xfer);
		return ret;
	}

	ret = usbh_xfer_enqueue(ctx->udev, param->xfer);
	if (ret) {
		(void)usbh_xfer_free(ctx->udev, param->xfer);
		return ret;
	}

	return 0;
}

static int usbh_cdc_ecm_comm_rx(struct usbh_cdc_ecm_ctx *const ctx);

static int usbh_cdc_ecm_comm_rx_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct usbh_cdc_ecm_ctx *ctx = xfer->priv;
	struct usbh_cdc_ecm_msg msg;
	struct cdc_notification_packet *notif;
	uint32_t *link_speeds;
	bool locked = false;
	int err;
	int ret = 0;

	if (!ctx) {
		ret = -EINVAL;
		goto cleanup;
	}

	(void)k_mutex_lock(&ctx->lock, K_FOREVER);
	locked = true;

	if (xfer->err) {
		LOG_WRN("notification RX transfer error (%d)", xfer->err);
		goto cleanup;
	}

	if (!ctx->udev || ctx->udev != udev) {
		ret = -ENODEV;
		goto cleanup;
	}

	notif = (struct cdc_notification_packet *)xfer->buf->data;
	switch (notif->bNotification) {
	case USB_CDC_NETWORK_CONNECTION:
		if (xfer->buf->len != sizeof(struct cdc_notification_packet)) {
			ret = -EBADMSG;
			goto cleanup;
		}

		if (sys_le16_to_cpu(notif->wValue)) {
			ctx->link_state = true;
		} else {
			ctx->link_state = false;
		}
		break;
	case USB_CDC_CONNECTION_SPEED_CHANGE:
		if (xfer->buf->len != (sizeof(struct cdc_notification_packet) + 8)) {
			ret = -EBADMSG;
			goto cleanup;
		}

		link_speeds = (uint32_t *)(notif + 1);
		ctx->download_speed = sys_le32_to_cpu(link_speeds[0]);
		ctx->upload_speed = sys_le32_to_cpu(link_speeds[1]);

		LOG_INF("network %s", ctx->link_state ? "connected" : "disconnected");
		LOG_INF("link speed: UL %u bps / DL %u bps", ctx->upload_speed,
			ctx->download_speed);

		if (ctx->link_state) {
			net_if_carrier_on(ctx->iface);

			msg.ctx = ctx;
			msg.event = CDC_ECM_EVENT_DATA_RX;
			err = usbh_cdc_ecm_msgq_put(&msg);
			if (err) {
				LOG_ERR("failed to send task data RX message");
			}
		} else {
			net_if_carrier_off(ctx->iface);
		}
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

cleanup:
	if (xfer->buf) {
		usbh_xfer_buf_free(udev, xfer->buf);
	}

	if (udev) {
		(void)usbh_xfer_free(udev, xfer);
	}

	if (locked) {
		(void)k_mutex_unlock(&ctx->lock);
	}

	err = usbh_cdc_ecm_comm_rx(ctx);
	if (err && err != -ENODEV) {
		msg.ctx = ctx;
		msg.event = CDC_ECM_EVENT_COMM_RX;
		(void)usbh_cdc_ecm_msgq_put(&msg);
	}

	return ret;
}

static int usbh_cdc_ecm_comm_rx(struct usbh_cdc_ecm_ctx *const ctx)
{
	struct usbh_cdc_ecm_xfer_params param;
	struct net_buf *buf;
	int ret;

	if (!ctx) {
		return -EINVAL;
	}

	if (k_mutex_lock(&ctx->lock, K_NO_WAIT)) {
		return -EBUSY;
	}

	if (!usbh_cdc_ecm_is_configured(ctx)) {
		ret = -ENODEV;
		goto done;
	}

	buf = usbh_xfer_buf_alloc(ctx->udev, sizeof(struct cdc_notification_packet) + 8);
	if (!buf) {
		LOG_WRN("failed to allocate data buffer for notification reception");
		ret = -ENOMEM;
		goto done;
	}

	param.buf = buf;
	param.cb = usbh_cdc_ecm_comm_rx_cb;
	param.cb_priv = ctx;
	param.ep_addr = ctx->comm_in_ep_addr;

	ret = usbh_cdc_ecm_xfer(ctx, &param);
	if (ret) {
		LOG_ERR("request notification RX transfer error (%d)", ret);
		usbh_xfer_buf_free(ctx->udev, buf);
	}

done:
	(void)k_mutex_unlock(&ctx->lock);

	return ret;
}

static int usbh_cdc_ecm_data_rx(struct usbh_cdc_ecm_ctx *const ctx);

static int usbh_cdc_ecm_data_rx_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct usbh_cdc_ecm_ctx *ctx = xfer->priv;
	struct usbh_cdc_ecm_msg msg;
	struct net_pkt *pkt;
	bool locked = false;
	int err;
	int ret = 0;

	if (!ctx) {
		ret = -EINVAL;
		goto cleanup;
	}

	(void)k_mutex_lock(&ctx->lock, K_FOREVER);
	locked = true;

	ctx->active_data_rx_xfers--;

	if (xfer->err) {
		LOG_WRN("data RX transfer error (%d)", xfer->err);
		goto cleanup;
	}

	if (!ctx->udev || ctx->udev != udev) {
		ret = -ENODEV;
		goto cleanup;
	}

	if (!xfer->buf->len) {
		LOG_DBG("discard received 0 length data");
		goto cleanup;
	}

	if (xfer->buf->len > ctx->max_segment_size) {
		LOG_WRN("dropped received data which length[%u] exceeding max segment size[%u]",
			xfer->buf->len, ctx->max_segment_size);
		goto cleanup;
	}

	if (!ctx->link_state) {
		goto cleanup;
	}

	pkt = net_pkt_rx_alloc_with_buffer(ctx->iface, xfer->buf->len, AF_UNSPEC, 0, K_NO_WAIT);
	if (!pkt) {
		LOG_WRN("failed to allocate net packet and lost received data");
		goto cleanup;
	}

	ret = net_pkt_write(pkt, xfer->buf->data, xfer->buf->len);
	if (ret) {
		LOG_ERR("write data into net packet error (%d)", ret);
		net_pkt_unref(pkt);
		goto cleanup;
	}

	ret = net_recv_data(ctx->iface, pkt);
	if (ret) {
		LOG_ERR("passed data into network stack error (%d)", ret);
		net_pkt_unref(pkt);
	}

cleanup:
	if (xfer->buf) {
		net_buf_unref(xfer->buf);
	}

	if (udev) {
		(void)usbh_xfer_free(udev, xfer);
	}

	if (locked) {
		(void)k_mutex_unlock(&ctx->lock);
	}

	err = usbh_cdc_ecm_data_rx(ctx);
	if (err && err != -ENODEV) {
		msg.ctx = ctx;
		msg.event = CDC_ECM_EVENT_DATA_RX;
		(void)usbh_cdc_ecm_msgq_put(&msg);
	}

	return ret;
}

static int usbh_cdc_ecm_data_rx(struct usbh_cdc_ecm_ctx *const ctx)
{
	struct usbh_cdc_ecm_xfer_params param;
	struct net_buf *buf;
	int ret = 0;

	if (!ctx) {
		return -EINVAL;
	}

	if (k_mutex_lock(&ctx->lock, K_NO_WAIT)) {
		return -EBUSY;
	}

	if (!usbh_cdc_ecm_is_configured(ctx)) {
		ret = -ENODEV;
		goto done;
	}

	if (ctx->active_data_rx_xfers >= CONFIG_USBH_CDC_ECM_DATA_RX_QUEUE_DEPTH) {
		ret = -EBUSY;
		goto done;
	}

	buf = net_buf_alloc(&usbh_cdc_ecm_data_rx_pool, K_NO_WAIT);
	if (!buf) {
		LOG_WRN("failed to allocate data buffer for data reception");
		ret = -ENOMEM;
		goto done;
	}

	param.buf = buf;
	param.cb = usbh_cdc_ecm_data_rx_cb;
	param.cb_priv = ctx;
	param.ep_addr = ctx->data_in_ep_addr;

	ret = usbh_cdc_ecm_xfer(ctx, &param);
	if (ret) {
		LOG_ERR("request data RX transfer error (%d)", ret);
		net_buf_unref(buf);
		goto done;
	}
	ctx->active_data_rx_xfers++;

done:
	(void)k_mutex_unlock(&ctx->lock);

	return ret;
}

static int usbh_cdc_ecm_data_rx_queue(struct usbh_cdc_ecm_ctx *const ctx)
{
	struct usbh_cdc_ecm_xfer_params param;
	struct net_buf *buf;
	int ret = 0;

	if (!ctx) {
		return -EINVAL;
	}

	if (k_mutex_lock(&ctx->lock, K_NO_WAIT)) {
		return -EBUSY;
	}

	if (!usbh_cdc_ecm_is_configured(ctx)) {
		ret = -ENODEV;
		goto done;
	}

	while (ctx->active_data_rx_xfers < CONFIG_USBH_CDC_ECM_DATA_RX_QUEUE_DEPTH) {
		buf = net_buf_alloc(&usbh_cdc_ecm_data_rx_pool, K_NO_WAIT);
		if (!buf) {
			LOG_WRN("failed to allocate data buffer for data reception");
			ret = -ENOMEM;
			break;
		}

		param.buf = buf;
		param.cb = usbh_cdc_ecm_data_rx_cb;
		param.cb_priv = ctx;
		param.ep_addr = ctx->data_in_ep_addr;

		ret = usbh_cdc_ecm_xfer(ctx, &param);
		if (ret) {
			LOG_ERR("request data RX transfer error (%d)", ret);
			net_buf_unref(buf);
			break;
		}
		ctx->active_data_rx_xfers++;
	}

done:
	(void)k_mutex_unlock(&ctx->lock);

	return ret;
}

static int usbh_cdc_ecm_data_tx_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct usbh_cdc_ecm_ctx *ctx = xfer->priv;
	bool locked = false;
	int ret = 0;

	if (!ctx) {
		ret = -EINVAL;
		goto cleanup;
	}

	(void)k_mutex_lock(&ctx->lock, K_FOREVER);
	locked = true;

	if (xfer->err) {
		LOG_WRN("data TX transfer error (%d)", xfer->err);
		goto cleanup;
	}

	if (!ctx->udev || ctx->udev != udev) {
		ret = -ENODEV;
		goto cleanup;
	}

	/** TODO: statistics processing */

cleanup:
	if (xfer->buf) {
		net_buf_unref(xfer->buf);
	}

	if (udev) {
		(void)usbh_xfer_free(udev, xfer);
	}

	if (locked) {
		(void)k_mutex_unlock(&ctx->lock);
	}

	return ret;
}

static int usbh_cdc_ecm_data_tx(struct usbh_cdc_ecm_ctx *const ctx, struct net_buf *buf)
{
	struct usbh_cdc_ecm_xfer_params param;
	struct uhc_transfer *fst_xfer = NULL;
	struct net_buf *tx_buf = NULL;
	struct net_buf *zlp_buf = NULL;
	size_t total_len;
	int ret = 0;

	if (!ctx) {
		return -EINVAL;
	}

	if (k_mutex_lock(&ctx->lock, K_NO_WAIT)) {
		return -EBUSY;
	}

	if (!usbh_cdc_ecm_is_configured(ctx)) {
		ret = -ENODEV;
		goto done;
	}

	total_len = net_buf_frags_len(buf);
	if (!total_len || total_len > ctx->max_segment_size) {
		LOG_ERR("invalid buffer length[%zu] for data TX transfer", total_len);
		ret = -EMSGSIZE;
		goto done;
	}

	if (!buf->frags) {
		tx_buf = net_buf_ref(buf);
	} else {
		tx_buf = net_buf_alloc(&usbh_cdc_ecm_data_tx_pool, K_NO_WAIT);
		if (!tx_buf) {
			LOG_WRN("failed to allocate linearized data buffer for data transmit");
			ret = -ENOMEM;
			goto done;
		}

		if (net_buf_linearize(tx_buf->data, total_len, buf, 0, total_len) != total_len) {
			LOG_ERR("fragmented buffer linearization failed for data transmit");
			net_buf_unref(tx_buf);
			ret = -EIO;
			goto done;
		}

		(void)net_buf_add(tx_buf, total_len);
	}

	param.buf = tx_buf;
	param.cb = usbh_cdc_ecm_data_tx_cb;
	param.cb_priv = ctx;
	param.ep_addr = ctx->data_out_ep_addr;

	ret = usbh_cdc_ecm_xfer(ctx, &param);
	if (ret) {
		LOG_ERR("request data TX transfer error (%d)", ret);
		net_buf_unref(tx_buf);
		goto done;
	}

	fst_xfer = param.xfer;

	if (!(total_len % ctx->data_out_ep_mps)) {
		zlp_buf = net_buf_alloc(&usbh_cdc_ecm_data_tx_pool, K_NO_WAIT);
		if (!zlp_buf) {
			LOG_WRN("failed to allocate ZLP buffer for data transmit");
			ret = -ENOMEM;
			goto dequeue_first;
		}

		param.buf = zlp_buf;

		ret = usbh_cdc_ecm_xfer(ctx, &param);
		if (ret) {
			LOG_ERR("request data TX ZLP transfer error (%d)", ret);
			net_buf_unref(zlp_buf);
			goto dequeue_first;
		}
	}

	goto done;

dequeue_first:
	if (!usbh_xfer_dequeue(ctx->udev, fst_xfer)) {
		net_buf_unref(tx_buf);
		(void)usbh_xfer_free(ctx->udev, fst_xfer);
	}

done:
	(void)k_mutex_unlock(&ctx->lock);

	return ret;
}

static int usbh_cdc_ecm_parse_descriptors(struct usbh_cdc_ecm_ctx *const ctx,
					  const struct usb_desc_header *desc)
{
	const void *desc_end = NULL;
	struct usb_if_descriptor *if_desc = NULL;
	struct cdc_header_descriptor *cdc_header_desc = NULL;
	struct cdc_union_descriptor *cdc_union_desc = NULL;
	struct cdc_ecm_descriptor *cdc_ecm_desc = NULL;
	struct usb_ep_descriptor *ep_desc = NULL;
	uint8_t current_if_num = UINT8_MAX;
	uint8_t comm_if_num = UINT8_MAX;
	uint8_t data_if_num = UINT8_MAX;
	uint8_t union_ctrl_if = UINT8_MAX;
	uint8_t union_subord_if = UINT8_MAX;
	bool cdc_header_func_ready = false;
	bool cdc_union_func_ready = false;
	bool cdc_ethernet_func_ready = false;

	if (!ctx || !desc) {
		return -EINVAL;
	}

	if (!ctx->udev) {
		return -ENODEV;
	}

	desc_end = usbh_desc_get_cfg_end(ctx->udev);
	if (!desc_end) {
		return -ENODEV;
	}

	ctx->comm_if_num = 0;
	ctx->data_if_num = 0;
	ctx->data_alt_num = 0;
	ctx->comm_in_ep_addr = 0;
	ctx->data_in_ep_addr = 0;
	ctx->data_out_ep_addr = 0;
	ctx->data_out_ep_mps = 0;
	ctx->mac_str_desc_idx = 0;
	ctx->max_segment_size = 0;

	while (desc) {
		switch (desc->bDescriptorType) {
		case USB_DESC_INTERFACE:
			if_desc = (struct usb_if_descriptor *)desc;
			current_if_num = if_desc->bInterfaceNumber;
			if (if_desc->bInterfaceClass == USB_BCC_CDC_CONTROL &&
			    if_desc->bInterfaceSubClass == ECM_SUBCLASS) {
				comm_if_num = if_desc->bInterfaceNumber;
				ctx->comm_if_num = comm_if_num;
			} else if (if_desc->bInterfaceClass == USB_BCC_CDC_DATA) {
				if (data_if_num == UINT8_MAX) {
					data_if_num = if_desc->bInterfaceNumber;
					ctx->data_if_num = data_if_num;
				}
				if (if_desc->bNumEndpoints >= 2) {
					ctx->data_alt_num = if_desc->bAlternateSetting;
				}
			}
			break;
		case USB_DESC_CS_INTERFACE:
			cdc_header_desc = (struct cdc_header_descriptor *)desc;
			if (cdc_header_desc->bDescriptorSubtype == HEADER_FUNC_DESC) {
				cdc_header_func_ready = true;
			} else if (cdc_header_desc->bDescriptorSubtype == UNION_FUNC_DESC &&
				   cdc_header_func_ready) {
				cdc_union_desc = (struct cdc_union_descriptor *)desc;
				union_ctrl_if = cdc_union_desc->bControlInterface;
				if (cdc_union_desc->bFunctionLength >=
				    sizeof(struct cdc_union_descriptor)) {
					union_subord_if = cdc_union_desc->bSubordinateInterface0;
				} else {
					return -ENODEV;
				}
				cdc_union_func_ready = true;
			} else if (cdc_header_desc->bDescriptorSubtype == ETHERNET_FUNC_DESC &&
				   cdc_union_func_ready) {
				cdc_ecm_desc = (struct cdc_ecm_descriptor *)desc;
				ctx->mac_str_desc_idx = cdc_ecm_desc->iMACAddress;
				/** TODO: Ethernet Statistics Feature */
				ctx->max_segment_size =
					sys_le16_to_cpu(cdc_ecm_desc->wMaxSegmentSize);
				/** TODO: MCFilter Feature */
				/** TODO: Power Filter Feature */
				cdc_ethernet_func_ready = true;
			}
			break;
		case USB_DESC_ENDPOINT:
			ep_desc = (struct usb_ep_descriptor *)desc;
			if (current_if_num == UINT8_MAX) {
				break;
			}
			if (current_if_num == comm_if_num) {
				if ((ep_desc->bEndpointAddress & USB_EP_DIR_MASK) ==
				    USB_EP_DIR_IN) {
					ctx->comm_in_ep_addr = ep_desc->bEndpointAddress;
				} else {
					return -ENODEV;
				}
			} else if (current_if_num == data_if_num) {
				if ((ep_desc->bEndpointAddress & USB_EP_DIR_MASK) ==
				    USB_EP_DIR_IN) {
					ctx->data_in_ep_addr = ep_desc->bEndpointAddress;
				} else {
					ctx->data_out_ep_addr = ep_desc->bEndpointAddress;
					ctx->data_out_ep_mps =
						sys_le16_to_cpu(ep_desc->wMaxPacketSize);
				}
			}
			break;
		}

		desc = usbh_desc_get_next(desc, desc_end);
	}

	if (!cdc_header_func_ready) {
		LOG_ERR("CDC Header descriptor not found");
		return -ENODEV;
	}

	if (!cdc_union_func_ready) {
		LOG_ERR("CDC Union descriptor not found");
		return -ENODEV;
	}

	if (!cdc_ethernet_func_ready) {
		LOG_ERR("CDC Ethernet descriptor not found");
		return -ENODEV;
	}

	if (comm_if_num == UINT8_MAX) {
		LOG_ERR("communication interface not found");
		return -ENODEV;
	}

	if (data_if_num == UINT8_MAX) {
		LOG_ERR("data interface not found");
		return -ENODEV;
	}

	if (union_ctrl_if != comm_if_num) {
		LOG_ERR("union control interface mismatch communication interface (%u != %u)",
			union_ctrl_if, comm_if_num);
		return -ENODEV;
	}

	if (union_subord_if != data_if_num) {
		LOG_ERR("union subordinate interface mismatch data interface (%u != %u)",
			union_subord_if, data_if_num);
		return -ENODEV;
	}

	if (!ctx->mac_str_desc_idx) {
		LOG_ERR("MAC address string descriptor index is 0");
		return -ENODEV;
	}

	if (!ctx->max_segment_size) {
		LOG_WRN("wMaxSegmentSize is 0, using default %u",
			CONFIG_USBH_CDC_ECM_DATA_BUF_POOL_SIZE);
		ctx->max_segment_size = CONFIG_USBH_CDC_ECM_DATA_BUF_POOL_SIZE;
	}

	if (!ctx->comm_in_ep_addr) {
		LOG_ERR("COMM IN endpoint not found");
		return -ENODEV;
	}

	if (!ctx->data_in_ep_addr || !ctx->data_out_ep_addr) {
		LOG_ERR("DATA endpoints not found (IN=0x%02x, OUT=0x%02x)", ctx->data_in_ep_addr,
			ctx->data_out_ep_addr);
		return -ENODEV;
	}

	LOG_INF("device information:");
	LOG_INF("  Communication: interface %u, endpoint 0x%02x", ctx->comm_if_num,
		ctx->comm_in_ep_addr);
	LOG_INF("  Data: interface %u (alt %d), IN 0x%02x, OUT 0x%02x (MPS %u)", ctx->data_if_num,
		ctx->data_alt_num, ctx->data_in_ep_addr, ctx->data_out_ep_addr,
		ctx->data_out_ep_mps);
	LOG_INF("  wMaxSegmentSize %u bytes, MAC string descriptor index %u", ctx->max_segment_size,
		ctx->mac_str_desc_idx);

	return 0;
}

static int usbh_cdc_ecm_get_mac_address(struct usbh_cdc_ecm_ctx *const ctx)
{
	struct usb_string_descriptor zero_str_desc_head;
	struct usb_string_descriptor *zero_str_desc = NULL;
	bool zero_str_desc_allocated = false;
	size_t langid_size = 0;
	uint8_t *langid_data = NULL;
	uint8_t mac_str_desc_buf[2 + NET_ETH_ADDR_LEN * 4];
	struct usb_string_descriptor *mac_str_desc =
		(struct usb_string_descriptor *)mac_str_desc_buf;
	uint8_t *mac_utf16le = NULL;
	char mac_str[NET_ETH_ADDR_LEN * 2 + 1] = {0};
	bool found_mac = false;
	int ret;

	if (!ctx || !ctx->udev) {
		return -EINVAL;
	}

	ret = usbh_req_desc_str(ctx->udev, 0, sizeof(zero_str_desc_head), 0, &zero_str_desc_head);
	if (ret) {
		return ret;
	}

	langid_size = (zero_str_desc_head.bLength - 2) / 2;

	if (zero_str_desc_head.bLength > sizeof(zero_str_desc_head)) {
		zero_str_desc = k_malloc(zero_str_desc_head.bLength);
		if (!zero_str_desc) {
			return -ENOMEM;
		}

		zero_str_desc_allocated = true;

		ret = usbh_req_desc_str(ctx->udev, 0, zero_str_desc_head.bLength, 0, zero_str_desc);
		if (ret) {
			goto cleanup;
		}
	} else if (zero_str_desc_head.bLength < sizeof(zero_str_desc_head)) {
		return -ENODEV;
	} else {
		zero_str_desc = &zero_str_desc_head;
	}

	langid_data = (uint8_t *)&zero_str_desc->bString;

	for (size_t i = 0; i < langid_size; i++) {
		ret = usbh_req_desc_str(ctx->udev, ctx->mac_str_desc_idx,
					ARRAY_SIZE(mac_str_desc_buf),
					sys_get_le16(&langid_data[i * 2]), mac_str_desc);
		if (ret) {
			continue;
		}

		if (mac_str_desc->bLength != ARRAY_SIZE(mac_str_desc_buf)) {
			continue;
		}

		mac_utf16le = (uint8_t *)&mac_str_desc->bString;

		for (size_t j = 0; j < NET_ETH_ADDR_LEN * 2; j++) {
			mac_str[j] = (char)sys_get_le16(&mac_utf16le[j * 2]);
		}

		if (hex2bin(mac_str, NET_ETH_ADDR_LEN * 2, ctx->eth_mac.addr, NET_ETH_ADDR_LEN) ==
		    NET_ETH_ADDR_LEN) {
			if (net_eth_is_addr_valid(&ctx->eth_mac)) {
				found_mac = true;
				break;
			}
		}
	}

	if (!found_mac) {
		LOG_WRN("failed to retrieve valid MAC address");
		ret = -ENODEV;
	} else {
		LOG_INF("device MAC address: %02x:%02x:%02x:%02x:%02x:%02x", ctx->eth_mac.addr[0],
			ctx->eth_mac.addr[1], ctx->eth_mac.addr[2], ctx->eth_mac.addr[3],
			ctx->eth_mac.addr[4], ctx->eth_mac.addr[5]);
		ret = 0;
	}

cleanup:
	if (zero_str_desc_allocated) {
		k_free(zero_str_desc);
	}

	return ret;
}

static int usbh_cdc_ecm_init(struct usbh_class_data *const c_data,
			     struct usbh_context *const uhs_ctx)
{
	struct device *dev = c_data->priv;
	struct usbh_cdc_ecm_ctx *ctx = dev->data;

	ARG_UNUSED(uhs_ctx);

	(void)k_mutex_init(&ctx->lock);

	return 0;
}

static int usbh_cdc_ecm_completion_cb(struct usbh_class_data *const c_data,
				      struct uhc_transfer *const xfer)
{
	ARG_UNUSED(c_data);
	ARG_UNUSED(xfer);

	return 0;
}

static int usbh_cdc_ecm_probe(struct usbh_class_data *const c_data, struct usb_device *const udev,
			      const uint8_t iface)
{
	struct device *dev = c_data->priv;
	struct usbh_cdc_ecm_ctx *ctx = dev->data;
	const void *const desc_beg = usbh_desc_get_cfg(udev);
	const void *const desc_end = usbh_desc_get_cfg_end(udev);
	const struct usb_desc_header *desc;
	const struct usb_association_descriptor *assoc_desc;
	struct usbh_cdc_ecm_req_params param;
	struct usbh_cdc_ecm_msg msg;
	int ret;

	(void)k_mutex_lock(&ctx->lock, K_FOREVER);

	ctx->udev = udev;
	ctx->link_state = false;
	ctx->upload_speed = 0;
	ctx->download_speed = 0;
	ctx->active_data_rx_xfers = 0;

	desc = usbh_desc_get_by_iface(desc_beg, desc_end, iface);
	if (!desc) {
		LOG_ERR("no descriptor found for interface %u", iface);
		ret = -ENODEV;
		goto done;
	}

	if (desc->bDescriptorType == USB_DESC_INTERFACE_ASSOC) {
		assoc_desc = (struct usb_association_descriptor *)desc;
		desc = usbh_desc_get_by_iface(desc, desc_end, assoc_desc->bFirstInterface);
		if (!desc) {
			LOG_ERR("no descriptor (IAD) found for interface %u", iface);
			ret = -ENODEV;
			goto done;
		}
	}

	ret = usbh_cdc_ecm_parse_descriptors(ctx, desc);
	if (ret) {
		LOG_ERR("parse descriptor error (%d)", ret);
		goto done;
	}

	if (ctx->data_alt_num) {
		ret = usbh_device_interface_set(ctx->udev, ctx->data_if_num, ctx->data_alt_num,
						false);
		if (ret) {
			LOG_ERR("set data interface alternate setting error (%d)", ret);
			goto done;
		}
	}

	ret = usbh_cdc_ecm_get_mac_address(ctx);
	if (ret) {
		LOG_ERR("get MAC address error (%d)", ret);
		goto done;
	}

	ret = net_if_set_link_addr(ctx->iface, ctx->eth_mac.addr, ARRAY_SIZE(ctx->eth_mac.addr),
				   NET_LINK_ETHERNET);
	if (ret) {
		LOG_ERR("set MAC address error (%d)", ret);
		goto done;
	}

	param.if_num = ctx->comm_if_num;
	param.bRequest = SET_ETHERNET_PACKET_FILTER;
	param.eth_pkt_filter_bitmap =
		PACKET_TYPE_BROADCAST | PACKET_TYPE_DIRECTED | PACKET_TYPE_ALL_MULTICAST;
	ret = usbh_cdc_ecm_req(ctx, &param);
	if (ret) {
		LOG_ERR("set default ethernet packet filter error (%d)", ret);
		goto done;
	}

	msg.ctx = ctx;
	msg.event = CDC_ECM_EVENT_TASK_START;
	ret = usbh_cdc_ecm_msgq_put(&msg);
	if (ret) {
		LOG_ERR("send task start message error (%d)", ret);
		goto done;
	}

	LOG_INF("device probed");

done:
	if (ret) {
		ctx->udev = NULL;
	}

	(void)k_mutex_unlock(&ctx->lock);

	return ret;
}

static int usbh_cdc_ecm_removed(struct usbh_class_data *const c_data)
{
	struct device *dev = c_data->priv;
	struct usbh_cdc_ecm_ctx *ctx = dev->data;

	(void)k_mutex_lock(&ctx->lock, K_FOREVER);

	ctx->udev = NULL;
	ctx->link_state = false;
	ctx->upload_speed = 0;
	ctx->download_speed = 0;

	net_if_carrier_off(ctx->iface);

	(void)k_mutex_unlock(&ctx->lock);

	while (true) {
		(void)k_mutex_lock(&ctx->lock, K_FOREVER);
		if (ctx->active_data_rx_xfers) {
			(void)k_mutex_unlock(&ctx->lock);
			break;
		}
		(void)k_mutex_unlock(&ctx->lock);
		k_sleep(K_MSEC(10));
	}

	LOG_INF("device removed");

	return 0;
}

static int usbh_cdc_ecm_suspended(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static int usbh_cdc_ecm_resumed(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static struct usbh_class_api usbh_cdc_ecm_class_api = {
	.init = usbh_cdc_ecm_init,
	.completion_cb = usbh_cdc_ecm_completion_cb,
	.probe = usbh_cdc_ecm_probe,
	.removed = usbh_cdc_ecm_removed,
	.suspended = usbh_cdc_ecm_suspended,
	.resumed = usbh_cdc_ecm_resumed,
};

static void eth_usbh_cdc_ecm_iface_init(struct net_if *iface)
{
	struct usbh_cdc_ecm_ctx *ctx = net_if_get_device(iface)->data;

	(void)k_mutex_lock(&ctx->lock, K_FOREVER);

	ctx->iface = iface;

	ethernet_init(ctx->iface);

	net_if_carrier_off(ctx->iface);

	(void)k_mutex_unlock(&ctx->lock);
}

#if defined(CONFIG_NET_STATISTICS_ETHERNET)
struct net_stats_eth *eth_usbh_cdc_ecm_get_stats(const struct device *dev)
{

	struct usbh_cdc_ecm_data *priv = dev->data;

	return &priv->stats;
}
#endif

static int eth_usbh_cdc_ecm_set_config(const struct device *dev, enum ethernet_config_type type,
				       const struct ethernet_config *config)
{
	struct usbh_cdc_ecm_ctx *ctx = dev->data;
	int ret = 0;

	switch (type) {
	case ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
		ret = net_if_set_link_addr(ctx->iface, (uint8_t *)config->mac_address.addr,
					   NET_ETH_ADDR_LEN, NET_LINK_ETHERNET);
		break;
#if defined(CONFIG_NET_PROMISCUOUS_MODE)
	case ETHERNET_CONFIG_TYPE_PROMISC_MODE:
		if (config->promisc_mode) {
			;
		} else {
			;
		}
		break;
#endif
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}

static int eth_usbh_cdc_ecm_send(const struct device *dev, struct net_pkt *pkt)
{
	struct usbh_cdc_ecm_ctx *ctx = dev->data;

	if (!pkt) {
		return -EINVAL;
	}

	return usbh_cdc_ecm_data_tx(ctx, pkt->buffer);
}

static struct ethernet_api eth_usbh_cdc_ecm_api = {
	.iface_api.init = eth_usbh_cdc_ecm_iface_init,
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	.get_stats = eth_usbh_cdc_ecm_get_stats,
#endif
	.set_config = eth_usbh_cdc_ecm_set_config,
	.send = eth_usbh_cdc_ecm_send,
};

static struct usbh_class_filter cdc_ecm_filters[] = {{
	.flags = USBH_CLASS_MATCH_CODE_TRIPLE,
	.class = USB_BCC_CDC_CONTROL,
	.sub = ECM_SUBCLASS,
}};

static void usbh_cdc_ecm_thread(void *arg1, void *arg2, void *arg3)
{
	struct usbh_cdc_ecm_msg msg, new_msg;
	struct usbh_cdc_ecm_ctx *ctx;
	int err;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)k_msgq_get(&usbh_cdc_ecm_msgq, &msg, K_FOREVER);

		ctx = msg.ctx;

		if (!ctx) {
			continue;
		}

		err = 0;

		switch (msg.event) {
		case CDC_ECM_EVENT_TASK_START:
			(void)k_mutex_lock(&ctx->lock, K_NO_WAIT);
			if (!usbh_cdc_ecm_is_configured(ctx)) {
				err = -ENODEV;
			} else {
				new_msg.ctx = ctx;
				new_msg.event = CDC_ECM_EVENT_COMM_RX;
				err = usbh_cdc_ecm_msgq_put(&new_msg);
			}
			(void)k_mutex_unlock(&ctx->lock);
			break;
		case CDC_ECM_EVENT_COMM_RX:
			err = usbh_cdc_ecm_comm_rx(ctx);
			break;
		case CDC_ECM_EVENT_DATA_RX:
			err = usbh_cdc_ecm_data_rx_queue(ctx);
			break;
		default:
			break;
		}

		if (err && err != -ENODEV) {
			LOG_WRN("thread event[%d] error (%d)", msg.event, err);
		}
	}
}

K_THREAD_DEFINE(usbh_cdc_ecm, CONFIG_USBH_CDC_ECM_STACK_SIZE, usbh_cdc_ecm_thread, NULL, NULL, NULL,
		CONFIG_SYSTEM_WORKQUEUE_PRIORITY, 0, 0);

#define USBH_CDC_ECM_DT_DEVICE_DEFINE(n)                                                           \
	static struct usbh_cdc_ecm_ctx cdc_ecm_ctx_##n;                                            \
                                                                                                   \
	ETH_NET_DEVICE_DT_INST_DEFINE(n, NULL, NULL, &cdc_ecm_ctx_##n, NULL,                       \
				      CONFIG_ETH_INIT_PRIORITY, &eth_usbh_cdc_ecm_api,             \
				      NET_ETH_MTU);                                                \
                                                                                                   \
	USBH_DEFINE_CLASS(cdc_ecm_c_data_##n, &usbh_cdc_ecm_class_api,                             \
			  (void *)DEVICE_DT_INST_GET(n), cdc_ecm_filters)

DT_INST_FOREACH_STATUS_OKAY(USBH_CDC_ECM_DT_DEVICE_DEFINE);
