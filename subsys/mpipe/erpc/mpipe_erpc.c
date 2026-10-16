/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/erpc/mpipe_erpc.h>

#include "erpc_mbf_setup.h"
#include "erpc_transport_setup.h"

LOG_MODULE_REGISTER(mpipe_erpc, CONFIG_MPIPE_LOG_LEVEL);

static erpc_client_t client;

static const char *mpipe_erpc_status_str(erpc_status_t err)
{
	switch (err) {
	case kErpcStatus_Fail:
		return "generic failure";
	case kErpcStatus_InvalidArgument:
		return "invalid argument";
	case kErpcStatus_Timeout:
		return "timeout";
	case kErpcStatus_InvalidMessageVersion:
		return "unknown message version";
	case kErpcStatus_ExpectedReply:
		return "expected a reply, got another message type";
	case kErpcStatus_CrcCheckFailed:
		return "corrupted message";
	case kErpcStatus_BufferOverrun:
		return "buffer overrun";
	case kErpcStatus_UnknownName:
		return "unknown host name";
	case kErpcStatus_ConnectionFailure:
		return "connection failed";
	case kErpcStatus_ConnectionClosed:
		return "connection closed by peer";
	case kErpcStatus_MemoryError:
		return "memory allocation failed";
	case kErpcStatus_ServerIsDown:
		return "server is down";
	case kErpcStatus_InitFailed:
		return "transport initialization failed";
	case kErpcStatus_ReceiveFailed:
		return "receive failed";
	case kErpcStatus_SendFailed:
		return "send failed";
	default:
		return "unknown error";
	}
}

void mpipe_erpc_error_handler(erpc_status_t err, uint32_t function_id)
{
	if (err == kErpcStatus_Success) {
		return;
	}

	if (function_id != 0U) {
		LOG_ERR("eRPC error %d (%s) in function %u", (int)err, mpipe_erpc_status_str(err),
			function_id);
	} else {
		LOG_ERR("eRPC error %d (%s)", (int)err, mpipe_erpc_status_str(err));
	}
}

erpc_client_t mpipe_erpc_client_init(void)
{
	erpc_transport_t transport;
	erpc_mbf_t mbf;

	if (client != NULL) {
		return client;
	}

	transport = erpc_transport_zephyr_ipc_master_init(CONFIG_ERPC_ENDPOINT_NAME,
							  CONFIG_MPIPE_ERPC_INIT_DELAY_MS);
	if (transport == NULL) {
		LOG_ERR("Failed to initialize the eRPC transport");
		return NULL;
	}

	mbf = erpc_mbf_ipc_service_init(transport);
	if (mbf == NULL) {
		LOG_ERR("Failed to initialize the eRPC message buffer factory");
		return NULL;
	}

	client = erpc_client_init(transport, mbf);
	if (client == NULL) {
		LOG_ERR("Failed to initialize the eRPC client");
		return NULL;
	}

	erpc_client_set_error_handler(client, mpipe_erpc_error_handler);

	return client;
}
