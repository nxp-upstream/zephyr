/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The microphone stream always flows while the phone is connected; this is
 * what the phone is asked to do with it. The mode outlives a connection, so a
 * phone that reconnects is told the mode in force.
 *
 * The user button cycles speaker, inject, off; the first user LED is lit in
 * speaker mode and the second in inject mode. The shell command "mic" does
 * the same from a terminal, and is all there is on a board without them.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/shell/shell.h>

#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mic_mode);

#define LED_SPEAKER_NODE DT_ALIAS(led0)
#define LED_INJECT_NODE  DT_ALIAS(led1)

#if DT_NODE_HAS_STATUS_OKAY(LED_SPEAKER_NODE)
static const struct gpio_dt_spec led_speaker = GPIO_DT_SPEC_GET(LED_SPEAKER_NODE, gpios);
#else
static const struct gpio_dt_spec led_speaker;
#endif

#if DT_NODE_HAS_STATUS_OKAY(LED_INJECT_NODE)
static const struct gpio_dt_spec led_inject = GPIO_DT_SPEC_GET(LED_INJECT_NODE, gpios);
#else
static const struct gpio_dt_spec led_inject;
#endif

static enum sc_mic_mode mic_mode = SC_MIC_MODE_SPEAKER;

const char *mic_mode_name(enum sc_mic_mode mode)
{
	switch (mode) {
	case SC_MIC_MODE_OFF:
		return "off";
	case SC_MIC_MODE_SPEAKER:
		return "speaker";
	case SC_MIC_MODE_INJECT:
		return "inject";
	default:
		return "?";
	}
}

enum sc_mic_mode mic_mode_get(void)
{
	return mic_mode;
}

static void led_init(const struct gpio_dt_spec *led)
{
	if (led->port != NULL && gpio_is_ready_dt(led)) {
		(void)gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
	}
}

static void led_show(const struct gpio_dt_spec *led, bool on)
{
	if (led->port != NULL) {
		(void)gpio_pin_set_dt(led, on ? 1 : 0);
	}
}

void mic_mode_set(enum sc_mic_mode mode)
{
	mic_mode = mode;
	led_show(&led_speaker, mode == SC_MIC_MODE_SPEAKER);
	led_show(&led_inject, mode == SC_MIC_MODE_INJECT);
	LOG_INF("Microphone mode: %s", mic_mode_name(mode));
	control_send_mic_mode(mode);
}

void mic_mode_init(void)
{
	led_init(&led_speaker);
	led_init(&led_inject);
	mic_mode_set(mic_mode);
}

#if DT_HAS_COMPAT_STATUS_OKAY(gpio_keys)
static void button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->code != INPUT_KEY_0 || evt->value == 0) {
		return;
	}

	switch (mic_mode) {
	case SC_MIC_MODE_SPEAKER:
		mic_mode_set(SC_MIC_MODE_INJECT);
		break;
	case SC_MIC_MODE_INJECT:
		mic_mode_set(SC_MIC_MODE_OFF);
		break;
	default:
		mic_mode_set(SC_MIC_MODE_SPEAKER);
		break;
	}
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET_ANY(gpio_keys), button_cb, NULL);
#endif

#if defined(CONFIG_SHELL)
static int cmd_mic(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Microphone mode: %s", mic_mode_name(mic_mode));
		return 0;
	}

	if (strcmp(argv[1], "off") == 0) {
		mic_mode_set(SC_MIC_MODE_OFF);
	} else if (strcmp(argv[1], "speaker") == 0) {
		mic_mode_set(SC_MIC_MODE_SPEAKER);
	} else if (strcmp(argv[1], "inject") == 0) {
		mic_mode_set(SC_MIC_MODE_INJECT);
	} else {
		shell_error(sh, "Unknown mode %s", argv[1]);
		return -EINVAL;
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(mic, NULL, "Microphone mode: mic [off|speaker|inject]", cmd_mic, 1, 1);
#endif
