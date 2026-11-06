/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Log buffer of the remote audio server, a ring of whole lines, and its
 * reader, aud_log_read_rpc. Each line is its level byte, its text and '\n'.
 * A line that does not fit in the free space is dropped whole, so the ring
 * always holds whole lines.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(CONFIG_PRINTK)
#include <zephyr/sys/printk.h>
#endif
#include <zephyr/sys/util.h>

#include "aud_remote_log.h"
#include "c_mpipe_aud_transform_server.h"
#include "mpipe_aud_transform_common.h"

#define AUD_LOG_BUF_SIZE CONFIG_AUD_REMOTE_LOG_BUF_SIZE

/* The positions below run free and wrap at 2^32: the ring size must divide that */
BUILD_ASSERT(IS_POWER_OF_TWO(AUD_LOG_BUF_SIZE),
	     "CONFIG_AUD_REMOTE_LOG_BUF_SIZE must be a power of two");
BUILD_ASSERT(AUD_LOG_LINE_MAX <= AUD_LOG_BUF_SIZE);
/* Every read can carry the oldest line, whatever its length */
BUILD_ASSERT(AUD_LOG_LINE_MAX <= sizeof(mpipe_rpc_aud_log_text));
/* The level, one character of text, '\n' */
BUILD_ASSERT(AUD_LOG_LINE_MAX >= 3U);

/*
 * The lines not read yet are aud_log_ring[tail .. head - 1], modulo the
 * size. Named for a debugger: when eRPC never comes up, read it from memory.
 */
char aud_log_ring[AUD_LOG_BUF_SIZE];
static uint32_t aud_log_head;
static uint32_t aud_log_tail;

/* Lines dropped since the last read */
static uint32_t aud_log_dropped;

void aud_log(char level, const char *fmt, ...)
{
	char line[AUD_LOG_LINE_MAX];
	uint32_t first;
	uint32_t len;
	uint32_t pos;
	va_list ap;
	int n;

	line[0] = level;

	/* Room for the level and the '\n': the text gets the rest */
	va_start(ap, fmt);
	n = vsnprintf(&line[1], sizeof(line) - 1U, fmt, ap);
	va_end(ap);

	if (n < 0) {
		n = 0;
	}

	len = 1U + MIN((uint32_t)n, (uint32_t)sizeof(line) - 2U);

	/* One line is one line: a '\n' inside the text would split it */
	for (uint32_t i = 1U; i < len; i++) {
		if (line[i] == '\n') {
			line[i] = ' ';
		}
	}

#if defined(CONFIG_PRINTK)
	/* Where this core has a console, the line shows there too. line[len] is the NUL. */
	printk("%s\n", &line[1]);
#endif

	line[len] = '\n';
	len++;

	if (len > AUD_LOG_BUF_SIZE - (aud_log_head - aud_log_tail)) {
		aud_log_dropped++;
		return;
	}

	/* The line may wrap: first is what fits before the end of the ring */
	pos = aud_log_head % AUD_LOG_BUF_SIZE;
	first = MIN(len, AUD_LOG_BUF_SIZE - pos);
	memcpy(&aud_log_ring[pos], line, first);
	memcpy(aud_log_ring, &line[first], len - first);

	aud_log_head += len;
}

int32_t aud_log_read_rpc(mpipe_rpc_aud_log_text text, uint16_t *len, uint32_t *dropped)
{
	uint32_t avail = MIN(aud_log_head - aud_log_tail, (uint32_t)sizeof(mpipe_rpc_aud_log_text));
	uint32_t whole = 0U;

	/* Copy what fits, then hand over only the lines it ends whole */
	for (uint32_t i = 0U; i < avail; i++) {
		text[i] = (uint8_t)aud_log_ring[(aud_log_tail + i) % AUD_LOG_BUF_SIZE];
		if (text[i] == (uint8_t)'\n') {
			whole = i + 1U;
		}
	}

	aud_log_tail += whole;
	*len = (uint16_t)whole;
	*dropped = aud_log_dropped;
	aud_log_dropped = 0U;

	return 0;
}
