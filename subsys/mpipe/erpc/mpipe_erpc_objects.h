/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The eRPC objects of the channels and of the servers, which are C++ objects,
 * behind functions the C code of the backend calls.
 */

#ifndef ZEPHYR_SUBSYS_MPIPE_ERPC_MPIPE_ERPC_OBJECTS_H_
#define ZEPHYR_SUBSYS_MPIPE_ERPC_MPIPE_ERPC_OBJECTS_H_

#include <stdint.h>

#include "erpc_client_setup.h"
#include "erpc_common.h"
#include "erpc_server_setup.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create an eRPC client on an endpoint of its own. The call returns once the
 * server on the other core has bound the endpoint. NULL when every client is
 * taken or when the transport could not be initialized.
 */
erpc_client_t mpipe_erpc_objects_client_create(const char *ept_name, uint32_t init_delay_ms,
					       client_error_handler_t error_handler);

/* The calls of the stream interface, made through a client */
int32_t mpipe_erpc_objects_stream_open(erpc_client_t client, uint32_t function, uint32_t *instance);
int32_t mpipe_erpc_objects_stream_process(erpc_client_t client, uint32_t instance, uint32_t in_buf,
					  uint32_t in_sz, uint32_t out_buf, uint32_t out_cap,
					  uint32_t *out_sz);
int32_t mpipe_erpc_objects_stream_set_state(erpc_client_t client, uint32_t instance,
					    uint8_t transition);
int32_t mpipe_erpc_objects_stream_eos(erpc_client_t client, uint32_t instance);

/* Functions answering the calls of the stream interface on a server */
struct mpipe_erpc_objects_stream_handler {
	int32_t (*open)(void *ctx, uint32_t function, uint32_t *instance);
	int32_t (*process)(void *ctx, uint32_t instance, uint32_t in_buf, uint32_t in_sz,
			   uint32_t out_buf, uint32_t out_cap, uint32_t *out_sz);
	int32_t (*set_state)(void *ctx, uint32_t instance, uint8_t transition);
	int32_t (*eos)(void *ctx, uint32_t instance);
};

/*
 * Create an eRPC server on an endpoint of its own, with the stream interface
 * answered by the handler. The call returns once the client on the other core
 * has bound the endpoint. NULL when every server is taken or when the
 * transport could not be initialized.
 */
erpc_server_t mpipe_erpc_objects_server_create(
	const char *ept_name, const struct mpipe_erpc_objects_stream_handler *handler, void *ctx);

/* Add the service of another interface to a server */
void mpipe_erpc_objects_server_add_service(erpc_server_t server, void *service);

/* Answer the calls: returns only on an error of the transport */
erpc_status_t mpipe_erpc_objects_server_run(erpc_server_t server);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MPIPE_ERPC_MPIPE_ERPC_OBJECTS_H_ */
