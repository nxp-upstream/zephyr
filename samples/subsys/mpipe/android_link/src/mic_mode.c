/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The demo mode: what the phone does with the board's microphone, and what it
 * sends on its audio connection. The streams always flow while the phone is
 * connected; the mode only says what each side does with them. It outlives a
 * connection, so a phone that reconnects is told the mode in force.
 *
 * The user button cycles mirror, inject, intercom, off. The green LED is lit
 * when the board's microphone plays on the phone (mirror, intercom), the red
 * one when the phone's microphone is involved (inject, intercom). The shell
 * command "mode" does the same from a terminal, and is all there is on a
 * board without button or LEDs.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/shell/shell.h>

#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(demo_mode);

#define LED_GREEN_NODE DT_ALIAS(led0)
#define LED_RED_NODE   DT_ALIAS(led1)

#if DT_NODE_HAS_STATUS_OKAY(LED_GREEN_NODE)
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
#else
static const struct gpio_dt_spec led_green;
#endif

#if DT_NODE_HAS_STATUS_OKAY(LED_RED_NODE)
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
#else
static const struct gpio_dt_spec led_red;
#endif

static enum demo_mode demo_mode = DEMO_MODE_MIRROR;

/* What each mode asks of the phone */
static const struct {
	const char *name;
	enum sc_mic_mode mic_mode;
	enum sc_audio_source audio_source;
	bool green;
	bool red;
} modes[] = {
	[DEMO_MODE_MIRROR] = {"mirror", SC_MIC_MODE_SPEAKER, SC_AUDIO_SOURCE_PLAYBACK, true, false},
	[DEMO_MODE_INJECT] = {"inject", SC_MIC_MODE_INJECT, SC_AUDIO_SOURCE_PLAYBACK, false, true},
	[DEMO_MODE_INTERCOM] = {"intercom", SC_MIC_MODE_SPEAKER, SC_AUDIO_SOURCE_MIC, true, true},
	[DEMO_MODE_OFF] = {"off", SC_MIC_MODE_OFF, SC_AUDIO_SOURCE_PLAYBACK, false, false},
};

const char *demo_mode_name(enum demo_mode mode)
{
	return (mode < ARRAY_SIZE(modes)) ? modes[mode].name : "?";
}

enum demo_mode demo_mode_get(void)
{
	return demo_mode;
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

void demo_mode_announce(void)
{
	control_send_mic_mode(modes[demo_mode].mic_mode);
	control_send_audio_source(modes[demo_mode].audio_source);
}

void demo_mode_set(enum demo_mode mode)
{
	if (mode >= ARRAY_SIZE(modes)) {
		return;
	}

	demo_mode = mode;
	led_show(&led_green, modes[mode].green);
	led_show(&led_red, modes[mode].red);
	LOG_INF("Mode: %s", modes[mode].name);
	demo_mode_announce();
}

void demo_mode_init(void)
{
	led_init(&led_green);
	led_init(&led_red);
	demo_mode_set(demo_mode);
}

#if DT_HAS_COMPAT_STATUS_OKAY(gpio_keys)
static void button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->code != INPUT_KEY_0 || evt->value == 0) {
		return;
	}

	demo_mode_set((demo_mode + 1) % ARRAY_SIZE(modes));
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET_ANY(gpio_keys), button_cb, NULL);
#endif

#if defined(CONFIG_SHELL)
static int cmd_mode(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Mode: %s", modes[demo_mode].name);
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strcmp(argv[1], modes[i].name) == 0) {
			demo_mode_set(i);
			return 0;
		}
	}

	shell_error(sh, "Unknown mode %s", argv[1]);
	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(mode, NULL, "Demo mode: mode [mirror|inject|intercom|off]", cmd_mode, 1,
		       1);
#endif
