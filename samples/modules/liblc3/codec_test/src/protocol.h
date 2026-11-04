/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LC3_TEST_PROTOCOL_H_
#define LC3_TEST_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_HEADER_SIZE 5U
#define PROTOCOL_MAX_PAYLOAD 49152U
#define PROTOCOL_ERROR 0xffU

enum protocol_command {
	CMD_ENC_SETUP = 0x01,
	CMD_ENC_FRAME = 0x02,
	CMD_ENC_FRAMES = 0x03,
	CMD_DEC_SETUP = 0x10,
	CMD_DEC_FRAME = 0x11,
	CMD_DEC_FRAMES = 0x12,
	CMD_DEC_PLC = 0x13,
	CMD_INFO = 0x20,
	CMD_RESET = 0x21,
	CMD_QUERY = 0x22,
};

typedef int (*protocol_send_fn)(uint8_t type, const void *payload, size_t length);

void protocol_reset(void);
int protocol_feed(const uint8_t *data, size_t length, protocol_send_fn send);

#endif
