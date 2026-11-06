/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Log of the remote audio server. This core has no console: each line goes
 * into a buffer of this core, and the Cortex-M33 reads the lines back with
 * aud_log_read_rpc (mpipe_aud_transform.erpc) and prints them.
 *
 * Only the thread that runs the eRPC server logs, and it serves one call at
 * a time: the buffer takes no lock.
 */

#ifndef AUD_REMOTE_LOG_H_
#define AUD_REMOTE_LOG_H_

#include <zephyr/toolchain.h>

/* Levels of a line, the first byte of each line on the wire */
#define AUD_LOG_INF 'I'
#define AUD_LOG_WRN 'W'
#define AUD_LOG_ERR 'E'

/*
 * Bytes of one line in the buffer, its level and its '\n' included. Longer
 * text is cut. One line always fits in one aud_log_read_rpc.
 */
#define AUD_LOG_LINE_MAX 200U

/*
 * Format one line at level, one of AUD_LOG_*, into the buffer. fmt takes no
 * trailing '\n'. The line is dropped, and counted, when the buffer is full.
 */
void aud_log(char level, const char *fmt, ...) __printf_like(2, 3);

#endif /* AUD_REMOTE_LOG_H_ */
