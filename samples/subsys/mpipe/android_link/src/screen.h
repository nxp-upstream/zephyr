/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRMIRROR_SCREEN
#define SCRMIRROR_SCREEN

#include <zephyr/net/socket.h>

#include "control_msg.h"

#define SCR_WIDTH  DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCR_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define SCR_BUF_SZ (SCR_WIDTH * SCR_HEIGHT * 2)
#define SCR_FORMAT PIXEL_FORMAT_RGB_565

#define EVENT_SOCKET_THREAD_STOP (1 << 1)
#define EVENT_TOUCH_ERROR        (1 << 2)
#define SENSITIVITY_RADIUS       1

/*
 * Maximum number of simultaneous touch points. The GT911 controller on the
 * rk055hdmipi4ma0 shield reports this through its own Kconfig; boards without
 * that driver (e.g. native_sim with the single-pointer SDL input) fall back
 * to a single touch point.
 */
#ifdef CONFIG_INPUT_GT911_MAX_TOUCH_POINTS
#define MAX_TOUCH_POINTS CONFIG_INPUT_GT911_MAX_TOUCH_POINTS
#else
#define MAX_TOUCH_POINTS 1
#endif

void control_init(void);
/*
 * Start sending touch events to the phone at @p client_addr: over @p control_fd,
 * the connection the phone opened for control, or over a UDP socket of its own.
 */
void connect_control_socket(int control_fd, struct sockaddr_in *client_addr);
int disconnect_control_socket(void);
bool control_socket_is_connected(void);
/* Tell the phone what to do with the microphone stream; nothing when not connected */
void control_send_mic_mode(enum sc_mic_mode mode);

/*
 * The microphone mode the phone is asked for. Changing it sends the phone the
 * new mode when the control connection is up; connecting sends the current one.
 */
enum sc_mic_mode mic_mode_get(void);
void mic_mode_set(enum sc_mic_mode mode);
const char *mic_mode_name(enum sc_mic_mode mode);

int display_setup(const struct device *const display_dev);

#endif
