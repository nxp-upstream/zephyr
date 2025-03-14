/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dual_usbd, LOG_LEVEL_DBG);

#define ZEPHYR_PROJECT_USB_VID 0x2fe3

USBD_DEVICE_DEFINE(usb0_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
	ZEPHYR_PROJECT_USB_VID, CONFIG_SAMPLE_USBD_PID);

USBD_DEVICE_DEFINE(usb1_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc1)),
	ZEPHYR_PROJECT_USB_VID, CONFIG_SAMPLE_USBD_PID);

USBD_DESC_LANG_DEFINE(usb0_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb0_mfr, CONFIG_SAMPLE_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(usb0_product, CONFIG_SAMPLE_USBD_PRODUCT);
USBD_DESC_SERIAL_NUMBER_DEFINE(usb0_sn);

USBD_DESC_LANG_DEFINE(usb1_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb1_mfr, CONFIG_SAMPLE_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(usb1_product, CONFIG_SAMPLE_USBD_PRODUCT);
USBD_DESC_SERIAL_NUMBER_DEFINE(usb1_sn);


USBD_DESC_CONFIG_DEFINE(usb_fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(usb_hs_cfg_desc, "HS Configuration");

/* doc configuration instantiation start */
static const uint8_t attributes =
	(IS_ENABLED(CONFIG_SAMPLE_USBD_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0) |
	(IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0);

/* Speed configuration */
USBD_CONFIGURATION_DEFINE(usb_fs_config, attributes,
			CONFIG_SAMPLE_USBD_MAX_POWER, &usb_fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(usb_hs_config, attributes,
			CONFIG_SAMPLE_USBD_MAX_POWER, &usb_hs_cfg_desc);
/* doc configuration instantiation end */

static int usb0_register_cdc_acm_n(struct usbd_context *const uds_ctx,
			const enum usbd_speed speed)
{
	struct usbd_config_node *cfg_nd;
	int err;

	if (speed == USBD_SPEED_HS) {
		cfg_nd = &usb_hs_config;
	} else {
		cfg_nd = &usb_fs_config;
	}

	err = usbd_add_configuration(uds_ctx, speed, cfg_nd);
	if (err) {
		LOG_ERR("Failed to add configuration");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_0", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm0");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_1", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm1");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_2", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm2");
		return err;
	}

	return usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
}

static int usb1_register_cdc_acm_n(struct usbd_context *const uds_ctx,
	const enum usbd_speed speed)
{
	struct usbd_config_node *cfg_nd;
	int err;

	if (speed == USBD_SPEED_HS) {
		cfg_nd = &usb_hs_config;
	} else {
		cfg_nd = &usb_fs_config;
	}

	err = usbd_add_configuration(uds_ctx, speed, cfg_nd);
	if (err) {
		LOG_ERR("Failed to add configuration");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_3", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm3");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_4", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm4");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_5", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm5");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_6", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm6");
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_7", speed, 1);
	if (err) {
		LOG_ERR("Failed to register cdc_acm7");
		return err;
	}

	return usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
}

struct usbd_context *usb0_init_device(usbd_msg_cb_t msg_cb)
{
	int err = 0;
	enum usbd_speed speed;

	err = usbd_add_descriptor(&usb0_usbd, &usb0_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb0_usbd, &usb0_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb0_usbd, &usb0_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb0_usbd, &usb0_sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}


	speed = usbd_caps_speed(&usb0_usbd);
	err = usb0_register_cdc_acm_n(&usb0_usbd, speed);
	if (err) {
		return NULL;
	}

	err = usbd_msg_register_cb(&usb0_usbd, msg_cb);
	if (err) {
		LOG_ERR("Failed to register message callback");
		return NULL;
	}

	err = usbd_init(&usb0_usbd);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}

	return &usb0_usbd;
}

struct usbd_context *usb1_init_device(usbd_msg_cb_t msg_cb)
{
	int err = 0;
	enum usbd_speed speed;

	err = usbd_add_descriptor(&usb1_usbd, &usb1_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb1_usbd, &usb1_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb1_usbd, &usb1_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb1_usbd, &usb1_sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}


	speed = usbd_caps_speed(&usb1_usbd);
	err = usb1_register_cdc_acm_n(&usb1_usbd, speed);
	if (err) {
		return NULL;
	}

	err = usbd_msg_register_cb(&usb1_usbd, msg_cb);
	if (err) {
		LOG_ERR("Failed to register usb1 message callback");
		return NULL;
	}

	err = usbd_init(&usb1_usbd);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}

	return &usb1_usbd;
}

/*
 * SYS_INIT(usb0_init_device, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
 * SYS_INIT(usb1_init_device, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
 */
