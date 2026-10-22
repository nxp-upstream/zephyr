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
	/* Moves lost to a full queue since the last report */
	uint32_t moves_dropped;
};

static struct control_data data;

/*
 * The messages leave from a work queue of their own: on the system workqueue
 * a send that blocks on Wi-Fi held up the touch controller's poll, which runs
 * there too. Everything queued when the work runs goes in one send.
 */
#define CONTROL_SEND_BUF_SIZE     1024
#define CONTROL_SEND_STACK_SIZE   2048
#define CONTROL_SEND_THREAD_PRIO  3

static uint8_t msg_buf[CONTROL_SEND_BUF_SIZE];
static struct k_work_q send_q;
static K_KERNEL_STACK_DEFINE(send_q_stack, CONTROL_SEND_STACK_SIZE);

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
	struct sc_control_msg keep_alive = {0};

	keep_alive.type = SC_CONTROL_MSG_TYPE_KEEP_ALIVE;
	LOG_DBG("Check alive");
	if (p_data->is_connected && k_msgq_num_used_get(&sc_control_queue) == 0) {
		k_msgq_put(&sc_control_queue, &keep_alive, K_NO_WAIT);
		k_work_submit_to_queue(&send_q, &p_data->send_work);
	}
}

static void control_send(const struct sc_control_msg *msg)
{
	if (data.is_connected) {
		k_msgq_put(&sc_control_queue, msg, K_NO_WAIT);
		k_work_submit_to_queue(&send_q, &data.send_work);
	}
}

void control_send_mic_mode(enum sc_mic_mode mode)
{
	struct sc_control_msg msg = {
		.type = SC_CONTROL_MSG_TYPE_SET_MIC_MODE,
		.set_mic_mode.mode = mode,
	};

	control_send(&msg);
}

void control_send_key(enum android_keycode keycode)
{
	struct sc_control_msg msg = {
		.type = SC_CONTROL_MSG_TYPE_INJECT_KEYCODE,
		.inject_keycode.keycode = keycode,
		.inject_keycode.repeat = 0,
		.inject_keycode.metastate = AMETA_NONE,
	};

	msg.inject_keycode.action = AKEY_EVENT_ACTION_DOWN;
	control_send(&msg);
	msg.inject_keycode.action = AKEY_EVENT_ACTION_UP;
	control_send(&msg);
}

void control_send_quit(void)
{
	struct sc_control_msg msg = {.type = SC_CONTROL_MSG_TYPE_QUIT};

	control_send(&msg);
}

void control_send_audio_source(enum sc_audio_source source)
{
	struct sc_control_msg msg = {
		.type = SC_CONTROL_MSG_TYPE_SET_AUDIO_SOURCE,
		.set_audio_source.source = source,
	};

	control_send(&msg);
}

/*
 * A move lost to a full queue only costs smoothness; a down or an up lost
 * leaves the phone with a finger it never sees released. When the queue is
 * full, a down or an up takes the place of the oldest message queued.
 */
static void control_queue_touch(struct control_data *p_data, const struct sc_control_msg *msg)
{
	struct sc_control_msg oldest;

	if (k_msgq_put(&sc_control_queue, msg, K_NO_WAIT) == 0) {
		return;
	}

	if (msg->inject_touch_event.action == AMOTION_EVENT_ACTION_MOVE) {
		p_data->moves_dropped++;
		return;
	}

	(void)k_msgq_get(&sc_control_queue, &oldest, K_NO_WAIT);
	if (k_msgq_put(&sc_control_queue, msg, K_NO_WAIT) != 0) {
		LOG_ERR("Touch %s lost, the queue is full",
			msg->inject_touch_event.action == AMOTION_EVENT_ACTION_UP ? "up" : "down");
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

			control_queue_touch(p_data, &actual_msg);
			k_work_submit_to_queue(&send_q, &p_data->send_work);
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

	while (k_msgq_num_used_get(&sc_control_queue) > 0) {
		size_t len = 0;
		ssize_t send_size;

		/* Serialize what is queued, as long as a longest message still fits */
		while (len + SC_CONTROL_MSG_MAX_SIZE <= sizeof(msg_buf) &&
		       k_msgq_get(&sc_control_queue, &android_evt_data, K_NO_WAIT) == 0) {
			len += sc_control_msg_serialize(&android_evt_data, &msg_buf[len]);
			LOG_DBG("Report touch point %u x: %d y: %d btn: %d",
				(unsigned int)android_evt_data.inject_touch_event.pointer_id,
				android_evt_data.inject_touch_event.position.point.x,
				android_evt_data.inject_touch_event.position.point.y,
				android_evt_data.inject_touch_event.action);
		}

		if (len == 0) {
			break;
		}

		if (evt_send->moves_dropped != 0U) {
			LOG_WRN("%u touch moves dropped, the queue was full",
				evt_send->moves_dropped);
			evt_send->moves_dropped = 0;
		}

		if (IS_ENABLED(CONFIG_CONTROL_EVENT_TCP)) {
			send_size = zsock_send(evt_send->control_socket, msg_buf, len, 0);
		} else {
			send_size = zsock_sendto(evt_send->control_socket, msg_buf, len, 0,
						 (const struct sockaddr *)evt_send->remote_addr,
						 sizeof(struct sockaddr));
		}
		if (send_size <= 0) {
			LOG_ERR("Touch socket error");
			k_event_post(&application_event, EVENT_TOUCH_ERROR);
			return;
		}
	}
}

void connect_control_socket(int control_fd, struct sockaddr_in *client_addr)
{
	if (IS_ENABLED(CONFIG_CONTROL_EVENT_TCP)) {
		/* The connection the phone opened for control, accepted by the caller */
		data.control_socket = control_fd;

		/*
		 * Every message is a few bytes and must leave now: with Nagle a
		 * segment waits for the acknowledgment of the previous one, and the
		 * phone delays its acknowledgments, so moves left in bursts.
		 */
		if (zsock_setsockopt(control_fd, IPPROTO_TCP, TCP_NODELAY, &(int){1},
				     sizeof(int)) < 0) {
			LOG_WRN("Cannot disable Nagle on the control socket (%d)", errno);
		}
	} else {
		data.control_socket = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	}

	if (data.control_socket < 0) {
		LOG_ERR("No control socket: %d", errno);
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
	k_work_queue_init(&send_q);
	k_work_queue_start(&send_q, send_q_stack, K_KERNEL_STACK_SIZEOF(send_q_stack),
			   CONTROL_SEND_THREAD_PRIO, NULL);
	k_thread_name_set(k_work_queue_thread_get(&send_q), "control_send");
	k_work_init(&data.send_work, send_work_handler);
	k_timer_user_data_set(&check_alive_timer, &data);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TOUCH_NODE), parse_touch_event_cb, &data);
