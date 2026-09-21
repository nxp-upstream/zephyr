/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbh.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

/*
 * The HID Boot class reports keyboard and mouse events through the input
 * subsystem. Each probed interface gets its own pseudo device, so the callback
 * is registered for all devices and tells them apart by evt->dev.
 */
static void hid_input_cb(struct input_event *evt, void *user_data)
{
	const char *name;

	ARG_UNUSED(user_data);

	name = evt->dev != NULL ? evt->dev->name : "unknown";

	switch (evt->type) {
	case INPUT_EV_KEY:
		LOG_INF("%s: code %u %s", name, evt->code,
			evt->value != 0 ? "pressed" : "released");
		break;
	case INPUT_EV_REL:
		LOG_INF("%s: %s %d", name, evt->code == INPUT_REL_X ? "x" : "y", evt->value);
		break;
	default:
		LOG_WRN("%s: unhandled event type %u", name, evt->type);
		break;
	}
}
INPUT_CALLBACK_DEFINE(NULL, hid_input_cb, NULL);

int main(void)
{
	int err;

	err = usbh_init(&uhs_ctx);
	if (err != 0) {
		LOG_ERR("Failed to initialize USB host support, %d", err);
		return err;
	}

	err = usbh_enable(&uhs_ctx);
	if (err != 0) {
		LOG_ERR("Failed to enable USB host support, %d", err);
		return err;
	}

	LOG_INF("USB host HID sample started, connect a keyboard or a mouse");

	return 0;
}
