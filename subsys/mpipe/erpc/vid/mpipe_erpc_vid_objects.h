/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The eRPC objects of the video format interface, which are C++ objects,
 * behind functions the C code calls.
 */

#ifndef ZEPHYR_SUBSYS_MPIPE_ERPC_VID_MPIPE_ERPC_VID_OBJECTS_H_
#define ZEPHYR_SUBSYS_MPIPE_ERPC_VID_MPIPE_ERPC_VID_OBJECTS_H_

#include <stdint.h>

#include "erpc_client_setup.h"
#include "mpipe_vid_format_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The calls of the video format interface, made through the client of a channel */
int32_t mpipe_erpc_vid_objects_get_buf_caps(erpc_client_t client, uint32_t instance,
					    uint8_t direction, uint8_t *min_buffers,
					    uint16_t *buf_align);
int32_t mpipe_erpc_vid_objects_get_caps(erpc_client_t client, uint32_t instance, uint8_t direction,
					uint8_t ind, mpipe_rpc_video_format_cap *vfc);
int32_t mpipe_erpc_vid_objects_set(erpc_client_t client, uint32_t instance,
				   mpipe_rpc_video_format *fmt);
int32_t mpipe_erpc_vid_objects_transform_cap(erpc_client_t client, uint32_t instance,
					     uint8_t direction, uint16_t ind,
					     const mpipe_rpc_video_format_cap *vfc,
					     mpipe_rpc_video_format_cap *other_vfc);

/* Functions answering the calls of the video format interface on a server */
struct mpipe_erpc_vid_objects_handler {
	int32_t (*get_buf_caps)(void *ctx, uint32_t instance, uint8_t direction,
				uint8_t *min_buffers, uint16_t *buf_align);
	int32_t (*get_caps)(void *ctx, uint32_t instance, uint8_t direction, uint8_t ind,
			    mpipe_rpc_video_format_cap *vfc);
	int32_t (*set)(void *ctx, uint32_t instance, mpipe_rpc_video_format *fmt);
	int32_t (*transform_cap)(void *ctx, uint32_t instance, uint8_t direction, uint16_t ind,
				 const mpipe_rpc_video_format_cap *vfc,
				 mpipe_rpc_video_format_cap *other_vfc);
};

/*
 * Create the service of the video format interface for one server, answered by
 * the handler. NULL when every service is taken.
 */
void *mpipe_erpc_vid_objects_service_create(const struct mpipe_erpc_vid_objects_handler *handler,
					    void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MPIPE_ERPC_VID_MPIPE_ERPC_VID_OBJECTS_H_ */
