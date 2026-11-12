/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The remote audio server has no console: it keeps its log lines and this
 * core reads them with aud_log_read_rpc, then prints each one at its level
 * in a log module of its own. A file of its own because a file registers
 * one log module.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "c_mpipe_aud_transform_client.h"
#include "mpipe_aud_transform_common.h"
#include "mpipe_aud_transform_client_erpc_log.h"

LOG_MODULE_REGISTER(mpipe_aud_remote, CONFIG_MPIPE_LOG_LEVEL);

/* Reads in one drain, so that a remote that keeps logging cannot hold the caller */
#define MPIPE_AUD_REMOTE_LOG_READS_MAX 8

/* The read returns its byte count as 16 bits */
BUILD_ASSERT(sizeof(mpipe_rpc_aud_log_text) <= UINT16_MAX);

/*
 * The text of one read and the line being printed. Every drain runs under
 * the RPC lock, so they need no stack and no lock of their own.
 */
static mpipe_rpc_aud_log_text text;
static char line[sizeof(mpipe_rpc_aud_log_text) + 1U];

static void mpipe_aud_transform_client_erpc_log_print(const uint8_t *start, uint16_t len)
{
	char level = (char)start[0];
	uint16_t n = len;

	/* The level byte, then the text: one of the levels the remote writes */
	if (level == 'I' || level == 'W' || level == 'E') {
		start++;
		n--;
	} else {
		level = 'I';
	}

	memcpy(line, start, n);
	line[n] = '\0';

	switch (level) {
	case 'E':
		LOG_ERR("%s", line);
		break;
	case 'W':
		LOG_WRN("%s", line);
		break;
	default:
		LOG_INF("%s", line);
		break;
	}
}

void mpipe_aud_transform_client_erpc_log_drain(void)
{
	for (int i = 0; i < MPIPE_AUD_REMOTE_LOG_READS_MAX; i++) {
		uint32_t dropped = 0U;
		uint16_t len = 0U;
		uint16_t start = 0U;
		int32_t ret;

		ret = aud_log_read_rpc(text, &len, &dropped);
		if (ret != 0) {
			/* The eRPC error handler has reported the failure */
			return;
		}

		if (dropped != 0U) {
			LOG_WRN("%u lines lost: the log of the remote was full", dropped);
		}

		len = MIN(len, (uint16_t)sizeof(text));
		if (len == 0U) {
			return;
		}

		/* Whole lines, each ended by '\n' */
		for (uint16_t j = 0U; j < len; j++) {
			if (text[j] == (uint8_t)'\n') {
				if (j > start) {
					mpipe_aud_transform_client_erpc_log_print(&text[start],
										  j - start);
				}
				start = j + 1U;
			}
		}

		/* A last line without its '\n' is printed as it is */
		if (start < len) {
			mpipe_aud_transform_client_erpc_log_print(&text[start], len - start);
		}
	}
}
