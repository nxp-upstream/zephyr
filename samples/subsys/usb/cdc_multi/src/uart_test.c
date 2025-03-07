/*
 * Copyright 2025 NXP
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#ifdef CONFIG_LOG
	#include <zephyr/logging/log.h>
	LOG_MODULE_REGISTER(uart_test, CONFIG_UART_TEST_LOG_LEVEL);
#endif

#define UART1_NODE DT_NODELABEL(test_uart1)
#define UART2_NODE DT_NODELABEL(test_uart2)
static const struct device *const uart1_dev = DEVICE_DT_GET(UART1_NODE);
static const struct device *const uart2_dev = DEVICE_DT_GET(UART2_NODE);

#define SLEEP_TIME_MS 1000
#define TEST_BUFFER_LEN 20
const uint8_t test_pattern[TEST_BUFFER_LEN] = "UART Hello";
static uint8_t test1_buffer[TEST_BUFFER_LEN];
static uint8_t test2_buffer[TEST_BUFFER_LEN];
static volatile uint8_t uart1_error_counter;
static volatile uint8_t uart2_error_counter;
static int tx1_byte_offset;
static int rx1_byte_offset;
static int tx2_byte_offset;
static int rx2_byte_offset;

/* Define UART Test Thread */
#define UART_STACKSIZE	8096U
#define UART_PRIORITY	2U

void UART_Test_thread(void *dev, void *dummy2, void *dummy3);
K_THREAD_DEFINE(UART_Test_id, UART_STACKSIZE, UART_Test_thread,
		NULL, NULL, NULL, UART_PRIORITY, 0U, 0U);

/*
 * ISR for UART TX action
 */
static void uart_tx_interrupt_service(const struct device *dev, int *tx_byte_offset)
{
	uint8_t bytes_sent = 0;
	uint8_t *tx_data_pointer = (uint8_t *)(test_pattern + *tx_byte_offset);

	if (*tx_byte_offset < TEST_BUFFER_LEN) {
		bytes_sent = uart_fifo_fill(dev, tx_data_pointer, 1);
		*tx_byte_offset += bytes_sent;
	} else {
		*tx_byte_offset = 0;
		uart_irq_tx_disable(dev);
	}
}

/*
 * ISR for UART RX action
 */
static void uart_rx_interrupt_service(const struct device *dev, uint8_t *receive_buffer_pointer,
				      int *rx_byte_offset)
{
	int rx_data_length = 0;

	do {
		rx_data_length = uart_fifo_read(dev, receive_buffer_pointer + *rx_byte_offset,
						TEST_BUFFER_LEN);
		*rx_byte_offset += rx_data_length;
	} while (rx_data_length);
}

/*
 * Callback function for UART1 interrupt based transmission test
 */
static void interrupt_driven_uart1_callback(const struct device *dev, void *user_data)
{
	int err;

	uart_irq_update(dev);
	err = uart_err_check(dev);
	if (err != 0) {
		uart1_error_counter++;
	}
	while (uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uart_rx_interrupt_service(dev, (uint8_t *)user_data, &rx1_byte_offset);
		}
		if (uart_irq_tx_ready(dev)) {
			uart_tx_interrupt_service(dev, &tx1_byte_offset);
		}
	}
}

/*
 * Callback function for UART2 interrupt based transmission test
 */
static void interrupt_driven_uart2_callback(const struct device *dev, void *user_data)
{
	int err;

	uart_irq_update(dev);
	err = uart_err_check(dev);
	if (err != 0) {
		uart2_error_counter++;
	}
	while (uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uart_rx_interrupt_service(dev, (uint8_t *)user_data, &rx2_byte_offset);
		}
		if (uart_irq_tx_ready(dev)) {
			uart_tx_interrupt_service(dev, &tx2_byte_offset);
		}
	}
}

void UART_Test_thread(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);

	int err;
	struct uart_config test_uart_config = {.baudrate = DT_PROP(UART1_NODE, current_speed),
						.parity = UART_CFG_PARITY_NONE,
						.stop_bits = UART_CFG_STOP_BITS_1,
						.data_bits = UART_CFG_DATA_BITS_8,
						.flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS };

	err = uart_configure(uart1_dev, &test_uart_config);
	if (err) {
		LOG_ERR("'uart1_configure' api call - unexpected error: %d", err);
	}

	err = uart_configure(uart2_dev, &test_uart_config);
	if (err) {
		LOG_ERR("'uart2_configure' api call - unexpected error: %d", err);
	}

	err = uart_irq_callback_set(uart1_dev, interrupt_driven_uart1_callback);
	if (err) {
		LOG_ERR("Unexpected error when setting callback for UART1 %d", err);
	}

	err = uart_irq_callback_set(uart2_dev, interrupt_driven_uart2_callback);
	if (err) {
		LOG_ERR("Unexpected error when setting callback for UART2 %d", err);
	}

	while(1) {
		err = uart_irq_callback_user_data_set(uart1_dev,
						interrupt_driven_uart1_callback,
						(void *)test1_buffer);
		if (err) {
			LOG_ERR("Unexpected error when setting user data for UART1 callback %d", err);
		}

		err = uart_irq_callback_user_data_set(uart2_dev,
						interrupt_driven_uart2_callback,
						(void *)test2_buffer);
		if (err) {
			LOG_ERR("Unexpected error when setting user data for UART2 callback %d", err);
		}

		memset(test1_buffer, 0U, TEST_BUFFER_LEN);
		memset(test2_buffer, 0U, TEST_BUFFER_LEN);

		uart_irq_err_enable(uart1_dev);
		uart_irq_err_enable(uart2_dev);

		uart_irq_rx_enable(uart1_dev);
		uart_irq_rx_enable(uart2_dev);

		uart_irq_tx_enable(uart1_dev);
		uart_irq_tx_enable(uart2_dev);

		/* wait for the tramission to finish (no polling is intentional) */
		k_sleep(K_MSEC(SLEEP_TIME_MS));

		err = 0;
		for (int index = 0; index < TEST_BUFFER_LEN; index++) {
			if (test1_buffer[index] != test_pattern[index]) {
				LOG_ERR("test1_buffer index %d does not match pattern", index);
				err++;
				break;
			}
			if (test2_buffer[index] != test_pattern[index]) {
				LOG_ERR("test2_buffer index %d does not match pattern", index);
				err++;
				break;
			}
		}

		if (err == 0) {
			LOG_INF("UART test passed");
		}
		tx1_byte_offset = 0;
		rx1_byte_offset = 0;
		tx2_byte_offset = 0;
		rx2_byte_offset = 0;
			}
}
