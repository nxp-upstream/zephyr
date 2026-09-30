/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/input/input.h>
#include <zephyr/net/socket.h>
#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(touch_report);

/*
 * Resolve the touch controller node. Boards that declare a
 * "zephyr,touch" chosen node (e.g. native_sim with the SDL pointer input)
 * use it directly; otherwise fall back to the GT911 controller node label
 * provided by the rk055hdmipi4ma0 display shield.
 */
#if DT_HAS_CHOSEN(zephyr_touch)
#define TOUCH_NODE DT_CHOSEN(zephyr_touch)
#else
#define TOUCH_NODE DT_NODELABEL(gt911_rk055hdmipi4ma0)
#endif

K_MSGQ_DEFINE(sc_control_queue, sizeof(struct sc_control_msg), CONFIG_INPUT_QUEUE_MAX_MSGS, 4);

static void check_alive(struct k_timer *dummy);
static K_TIMER_DEFINE(check_alive_timer, check_alive, NULL);
extern struct k_event application_event;

struct point_event {
	uint16_t x;
	uint16_t y;
	uint16_t state;
};

struct control_data {
	int control_socket;
	struct sockaddr_in *remote_addr;
	uint8_t actual_point_id;
	struct point_event prev_event[MAX_TOUCH_POINTS];
	struct point_event actual_event[MAX_TOUCH_POINTS];
	struct k_work send_work;
	bool is_connected;
};

static struct control_data data;

static uint8_t msg_buf[SC_CONTROL_MSG_MAX_SIZE];

static bool has_touch_moved(const struct point_event *prev_point,
			    const struct point_event *actual_point)
{
	return actual_point->state == 1 && prev_point->state == 1 &&
	       (actual_point->x - prev_point->x) * (actual_point->x - prev_point->x) +
			       (actual_point->y - prev_point->y) *
				       (actual_point->y - prev_point->y) >=
		       SENSITIVITY_RADIUS * SENSITIVITY_RADIUS;
}

static void convert_to_android_event(struct sc_control_msg *android_point,
				     const struct point_event *actual_event,
				     const struct point_event *prev_event)
{
	android_point->type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
	android_point->inject_touch_event.position.point.x = actual_event->x;
	android_point->inject_touch_event.position.point.y = actual_event->y;
	android_point->inject_touch_event.position.screen_size.height = SCR_HEIGHT;
	android_point->inject_touch_event.position.screen_size.width = SCR_WIDTH;

	if (actual_event->state == 1 && prev_event->state == 0) {
		android_point->inject_touch_event.action = AMOTION_EVENT_ACTION_DOWN;
		android_point->inject_touch_event.pressure = 1.0;
	} else if (actual_event->state == 1 && prev_event->state == 1) {
		android_point->inject_touch_event.action = AMOTION_EVENT_ACTION_MOVE;
		android_point->inject_touch_event.pressure = 1.0;
	} else if (actual_event->state == 0 && prev_event->state == 1) {
		android_point->inject_touch_event.action = AMOTION_EVENT_ACTION_UP;
		android_point->inject_touch_event.pressure = 0.0;
	}

	android_point->inject_touch_event.buttons = 0;
	android_point->inject_touch_event.action_button = 0;
}

static void check_alive(struct k_timer *timer)
{
	struct control_data *p_data = k_timer_user_data_get(timer);
	struct sc_control_msg dummy_msg = {0};

	dummy_msg.type = SC_CONTROL_MSG_TYPE_DUMMY;
	LOG_DBG("Check alive");
	if (p_data->is_connected && k_msgq_num_used_get(&sc_control_queue) == 0) {
		k_msgq_put(&sc_control_queue, &dummy_msg, K_NO_WAIT);
		k_work_submit(&p_data->send_work);
	}
}

static void parse_touch_event_cb(struct input_event *evt, void *user_data)
{
	struct control_data *p_data = user_data;
	struct sc_control_msg actual_msg;

	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		p_data->actual_point_id = evt->value < MAX_TOUCH_POINTS ? evt->value : 0;
		if (evt->value >= MAX_TOUCH_POINTS) {
			LOG_WRN("evt_value is greater than MAX_TOUCH_POINTS");
		}
		break;
	case INPUT_ABS_X:
		p_data->actual_event[p_data->actual_point_id].x = evt->value;
		break;
	case INPUT_ABS_Y:
		p_data->actual_event[p_data->actual_point_id].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		p_data->actual_event[p_data->actual_point_id].state = evt->value;
		if (p_data->is_connected &&
		    (has_touch_moved(&p_data->prev_event[p_data->actual_point_id],
				     &p_data->actual_event[p_data->actual_point_id]) ||
		     p_data->prev_event[p_data->actual_point_id].state !=
			     p_data->actual_event[p_data->actual_point_id].state)) {
			actual_msg.inject_touch_event.pointer_id = p_data->actual_point_id;
			convert_to_android_event(&actual_msg,
						 &p_data->actual_event[p_data->actual_point_id],
						 &p_data->prev_event[p_data->actual_point_id]);

			k_msgq_put(&sc_control_queue, &actual_msg, K_NO_WAIT);
			k_work_submit(&p_data->send_work);
		}
		p_data->prev_event[p_data->actual_point_id] =
			p_data->actual_event[p_data->actual_point_id];
		break;
	}
}

static void send_work_handler(struct k_work *kwork)
{
	struct sc_control_msg android_evt_data;
	struct control_data *evt_send = CONTAINER_OF(kwork, struct control_data, send_work);

	while (k_msgq_get(&sc_control_queue, &android_evt_data, K_NO_WAIT) == 0) {
		ssize_t msg_len = sc_control_msg_serialize(&android_evt_data, msg_buf);
		ssize_t send_size;

		if (IS_ENABLED(CONFIG_CONTROL_EVENT_TCP)) {
			send_size = zsock_send(evt_send->control_socket, msg_buf, msg_len, 0);
		} else {
			send_size = zsock_sendto(evt_send->control_socket, msg_buf, msg_len, 0,
						 (const struct sockaddr *)evt_send->remote_addr,
						 sizeof(struct sockaddr));
		}
		if (send_size <= 0) {
			LOG_ERR("Touch socket error");
			k_event_post(&application_event, EVENT_TOUCH_ERROR);
		}
		LOG_DBG("Report touch point %d x: %d y: %d btn: %d ", data.actual_point_id,
			android_evt_data.inject_touch_event.position.point.x,
			android_evt_data.inject_touch_event.position.point.x,
			android_evt_data.inject_touch_event.action);
	}
}

void connect_control_socket(int socket_fd, struct sockaddr_in *client_addr)
{
	int client_addr_len = 0;

	LOG_INF("Wait for control socket ...");

	if (IS_ENABLED(CONFIG_CONTROL_EVENT_TCP)) {
		/* Open another TCP socket for the control event*/
		data.control_socket =
			zsock_accept(socket_fd, (struct sockaddr *)client_addr, &client_addr_len);
	} else {
		data.control_socket = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	}

	if (data.control_socket < 0) {
		LOG_ERR("Failed to accept control socket: %d", errno);
		return;
	}

	data.remote_addr = client_addr;

	data.is_connected = true;

	LOG_INF("%s control socket is connected",
		IS_ENABLED(CONFIG_CONTROL_EVENT_TCP) ? "TCP" : "UDP");

	k_timer_start(&check_alive_timer, K_MSEC(20000), K_MSEC(20000));
}

int disconnect_control_socket(void)
{
	int ret = 0;

	if (data.is_connected) {
		if (IS_ENABLED(CONFIG_CONTROL_EVENT_TCP)) {
			zsock_shutdown(data.control_socket, SHUT_RDWR);
		}

		ret = zsock_close(data.control_socket);
		if (ret < 0) {
			LOG_ERR("Cannot close the connection %d", errno);
			ret = -EIO;
		}

		data.is_connected = false;
		k_timer_stop(&check_alive_timer);

		LOG_INF("Control socket disconnected");
	}

	return ret;
}

bool control_socket_is_connected(void)
{
	return data.is_connected;
}

void control_init(void)
{
	k_work_init(&data.send_work, send_work_handler);
	k_timer_user_data_set(&check_alive_timer, &data);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TOUCH_NODE), parse_touch_event_cb, &data);
