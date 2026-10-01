/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The microphone stream always flows while the phone is connected; this is
 * what the phone is asked to do with it. The mode outlives a connection, so a
 * phone that reconnects is told the mode in force.
 */

#include <zephyr/kernel.h>

#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mic_mode);

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

void mic_mode_set(enum sc_mic_mode mode)
{
	mic_mode = mode;
	LOG_INF("Microphone mode: %s", mic_mode_name(mode));
	control_send_mic_mode(mode);
}
