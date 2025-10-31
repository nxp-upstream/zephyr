/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB Host CDC ECM (Ethernet Control Model) class driver
 *
 * This file contains the public API for the USB Host CDC ECM class driver.
 * The driver enables communication with CDC ECM devices such as smartphones,
 * tablets, and other devices providing network connectivity via USB.
 */

#ifndef ZEPHYR_INCLUDE_USB_HOST_USBH_CDC_ECM_H_
#define ZEPHYR_INCLUDE_USB_HOST_USBH_CDC_ECM_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USBH_DEVICE_LINK_UP      3

int usbh_cdc_ecm_set_signal(const struct device *dev, struct k_poll_signal *sig);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_USB_HOST_USBH_CDC_ECM_H_ */
