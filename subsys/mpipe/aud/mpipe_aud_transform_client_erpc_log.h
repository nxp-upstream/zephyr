/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Reader of the log of the remote audio server, private to the eRPC audio
 * transform client.
 */

#ifndef ZEPHYR_SUBSYS_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_ERPC_LOG_H_
#define ZEPHYR_SUBSYS_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_ERPC_LOG_H_

#if defined(CONFIG_MPIPE_AUD_REMOTE_LOG)
/*
 * Read the lines the remote has logged and print them, in log module
 * mpipe_aud_remote. The caller holds the RPC lock and the log interface is
 * bound.
 */
void mpipe_aud_transform_client_erpc_log_drain(void);
#else
static inline void mpipe_aud_transform_client_erpc_log_drain(void)
{
}
#endif

#endif /* ZEPHYR_SUBSYS_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_ERPC_LOG_H_ */
