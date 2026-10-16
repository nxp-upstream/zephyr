/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "erpc_client_manager.h"
#include "erpc_manually_constructed.hpp"

#include "mpipe_vid_format_client.hpp"
#include "mpipe_erpc_vid_objects.h"
#include "mpipe_vid_format_server.hpp"

using namespace erpc;
using namespace erpcShim;

#if defined(CONFIG_MPIPE_ERPC_VID)
/*
 * A generated client object holds nothing but its client manager, so one is
 * built for the time of a call.
 */
int32_t mpipe_erpc_vid_objects_get_buf_caps(erpc_client_t client, uint32_t instance,
						   uint8_t direction,
						   uint8_t *min_buffers, uint16_t *buf_align)
{
	MpipeVidFormat_client format(reinterpret_cast<ClientManager *>(client));

	return format.mpipe_vid_format_get_buf_caps(instance, direction, min_buffers, buf_align);
}

int32_t mpipe_erpc_vid_objects_get_caps(erpc_client_t client, uint32_t instance,
						   uint8_t direction,
					       uint8_t ind, mpipe_rpc_video_format_cap *vfc)
{
	MpipeVidFormat_client format(reinterpret_cast<ClientManager *>(client));

	return format.mpipe_vid_format_get_caps(instance, direction, ind, vfc);
}

int32_t mpipe_erpc_vid_objects_set(erpc_client_t client, uint32_t instance,
					  mpipe_rpc_video_format *fmt)
{
	MpipeVidFormat_client format(reinterpret_cast<ClientManager *>(client));

	return format.mpipe_vid_format_set(instance, fmt);
}

int32_t mpipe_erpc_vid_objects_transform_cap(erpc_client_t client, uint32_t instance,
						   uint8_t direction,
						    uint16_t ind,
						    const mpipe_rpc_video_format_cap *vfc,
						    mpipe_rpc_video_format_cap *other_vfc)
{
	MpipeVidFormat_client format(reinterpret_cast<ClientManager *>(client));

	return format.mpipe_vid_format_transform_cap(instance, direction, ind, vfc, other_vfc);
}
#endif /* CONFIG_MPIPE_ERPC_VID */

#if defined(CONFIG_MPIPE_ERPC_VID_SERVER)
namespace
{

/* Answers the video format interface with the functions of the C code */
class format_handler : public MpipeVidFormat_interface
{
public:
	format_handler(const struct mpipe_erpc_vid_objects_handler *handler, void *ctx)
		: m_handler(handler), m_ctx(ctx)
	{
	}

	virtual int32_t mpipe_vid_format_get_buf_caps(uint32_t instance, uint8_t direction,
						      uint8_t *min_buffers,
						      uint16_t *buf_align) override
	{
		return m_handler->get_buf_caps(m_ctx, instance, direction, min_buffers, buf_align);
	}

	virtual int32_t mpipe_vid_format_get_caps(uint32_t instance, uint8_t direction, uint8_t ind,
						  mpipe_rpc_video_format_cap vfc[1]) override
	{
		return m_handler->get_caps(m_ctx, instance, direction, ind, vfc);
	}

	virtual int32_t mpipe_vid_format_set(uint32_t instance,
					     mpipe_rpc_video_format fmt[1]) override
	{
		return m_handler->set(m_ctx, instance, fmt);
	}

	virtual int32_t
	mpipe_vid_format_transform_cap(uint32_t instance, uint8_t direction, uint16_t ind,
				       const mpipe_rpc_video_format_cap vfc[1],
				       mpipe_rpc_video_format_cap other_vfc[1]) override
	{
		return m_handler->transform_cap(m_ctx, instance, direction, ind, vfc, other_vfc);
	}

private:
	const struct mpipe_erpc_vid_objects_handler *m_handler;
	void *m_ctx;
};

/* One for each eRPC server */
struct service_slot {
	ManuallyConstructed<format_handler> handler;
	ManuallyConstructed<MpipeVidFormat_service> service;
};

struct service_slot services[CONFIG_MPIPE_ERPC_SERVERS_NUM];
atomic_t services_used;

} /* namespace */

void *mpipe_erpc_vid_objects_service_create(
	const struct mpipe_erpc_vid_objects_handler *handler, void *ctx)
{
	atomic_val_t index = atomic_inc(&services_used);

	if (index >= static_cast<atomic_val_t>(ARRAY_SIZE(services))) {
		return NULL;
	}

	services[index].handler.construct(handler, ctx);
	services[index].service.construct(services[index].handler.get());

	return static_cast<Service *>(services[index].service.get());
}
#endif /* CONFIG_MPIPE_ERPC_VID_SERVER */
