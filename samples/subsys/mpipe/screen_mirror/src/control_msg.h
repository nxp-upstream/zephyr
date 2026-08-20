/*
 * Copyright (C) 2018 Genymobile
 * Copyright (C) 2018-2025 Romain Vimont
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __SC_CONTROLMSG_H__
#define __SC_CONTROLMSG_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "android/input.h"
#include "android/keycodes.h"
#include "coords.h"

#define SC_CONTROL_MSG_MAX_SIZE (1 << 18) // 256k

enum sc_control_msg_type {
	SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT = 2,
	SC_CONTROL_MSG_TYPE_DUMMY = 15
};

struct sc_control_msg {
	enum sc_control_msg_type type;
	union {
		struct {
			enum android_motionevent_action action;
			enum android_motionevent_buttons action_button;
			enum android_motionevent_buttons buttons;
			uint64_t pointer_id;
			struct sc_position position;
			float pressure;
		} inject_touch_event;
	};
};

// buf size must be at least CONTROL_MSG_MAX_SIZE
// return the number of bytes written
size_t sc_control_msg_serialize(const struct sc_control_msg *msg, unsigned char *buf);

#endif /*__SC_CONTROLMSG_H__*/
