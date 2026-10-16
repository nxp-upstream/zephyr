/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The eRPC objects of the channels and of the servers. They are static: the
 * setup functions of eRPC hold one object of each kind, so they are not used
 * here.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

extern "C" {
#include <zephyr/ipc/ipc_service.h>
}

#include "erpc_basic_codec.hpp"
#include "erpc_client_manager.h"
#include "erpc_crc16.hpp"
#include "erpc_ipc_service_zephyr_transport.hpp"
#include "erpc_manually_constructed.hpp"
#include "erpc_message_buffer.hpp"
#include "erpc_simple_server.hpp"

#include "mpipe_erpc_objects.h"
#include "mpipe_stream_client.hpp"
#include "mpipe_stream_server.hpp"

using namespace erpc;
using namespace erpcShim;

/* Time to wait for a transmit buffer of the IPC service */
#define TX_BUFFER_TIMEOUT K_MSEC(10)

namespace
{

/* The messages are the buffers of the IPC service: nothing is copied */
class ipc_message_buffer_factory : public MessageBufferFactory
{
public:
	explicit ipc_message_buffer_factory(struct ipc_ept *ept) : m_ept(ept) {}

	virtual MessageBuffer create(void) override
	{
		void *buf = NULL;
		uint32_t size = 0;

		(void)ipc_service_get_tx_buffer(m_ept, &buf, &size, TX_BUFFER_TIMEOUT);

		return MessageBuffer(reinterpret_cast<uint8_t *>(buf), static_cast<uint16_t>(size));
	}

	virtual void dispose(MessageBuffer *buf) override
	{
		void *data = reinterpret_cast<void *>(buf->get());

		if (data != NULL) {
			(void)ipc_service_release_rx_buffer(m_ept, data);
		}
	}

	virtual erpc_status_t prepareServerBufferForSend(MessageBuffer &message,
							 uint8_t reserveHeaderSize = 0) override
	{
		dispose(&message);
		message = create();
		if (message.get() == NULL) {
			return kErpcStatus_MemoryError;
		}

		message.setUsed(reserveHeaderSize);

		return kErpcStatus_Success;
	}

	/* The receive buffers come from the IPC service */
	virtual bool createServerBuffer(void) override { return false; }

private:
	struct ipc_ept *m_ept;
};

/* An endpoint and what eRPC needs to exchange messages on it */
struct ipc_link {
	ManuallyConstructed<ZephyrIPCTransport> transport;
	ManuallyConstructed<Crc16> crc16;
	ManuallyConstructed<ipc_message_buffer_factory> factory;
};

/* Protects the claim of the objects below and the opening of the IPC instance */
K_MUTEX_DEFINE(objects_lock);

/* One factory serves every client and server: the codecs are those of eRPC */
ManuallyConstructed<BasicCodecFactory> codec_factory;

CodecFactory *get_codec_factory(void)
{
	(void)k_mutex_lock(&objects_lock, K_FOREVER);
	if (!codec_factory.isUsed()) {
		codec_factory.construct();
	}
	(void)k_mutex_unlock(&objects_lock);

	return codec_factory.get();
}

bool ipc_link_open(struct ipc_link *link, const char *ept_name, uint32_t init_delay_ms)
{
	const struct device *ipc = DEVICE_DT_GET(DT_ALIAS(erpc_ipc));

	if (!device_is_ready(ipc)) {
		return false;
	}

	/*
	 * The endpoints are registered from several threads and share the
	 * instance: have it open before any registers, as an instance being
	 * opened by another thread refuses the registration.
	 */
	(void)k_mutex_lock(&objects_lock, K_FOREVER);
	(void)ipc_service_open_instance(ipc);
	(void)k_mutex_unlock(&objects_lock);

	if (init_delay_ms > 0U) {
		k_sleep(K_MSEC(init_delay_ms));
	}

	link->transport.construct();
	link->crc16.construct();
	link->transport->setCrc16(link->crc16.get());

	/* Returns once the other core has bound the endpoint */
	if (link->transport->init(ipc, ept_name) != kErpcStatus_Success) {
		return false;
	}

	link->factory.construct(link->transport->getEndpoint());

	return true;
}

#if defined(CONFIG_MPIPE_ERPC)
struct client_slot {
	struct ipc_link link;
	ManuallyConstructed<ClientManager> manager;
};

struct client_slot clients[CONFIG_MPIPE_ERPC_ENDPOINTS_NUM];
uint8_t clients_used;
#endif /* CONFIG_MPIPE_ERPC */

#if defined(CONFIG_MPIPE_ERPC_SERVER)
/* Answers the stream interface with the functions of the C code */
class stream_handler : public MpipeStream_interface
{
public:
	stream_handler(const struct mpipe_erpc_objects_stream_handler *handler, void *ctx)
		: m_handler(handler), m_ctx(ctx)
	{
	}

	virtual int32_t mpipe_stream_open(uint32_t function, uint32_t *instance) override
	{
		return m_handler->open(m_ctx, function, instance);
	}

	virtual int32_t mpipe_stream_process(uint32_t instance, uint32_t in_buf, uint32_t in_sz,
					     uint32_t out_buf, uint32_t out_cap,
					     uint32_t *out_sz) override
	{
		return m_handler->process(m_ctx, instance, in_buf, in_sz, out_buf, out_cap, out_sz);
	}

	virtual int32_t mpipe_stream_set_state(uint32_t instance, uint8_t transition) override
	{
		return m_handler->set_state(m_ctx, instance, transition);
	}

	virtual int32_t mpipe_stream_eos(uint32_t instance) override
	{
		return m_handler->eos(m_ctx, instance);
	}

private:
	const struct mpipe_erpc_objects_stream_handler *m_handler;
	void *m_ctx;
};

struct server_slot {
	struct ipc_link link;
	ManuallyConstructed<SimpleServer> server;
	ManuallyConstructed<stream_handler> handler;
	ManuallyConstructed<MpipeStream_service> service;
};

struct server_slot servers[CONFIG_MPIPE_ERPC_SERVERS_NUM];
uint8_t servers_used;
#endif /* CONFIG_MPIPE_ERPC_SERVER */

} /* namespace */

#if defined(CONFIG_MPIPE_ERPC)
erpc_client_t mpipe_erpc_objects_client_create(const char *ept_name, uint32_t init_delay_ms,
					       client_error_handler_t error_handler)
{
	struct client_slot *slot = NULL;

	(void)k_mutex_lock(&objects_lock, K_FOREVER);
	if (clients_used < ARRAY_SIZE(clients)) {
		slot = &clients[clients_used];
		clients_used++;
	}
	(void)k_mutex_unlock(&objects_lock);

	if (slot == NULL || !ipc_link_open(&slot->link, ept_name, init_delay_ms)) {
		return NULL;
	}

	slot->manager.construct();
	slot->manager->setTransport(slot->link.transport.get());
	slot->manager->setCodecFactory(get_codec_factory());
	slot->manager->setMessageBufferFactory(slot->link.factory.get());
	slot->manager->setErrorHandler(error_handler);

	return reinterpret_cast<erpc_client_t>(slot->manager.get());
}

/*
 * A generated client object holds nothing but its client manager, so one is
 * built for the time of a call.
 */
int32_t mpipe_erpc_objects_stream_open(erpc_client_t client, uint32_t function,
				       uint32_t *instance)
{
	MpipeStream_client stream(reinterpret_cast<ClientManager *>(client));

	return stream.mpipe_stream_open(function, instance);
}

int32_t mpipe_erpc_objects_stream_process(erpc_client_t client, uint32_t instance,
					  uint32_t in_buf, uint32_t in_sz, uint32_t out_buf,
					  uint32_t out_cap, uint32_t *out_sz)
{
	MpipeStream_client stream(reinterpret_cast<ClientManager *>(client));

	return stream.mpipe_stream_process(instance, in_buf, in_sz, out_buf, out_cap, out_sz);
}

int32_t mpipe_erpc_objects_stream_set_state(erpc_client_t client, uint32_t instance,
					    uint8_t transition)
{
	MpipeStream_client stream(reinterpret_cast<ClientManager *>(client));

	return stream.mpipe_stream_set_state(instance, transition);
}

int32_t mpipe_erpc_objects_stream_eos(erpc_client_t client, uint32_t instance)
{
	MpipeStream_client stream(reinterpret_cast<ClientManager *>(client));

	return stream.mpipe_stream_eos(instance);
}
#endif /* CONFIG_MPIPE_ERPC */

#if defined(CONFIG_MPIPE_ERPC_SERVER)
erpc_server_t mpipe_erpc_objects_server_create(
	const char *ept_name, const struct mpipe_erpc_objects_stream_handler *handler, void *ctx)
{
	struct server_slot *slot = NULL;

	(void)k_mutex_lock(&objects_lock, K_FOREVER);
	if (servers_used < ARRAY_SIZE(servers)) {
		slot = &servers[servers_used];
		servers_used++;
	}
	(void)k_mutex_unlock(&objects_lock);

	if (slot == NULL || !ipc_link_open(&slot->link, ept_name, 0U)) {
		return NULL;
	}

	slot->server.construct();
	slot->server->setTransport(slot->link.transport.get());
	slot->server->setCodecFactory(get_codec_factory());
	slot->server->setMessageBufferFactory(slot->link.factory.get());

	slot->handler.construct(handler, ctx);
	slot->service.construct(slot->handler.get());
	slot->server->addService(slot->service.get());

	return reinterpret_cast<erpc_server_t>(slot->server.get());
}

void mpipe_erpc_objects_server_add_service(erpc_server_t server, void *service)
{
	reinterpret_cast<SimpleServer *>(server)->addService(static_cast<Service *>(service));
}

erpc_status_t mpipe_erpc_objects_server_run(erpc_server_t server)
{
	return reinterpret_cast<SimpleServer *>(server)->run();
}
#endif /* CONFIG_MPIPE_ERPC_SERVER */
