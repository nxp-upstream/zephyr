/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include "protocol.h"

#include <sample_usbd.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>

LOG_MODULE_REGISTER(lc3_codec_test, LOG_LEVEL_INF);

static const struct device *const cdc = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
RING_BUF_DECLARE(rx_ring, 8192U);
RING_BUF_DECLARE(tx_ring, 4096U);
K_SEM_DEFINE(io_event, 0, 1);
static struct k_spinlock io_lock;
static bool connected;
static bool rx_throttled;
static uint32_t generation;
static uint32_t worker_generation;

static void connection_set(bool active)
{
	k_spinlock_key_t key = k_spin_lock(&io_lock);

	connected = active;
	generation++;
	uart_irq_rx_disable(cdc);
	uart_irq_tx_disable(cdc);
	k_spin_unlock(&io_lock, key);
	k_sem_give(&io_event);
}

static void usb_message(struct usbd_context *const context, const struct usbd_msg *message)
{
	if (message->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		if (uart_line_ctrl_get(message->dev, UART_LINE_CTRL_DTR, &dtr) == 0) {
			connection_set(dtr != 0U);
		}
	} else if (message->type == USBD_MSG_RESET || message->type == USBD_MSG_UDC_ERROR ||
		   message->type == USBD_MSG_STACK_ERROR ||
		   (message->type == USBD_MSG_CONFIGURATION && message->status == 0)) {
		connection_set(false);
	}
	if (usbd_can_detect_vbus(context)) {
		if (message->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(context) != 0) {
				LOG_ERR("USB enable failed");
			}
		} else if (message->type == USBD_MSG_VBUS_REMOVED) {
			connection_set(false);
			if (usbd_disable(context) != 0) {
				LOG_ERR("USB disable failed");
			}
		}
	}
}

static void uart_interrupt(const struct device *device, void *user_data)
{
	ARG_UNUSED(user_data);

	while (true) {
		uart_irq_update(device);
		if (uart_irq_is_pending(device) <= 0) {
			break;
		}

		k_spinlock_key_t key = k_spin_lock(&io_lock);
		bool progress = false;
		uint8_t *buffer;
		uint32_t space;
		int transferred;
		int tx_ready;

		if (!connected || generation != worker_generation) {
			k_spin_unlock(&io_lock, key);
			break;
		}
		if (!rx_throttled && uart_irq_rx_ready(device) > 0) {
			space = MIN(ring_buf_put_ptr(&rx_ring, &buffer, 0U), 256U);
			if (space == 0U) {
				uart_irq_rx_disable(device);
				rx_throttled = true;
			} else {
				transferred = uart_fifo_read(device, buffer, space);
				ring_buf_commit(&rx_ring, transferred > 0 ? transferred : 0U);
				progress = transferred > 0;
			}
		}
		tx_ready = uart_irq_tx_ready(device);
		if (tx_ready > 0) {
			space = MIN(ring_buf_get_ptr(&tx_ring, &buffer, 0U), MIN(tx_ready, 256U));
			if (space == 0U) {
				uart_irq_tx_disable(device);
			} else {
				transferred = uart_fifo_fill(device, buffer, space);
				ring_buf_consume(&tx_ring, transferred > 0 ? transferred : 0U);
				progress = progress || transferred > 0;
			}
		}
		k_spin_unlock(&io_lock, key);
		if (progress) {
			k_sem_give(&io_event);
		} else {
			break;
		}
	}
}

static int send_bytes(const uint8_t *data, size_t length)
{
	int64_t deadline = k_uptime_get() + 30000;

	while (length > 0U) {
		k_spinlock_key_t key = k_spin_lock(&io_lock);
		uint32_t sent;

		if (!connected || generation != worker_generation) {
			k_spin_unlock(&io_lock, key);
			return -ECONNRESET;
		}
		sent = ring_buf_put(&tx_ring, data, length);
		uart_irq_tx_enable(cdc);
		k_spin_unlock(&io_lock, key);
		data += sent;
		length -= sent;
		if (sent == 0U) {
			if (k_uptime_get() >= deadline) {
				connection_set(false);
				return -ETIMEDOUT;
			}
			k_sem_take(&io_event, K_MSEC(100));
		}
	}
	return 0;
}

static int send_packet(uint8_t type, const void *data, size_t length)
{
	uint8_t header[PROTOCOL_HEADER_SIZE] = {type};
	int ret;

	sys_put_le32(length, header + 1U);
	ret = send_bytes(header, sizeof(header));
	if (ret != 0) {
		return ret;
	}
	return send_bytes(data, length);
}

int main(void)
{
	struct usbd_context *usb;
	uint8_t chunk[256];
	int ret;

	if (!device_is_ready(cdc)) {
		LOG_ERR("USB CDC device not ready");
		return -ENODEV;
	}
	ret = uart_irq_callback_user_data_set(cdc, uart_interrupt, NULL);
	if (ret != 0) {
		LOG_ERR("USB CDC IRQ callback: %d", ret);
		return ret;
	}
	usb = sample_usbd_init_device(usb_message);
	if (usb == NULL) {
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(usb)) {
		ret = usbd_enable(usb);
		if (ret != 0) {
			return ret;
		}
	}
	LOG_INF("LC3 USB test ready; console remains on debug UART");

	while (true) {
		k_spinlock_key_t key = k_spin_lock(&io_lock);
		size_t length = 0U;

		if (generation != worker_generation) {
			protocol_reset();
			ring_buf_reset(&rx_ring);
			ring_buf_reset(&tx_ring);
			worker_generation = generation;
			rx_throttled = false;
			if (connected) {
				uart_irq_rx_enable(cdc);
			}
		}
		if (connected) {
			length = ring_buf_get(&rx_ring, chunk, sizeof(chunk));
			if (rx_throttled && length > 0U) {
				rx_throttled = false;
				uart_irq_rx_enable(cdc);
			}
		}
		k_spin_unlock(&io_lock, key);
		if (length == 0U) {
			k_sem_take(&io_event, K_FOREVER);
			continue;
		}
		ret = protocol_feed(chunk, length, send_packet);
		if (ret != 0) {
			LOG_WRN("USB response interrupted: %d", ret);
			protocol_reset();
		}
	}
	return 0;
}
