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
 * A short press of the user button cycles mirror, inject, intercom, off; a
 * press held longer wakes the phone's voice assistant instead, as a headset
 * button does, which is how the assistant is called from across the room
 * since its wake word cannot hear the board; a press held for seconds ends
 * the demo: the phone's client exits instead of reconnecting. The green LED is lit when the board's
 * microphone plays on the phone (mirror, intercom), the red one when the
 * phone's microphone is involved (inject, intercom). The shell commands
 * "mode" and "assist" do the same from a terminal, and are all there is on a
 * board without button or LEDs. Each action is shown on the panel for a
 * moment.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/shell/shell.h>

#include "osd.h"
#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(demo_mode);

/* A press held this long calls the assistant instead of changing the mode */
#define LONG_PRESS_MS 700
/* A press held this long ends the demo on the phone */
#define QUIT_PRESS_MS 3000
/* How long a message stays on the panel */
#define OSD_MS        2000

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
	osd_show(modes[mode].name, OSD_MS);
	demo_mode_announce();
}

static void assist(void)
{
	LOG_INF("Calling the assistant");
	osd_show("Assistant", OSD_MS);
	control_send_key(AKEYCODE_VOICE_ASSIST);
}

static void quit_phone(void)
{
	LOG_INF("Ending the demo on the phone");
	osd_show("Bye", OSD_MS);
	control_send_quit();
}

void demo_mode_init(void)
{
	led_init(&led_green);
	led_init(&led_red);
	demo_mode_set(demo_mode);
}

#if DT_HAS_COMPAT_STATUS_OKAY(gpio_keys)
/*
 * How long the button has been held, in stages: a release in stage 0 is a
 * short press, in stage 1 it calls the assistant, and stage 2 is reached while
 * still held and ends the demo at once.
 */
static uint8_t press_stage;

static void press_timer_fn(struct k_timer *timer)
{
	if (press_stage == 0U) {
		press_stage = 1;
		osd_show("Assistant", OSD_MS);
		k_timer_start(timer, K_MSEC(QUIT_PRESS_MS - LONG_PRESS_MS), K_NO_WAIT);
	} else {
		press_stage = 2;
		quit_phone();
	}
}

static K_TIMER_DEFINE(press_timer, press_timer_fn, NULL);

static void button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->code != INPUT_KEY_0) {
		return;
	}

	if (evt->value != 0) {
		press_stage = 0;
		k_timer_start(&press_timer, K_MSEC(LONG_PRESS_MS), K_NO_WAIT);
		return;
	}

	k_timer_stop(&press_timer);
	switch (press_stage) {
	case 0:
		demo_mode_set((demo_mode + 1) % ARRAY_SIZE(modes));
		break;
	case 1:
		assist();
		break;
	default:
		break;
	}
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

static int cmd_assist(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	assist();

	return 0;
}

SHELL_CMD_REGISTER(assist, NULL, "Call the phone's voice assistant", cmd_assist);

static int cmd_phone_quit(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	quit_phone();

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(phone_cmds,
			       SHELL_CMD(quit, NULL, "End the demo on the phone", cmd_phone_quit),
			       SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(phone, &phone_cmds, "The phone's client", NULL);
#endif
