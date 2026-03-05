/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <src/core/mp_transform_client.h>
#include <src/core/rpc/erpc/erpc_error_handler.h>

#include "erpc_client_setup.h"
#include "erpc/c_mp_zvid_transform_client.h"
#include "erpc/mp_zvid_transform_common.h"

#ifdef CONFIG_MP_ERPC_TRANSPORT_RPMSGLITE
#include "rpmsg_lite.h"
/*
 * TODO: rpmsg-lite should define link ID in devicetree and the element should get it from the
 * application via set_property
 */
#define ERPC_TRANSPORT_RPMSG_LITE_LINK_ID (0)
#endif

#include "mp_zvid_transform_client.h"
#include "mp_zvid_transform_client_erpc.h"

LOG_MODULE_REGISTER(mp_zvid_transform_client_erpc, CONFIG_LIBMP_LOG_LEVEL);

static void mp_zvid_transform_client_init_erpc(void)
{
	/* TODO: 100, 101 endpoint got from devicetree */
	erpc_transport_t transport = erpc_transport_rpmsg_lite_rtos_master_init(
		100, 101, ERPC_TRANSPORT_RPMSG_LITE_LINK_ID);
	erpc_mbf_t message_buffer_factory = erpc_mbf_rpmsg_init(transport);
	erpc_client_t client = erpc_client_init(transport, message_buffer_factory);

	initMPService_client(client);

	/* Set default error handler */
	erpc_client_set_error_handler(client, erpc_error_handler);
}

void mp_zvid_transform_client_erpc_init(struct mp_element *self)
{
	struct mp_transform_client *transform_client = MP_TRANSFORM_CLIENT(self);
	struct mp_zvid_transform_client *vtc = MP_ZVID_TRANSFORM_CLIENT(self);

	transform_client->init_rpc = mp_zvid_transform_client_init_erpc;
	transform_client->chainfn_rpc = chainfn_rpc;

	vtc->get_buf_caps_rpc = get_buf_caps_rpc;
	vtc->get_format_caps_rpc = get_format_caps_rpc;
	vtc->set_format_rpc = set_format_rpc;
	vtc->transform_cap_rpc = transform_cap_rpc;

	mp_zvid_transform_client_init(self);
}
