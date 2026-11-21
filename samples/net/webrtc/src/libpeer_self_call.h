/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYRTC_LIBPEER_SELF_CALL_H_
#define ZEPHYRTC_LIBPEER_SELF_CALL_H_

/*
 * Run a call between two endpoints of the image: the first one sends what
 * the media captures, the second one gives what it receives back to the
 * media. Returns 0 when the media went through.
 */
int libpeer_self_call_run(void);

#endif /* ZEPHYRTC_LIBPEER_SELF_CALL_H_ */
