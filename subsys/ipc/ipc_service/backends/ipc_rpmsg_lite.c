/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/ipc/ipc_service_backend.h>

#include <zephyr/drivers/mbox.h>
#include <zephyr/dt-bindings/ipc_service/static_vrings.h>

#include "ipc_rpmsg_lite.h"

#define DT_DRV_COMPAT nxp_ipc_rpmsg_lite

#define NUM_INSTANCES DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

#define WQ_STACK_SIZE CONFIG_IPC_SERVICE_BACKEND_RPMSG_LITE_WQ_STACK_SIZE

#define STATE_READY  (0)
#define STATE_BUSY   (1)
#define STATE_INITED (2)

#if defined(CONFIG_THREAD_MAX_NAME_LEN)
#define THREAD_MAX_NAME_LEN CONFIG_THREAD_MAX_NAME_LEN
#else
#define THREAD_MAX_NAME_LEN 1
#endif

/* MBOX Message Queue Buffer */
#define MBOX_MQ_ITEM_SIZE (sizeof(uint32_t))   /** Size of one Item in Message Queue */
#define MBOX_MQ_NO_ITEMS  (10 * NUM_INSTANCES) /** Number of Items in Message Queue */
static char g_mbox_mq_buffer[MBOX_MQ_NO_ITEMS * MBOX_MQ_ITEM_SIZE];

K_THREAD_STACK_DEFINE(g_mbox_stack, WQ_STACK_SIZE);

/* MBOX Work Queue */
static struct k_work g_mbox_work;
static struct k_work_q g_mbox_wq;

/* MBOX Message Queue */
static struct k_msgq g_mbox_mq;

struct backend_data_t {
	/* IPC RPMSG-Lite Instance */
	struct ipc_rpmsg_lite_instance ipc_rpmsg_inst;

	/* General */
	unsigned int role;
	atomic_t state;

	/* TX buffer size */
	int tx_buffer_size;
};

struct backend_config_t {
	unsigned int role;
	unsigned int link_id;
	uintptr_t shm_addr;
	size_t shm_size;
	struct mbox_dt_spec mbox_tx;
	struct mbox_dt_spec mbox_rx;
	unsigned int wq_prio_type;
	unsigned int wq_prio;
	unsigned int id;
	unsigned int buffer_size;
};

static struct backend_config_t *g_inst_conf_ref[NUM_INSTANCES];
static struct backend_data_t *g_inst_data_ref[NUM_INSTANCES];

static void ipc_rpmsg_lite_destroy_ept(struct ipc_rpmsg_lite_ept *ept)
{
	rpmsg_lite_destroy_ept(ept->priv_data.priv_inst_ref, ept->ep);
}

static struct ipc_rpmsg_lite_ept *
get_ept_slot_with_name(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst, const char *name)
{
	struct ipc_rpmsg_lite_ept *rpmsg_ept;

	for (size_t i = 0; i < NUM_ENDPOINTS; i++) {
		rpmsg_ept = &ipc_rpmsg_inst->endpoint[i];

		if (strcmp(name, rpmsg_ept->name) == 0) {
			return &ipc_rpmsg_inst->endpoint[i];
		}
	}

	return NULL;
}

static struct ipc_rpmsg_lite_ept *
get_available_ept_slot(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst)
{
	return get_ept_slot_with_name(ipc_rpmsg_inst, "");
}

static bool check_endpoints_freed(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst)
{
	struct ipc_rpmsg_lite_ept *rpmsg_ept;

	for (size_t i = 0; i < NUM_ENDPOINTS; i++) {
		rpmsg_ept = &ipc_rpmsg_inst->endpoint[i];

		if (rpmsg_ept->bound == true) {
			return false;
		}
	}

	return true;
}

/*
 * Returns:
 *  - true:  when the endpoint was already cached / registered
 *  - false: when the endpoint was never registered before
 *
 * Returns in **rpmsg_ept:
 *  - The endpoint with the name *name if it exists
 *  - The first endpoint slot available when the endpoint with name *name does
 *    not exist
 *  - NULL in case of error
 */
static bool get_ept(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst,
		    struct ipc_rpmsg_lite_ept **ipc_rpmsg_ept, const char *name)
{
	struct ipc_rpmsg_lite_ept *ept;

	ept = get_ept_slot_with_name(ipc_rpmsg_inst, name);
	if (ept != NULL) {
		(*ipc_rpmsg_ept) = ept;
		return true;
	}

	ept = get_available_ept_slot(ipc_rpmsg_inst);
	if (ept != NULL) {
		(*ipc_rpmsg_ept) = ept;
		return false;
	}

	(*ipc_rpmsg_ept) = NULL;

	return false;
}

static void advertise_ept(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst,
			  struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept, const char *name, uint32_t dest)
{
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
	ipc_rpmsg_ept->ep = rpmsg_lite_create_ept(ipc_rpmsg_inst->rpmsg_lite_inst, RL_ADDR_ANY,
						  ipc_rpmsg_inst->cb, ipc_rpmsg_ept,
						  &ipc_rpmsg_ept->ep_context);
#else
	ipc_rpmsg_ept->ep = rpmsg_lite_create_ept(ipc_rpmsg_inst->rpmsg_lite_inst, RL_ADDR_ANY,
						  ipc_rpmsg_inst->cb, ipc_rpmsg_ept);
#endif
	if (ipc_rpmsg_ept->ep == NULL) {
		return;
	}

	/* Announce endpoint creation */
	if (dest == RL_ADDR_ANY) {
		rpmsg_ns_announce(ipc_rpmsg_inst->rpmsg_lite_inst, ipc_rpmsg_ept->ep,
				  ipc_rpmsg_ept->name, RL_NS_CREATE);
	}

	ipc_rpmsg_ept->dest = dest;

	ipc_rpmsg_ept->bound = true;
	if (ipc_rpmsg_inst->bound_cb) {
		ipc_rpmsg_inst->bound_cb(ipc_rpmsg_ept);
	}
}

static void ns_bind_cb(uint32_t new_ept, const char *new_ept_name, uint32_t flags, void *user_data)
{
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst = user_data;
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;
	bool ept_cached;
	const char *name = new_ept_name;
	uint32_t dest = new_ept;

	if (ipc_rpmsg_inst == NULL) {
		return;
	}

	if (name == NULL || name[0] == '\0') {
		return;
	}

	k_mutex_lock(&ipc_rpmsg_inst->mtx, K_FOREVER);
	ept_cached = get_ept(ipc_rpmsg_inst, &ipc_rpmsg_ept, name);

	if (ipc_rpmsg_ept == NULL) {
		k_mutex_unlock(&ipc_rpmsg_inst->mtx);
		return;
	}

	if (ept_cached) {
		/*
		 * The endpoint was already registered by the HOST core. The
		 * endpoint can now be advertised to the REMOTE core.
		 */
		k_mutex_unlock(&ipc_rpmsg_inst->mtx);
		advertise_ept(ipc_rpmsg_inst, ipc_rpmsg_ept, name, dest);
	} else {
		/*
		 * The endpoint is not registered yet, this happens when the
		 * REMOTE core registers the endpoint before the HOST has
		 * had the chance to register it. Cache it saving name and
		 * destination address to be used by the next register_ept()
		 * call by the HOST core.
		 */
		strncpy(ipc_rpmsg_ept->name, name, sizeof(ipc_rpmsg_ept->name));
		ipc_rpmsg_ept->dest = dest;
		k_mutex_unlock(&ipc_rpmsg_inst->mtx);
	}
}

static void bound_cb(struct ipc_rpmsg_lite_ept *ept)
{
	struct ipc_rpmsg_lite_ept_priv *priv_data = &ept->priv_data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_lite_inst = priv_data->priv_inst_ref;

	rpmsg_lite_send(ipc_rpmsg_lite_inst->rpmsg_lite_inst, ept->ep, ept->dest, (char *)"", 0,
			RL_DONT_BLOCK);

	if (ept->cb->bound) {
		ept->cb->bound(ept->priv_data.priv);
	}
}

static int ept_cb(void *payload, uint32_t payload_len, uint32_t src, void *priv)
{
	struct ipc_rpmsg_lite_ept *ept;

	ept = (struct ipc_rpmsg_lite_ept *)priv;

	/*
	 * the remote processor has send a ns announcement, we use an empty
	 * message to advice the remote side that a local endpoint has been
	 * created and that the processor is ready to communicate with this
	 * endpoint
	 *
	 * ipc_rpmsg_register_ept
	 *  rpmsg_send_ns_message --------------> ns_bind_cb
	 *                                        bound_cb
	 *                ept_cb <--------------- rpmsg_send [empty message]
	 *              bound_cb
	 */
	if (payload_len == 0) {
		if (!ept->bound) {
			if (ept->dest == RL_ADDR_ANY) {
				ept->dest = src;
			}
			ept->bound = true;
			bound_cb(ept);
		}
		return RL_SUCCESS;
	}

	if (ept->cb->received) {
		ept->cb->received(payload, payload_len, ept->priv_data.priv);
	}

	return RL_SUCCESS;
}

/*****************************************************************************
 * RPMSG-Lite Platform Porting functions
 *
 * START
 ****************************************************************************/
void platform_notify(uint32_t vector_id)
{
	uint32_t link_id = RL_GET_LINK_ID(vector_id);
	uint32_t vq_id = RL_GET_Q_ID(vector_id);
	(void)vq_id;

	if (link_id >= NUM_INSTANCES) {
		return;
	}

	/* TODO: using only one MBOX Channel with data */
	if (g_inst_conf_ref[0]->mbox_tx.dev) {
		struct mbox_msg msg = {0};

		msg.data = &vector_id;
		msg.size = 4;

		mbox_send_dt(&g_inst_conf_ref[0]->mbox_tx, &msg);
	}
}

int32_t platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
	/* Register ISR to environment layer */
	env_register_isr(vector_id, isr_data);

	return 0;
}

int32_t platform_deinit_interrupt(uint32_t vector_id)
{
	/* Unregister ISR from environment layer */
	env_unregister_isr(vector_id);

	return 0;
}

int32_t platform_init(void)
{
	/* Not used */
	return 0;
}

int32_t platform_deinit(void)
{
	/* Not used */
	return 0;
}

uintptr_t platform_vatopa(void *addr)
{
	return ((uintptr_t)(char *)addr);
}

void *platform_patova(uintptr_t addr)
{
	return ((void *)(char *)addr);
}

int32_t platform_interrupt_enable(uint32_t vector_id)
{
	return (int32_t)vector_id;
}

int32_t platform_interrupt_disable(uint32_t vector_id)
{
	return (int32_t)vector_id;
}
/*****************************************************************************
 * RPMSG-Lite Platform Porting functions
 *
 * END
 ****************************************************************************/

static void mbox_callback_process(struct k_work *item)
{
	struct virtqueue *vq;
	uint32_t msg_data = 0;

	if (k_msgq_get(&g_mbox_mq, &msg_data, K_NO_WAIT) != 0) {
		/* TODO: Handle case when no data in queue */
		return;
	}

	uint32_t link_id = RL_GET_LINK_ID(msg_data);
	uint32_t vq_id = RL_GET_Q_ID(msg_data);

	if (g_inst_data_ref[link_id]->role == ROLE_HOST) {
		vq = (vq_id == 0) ? g_inst_data_ref[link_id]->ipc_rpmsg_inst.rpmsg_lite_inst->rvq
				  : g_inst_data_ref[link_id]->ipc_rpmsg_inst.rpmsg_lite_inst->tvq;
	} else {
		vq = (vq_id == 1) ? g_inst_data_ref[link_id]->ipc_rpmsg_inst.rpmsg_lite_inst->rvq
				  : g_inst_data_ref[link_id]->ipc_rpmsg_inst.rpmsg_lite_inst->tvq;
	}

	virtqueue_notification(vq);
}

static void mbox_callback(const struct device *instance, uint32_t channel, void *user_data,
			  struct mbox_msg *msg_data)
{
	if (msg_data == NULL || msg_data->size == 0) {
		/* TODO: Handle case when no data arrive */
		return;
	}

	/* In IRQ than k_msgq_put() and k_work_submit_to_queue() has to be called
	 * sequentially to ensure data and the work item will be on same place in queues.
	 * FIFO: k_msgq_put() -> k_work_submit_to_queue() ->
	 *   Work Item Callback mbox_callback_process() -> k_msgq_get()
	 */

	/* Put the received data to Message Queue */
	if (k_msgq_put(&g_mbox_mq, msg_data->data, K_NO_WAIT) != 0) {
		/* TODO: Handle error case when putting data to queue */
		return;
	}

	k_work_submit_to_queue(&g_mbox_wq, &g_mbox_work);
}

/* TODO: If we use only one MBOX then MBOX data structures can be outside of Instance data
 * The mbox_init() function call is called only once.
 */
static int mbox_init(const struct device *instance)
{
	const struct backend_config_t *conf = instance->config;
	struct backend_data_t *data = instance->data;
	int prio, err;

	prio = (conf->wq_prio_type == PRIO_COOP) ? K_PRIO_COOP(conf->wq_prio)
						 : K_PRIO_PREEMPT(conf->wq_prio);

	k_work_queue_init(&g_mbox_wq);
	k_work_queue_start(&g_mbox_wq, g_mbox_stack, WQ_STACK_SIZE, prio, NULL);

	if (IS_ENABLED(CONFIG_THREAD_NAME)) {
		char name[THREAD_MAX_NAME_LEN];

		snprintk(name, sizeof(name), "mbox_wq #%d", conf->id);
		k_thread_name_set(&g_mbox_wq.thread, name);
	}

	k_work_init(&g_mbox_work, mbox_callback_process);

	k_msgq_init(&g_mbox_mq, g_mbox_mq_buffer, MBOX_MQ_ITEM_SIZE, MBOX_MQ_NO_ITEMS);

	err = mbox_register_callback_dt(&conf->mbox_rx, mbox_callback, data);
	if (err != 0) {
		return err;
	}

	return mbox_set_enabled_dt(&conf->mbox_rx, 1);
}

static int mbox_deinit(const struct device *instance)
{
	const struct backend_config_t *conf = instance->config;
	k_tid_t wq_thread;
	int err;

	err = mbox_set_enabled_dt(&conf->mbox_rx, 0);
	if (err != 0) {
		return err;
	}

	k_work_queue_drain(&g_mbox_wq, 1);

	wq_thread = k_work_queue_thread_get(&g_mbox_wq);
	k_thread_abort(wq_thread);

	k_msgq_purge(&g_mbox_mq);
	k_msgq_cleanup(&g_mbox_mq);

	return 0;
}

static struct ipc_rpmsg_lite_ept *
register_ept_on_host(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst, const struct ipc_ept_cfg *cfg)
{
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;
	bool ept_cached;

	k_mutex_lock(&ipc_rpmsg_inst->mtx, K_FOREVER);

	ept_cached = get_ept(ipc_rpmsg_inst, &ipc_rpmsg_ept, cfg->name);
	if (ipc_rpmsg_ept == NULL) {
		k_mutex_unlock(&ipc_rpmsg_inst->mtx);
		return NULL;
	}

	ipc_rpmsg_ept->cb = &cfg->cb;
	ipc_rpmsg_ept->priv_data.priv = cfg->priv;
	ipc_rpmsg_ept->priv_data.priv_inst_ref = ipc_rpmsg_inst;
	ipc_rpmsg_ept->bound = false;
	ipc_rpmsg_ept->ep_priv = ipc_rpmsg_ept;

	if (ept_cached) {
		/*
		 * The endpoint was cached in the NS bind callback. We can finally
		 * advertise it.
		 */
		k_mutex_unlock(&ipc_rpmsg_inst->mtx);
		advertise_ept(ipc_rpmsg_inst, ipc_rpmsg_ept, cfg->name, ipc_rpmsg_ept->dest);
	} else {
		/*
		 * There is no endpoint in the cache because the REMOTE has
		 * not registered the endpoint yet. Cache it.
		 */
		strncpy(ipc_rpmsg_ept->name, cfg->name, sizeof(ipc_rpmsg_ept->name));
		k_mutex_unlock(&ipc_rpmsg_inst->mtx);
	}

	return ipc_rpmsg_ept;
}

static struct ipc_rpmsg_lite_ept *
register_ept_on_remote(struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst,
		       const struct ipc_ept_cfg *cfg)
{
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;

	ipc_rpmsg_ept = get_available_ept_slot(ipc_rpmsg_inst);
	if (ipc_rpmsg_ept == NULL) {
		return NULL;
	}

	ipc_rpmsg_ept->cb = &cfg->cb;
	ipc_rpmsg_ept->priv_data.priv = cfg->priv;
	ipc_rpmsg_ept->priv_data.priv_inst_ref = ipc_rpmsg_inst;
	ipc_rpmsg_ept->bound = false;
	ipc_rpmsg_ept->ep_priv = ipc_rpmsg_ept;
	ipc_rpmsg_ept->dest = RL_ADDR_ANY;

	strncpy(ipc_rpmsg_ept->name, cfg->name, sizeof(ipc_rpmsg_ept->name));

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
	ipc_rpmsg_ept->ep = rpmsg_lite_create_ept(ipc_rpmsg_inst->rpmsg_lite_inst, RL_ADDR_ANY,
						  ipc_rpmsg_inst->cb, ipc_rpmsg_ept,
						  &ipc_rpmsg_ept->ep_context);
#else
	ipc_rpmsg_ept->ep = rpmsg_lite_create_ept(ipc_rpmsg_inst->rpmsg_lite_inst, RL_ADDR_ANY,
						  ipc_rpmsg_inst->cb, ipc_rpmsg_ept);
#endif
	if (ipc_rpmsg_ept->ep == NULL) {
		return NULL;
	}

	/* Announce endpoint creation */
	rpmsg_ns_announce(ipc_rpmsg_inst->rpmsg_lite_inst, ipc_rpmsg_ept->ep, ipc_rpmsg_ept->name,
			  RL_NS_CREATE);

	return ipc_rpmsg_ept;
}

static int register_ept(const struct device *instance, void **token, const struct ipc_ept_cfg *cfg)
{
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_instance *rpmsg_inst;
	struct ipc_rpmsg_lite_ept *rpmsg_ept;

	/* Instance is not ready */
	if (atomic_get(&data->state) != STATE_INITED) {
		return -EBUSY;
	}

	/* Empty name is not valid */
	if (cfg->name == NULL || cfg->name[0] == '\0') {
		return -EINVAL;
	}

	rpmsg_inst = &data->ipc_rpmsg_inst;

	rpmsg_ept = (data->role == ROLE_HOST) ? register_ept_on_host(rpmsg_inst, cfg)
					      : register_ept_on_remote(rpmsg_inst, cfg);
	if (rpmsg_ept == NULL) {
		return -EINVAL;
	}

	(*token) = rpmsg_ept;

	return 0;
}

static int deregister_ept(const struct device *instance, void *token)
{
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;
	static struct k_work_sync sync;

	/* Instance is not ready */
	if (atomic_get(&data->state) != STATE_INITED) {
		return -EBUSY;
	}

	ipc_rpmsg_ept = (struct ipc_rpmsg_lite_ept *)token;

	/* Endpoint is not registered with instance */
	if (!ipc_rpmsg_ept) {
		return -ENOENT;
	}

	/* Drain pending work items before tearing down channel.
	 *
	 * Note: `k_work_flush` Faults on Cortex-M33 with "illegal use of EPSR"
	 * if `sync` is not declared static.
	 */
	k_work_flush(&g_mbox_work, &sync);

	ipc_rpmsg_lite_destroy_ept(ipc_rpmsg_ept);

	memset(ipc_rpmsg_ept, 0, sizeof(struct ipc_rpmsg_lite_ept));

	return 0;
}

static int send(const struct device *instance, void *token, const void *msg, size_t len)
{
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;
	int ret = -1;

	/* Instance is not ready */
	if (atomic_get(&data->state) != STATE_INITED) {
		return -EBUSY;
	}

	/* Empty message is not allowed */
	if (len == 0) {
		return -EBADMSG;
	}

	ipc_rpmsg_ept = (struct ipc_rpmsg_lite_ept *)token;

	/* Endpoint is not registered with instance */
	if (!ipc_rpmsg_ept) {
		return -ENOENT;
	}

	struct ipc_rpmsg_lite_ept_priv *priv_data = &ipc_rpmsg_ept->priv_data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_lite_inst = priv_data->priv_inst_ref;

	ret = rpmsg_lite_send(ipc_rpmsg_lite_inst->rpmsg_lite_inst, ipc_rpmsg_ept->ep,
			      ipc_rpmsg_ept->dest, (char *)msg, len, RL_DONT_BLOCK);

	/* No buffers available */
	if (ret != RL_SUCCESS) {
		return -ENOMEM;
	}

	return ret;
}

static int send_nocopy(const struct device *instance, void *token, const void *msg, size_t len)
{
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;

	/* Instance is not ready */
	if (atomic_get(&data->state) != STATE_INITED) {
		return -EBUSY;
	}

	/* Empty message is not allowed */
	if (len == 0) {
		return -EBADMSG;
	}

	ipc_rpmsg_ept = (struct ipc_rpmsg_lite_ept *)token;

	/* Endpoint is not registered with instance */
	if (!ipc_rpmsg_ept) {
		return -ENOENT;
	}

	struct ipc_rpmsg_lite_ept_priv *priv_data = &ipc_rpmsg_ept->priv_data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_lite_inst = priv_data->priv_inst_ref;

	return rpmsg_lite_send_nocopy(ipc_rpmsg_lite_inst->rpmsg_lite_inst, ipc_rpmsg_ept->ep,
				      ipc_rpmsg_ept->dest, (void *)msg, len);
}

static int open(const struct device *instance)
{
	const struct backend_config_t *conf = instance->config;
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst;
	int err;

	if (!atomic_cas(&data->state, STATE_READY, STATE_BUSY)) {
		return -EALREADY;
	}

	/* Initialize mbox only for one Instance
	 * All IPC instances will use same MBOX Channel otherwise:
	 *  TODO: Handle to check instances MBOXes and chose only one.
	 */
	if (conf->link_id == 0) {
		err = mbox_init(instance);
		if (err != 0) {
			goto error;
		}
	}

	ipc_rpmsg_inst = &data->ipc_rpmsg_inst;

	ipc_rpmsg_inst->bound_cb = bound_cb;
	ipc_rpmsg_inst->cb = ept_cb;

	if (conf->role == ROLE_HOST) {
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
		ipc_rpmsg_inst->rpmsg_lite_inst = rpmsg_lite_master_init(
			(void *)conf->shm_addr, conf->shm_size, conf->link_id, RL_NO_FLAGS,
			&ipc_rpmsg_inst->rpmsg_lite_context);
#elif defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
		ipc_rpmsg_inst->rpmsg_lite_inst = rpmsg_lite_master_init(
			(void *)conf->shm_addr, conf->link_id, RL_NO_FLAGS, NULL);
#else
		ipc_rpmsg_inst->rpmsg_lite_inst =
			rpmsg_lite_master_init((void *)conf->shm_addr, conf->link_id, RL_NO_FLAGS);
#endif
	} else {
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
		ipc_rpmsg_inst->rpmsg_lite_inst =
			rpmsg_lite_remote_init((void *)conf->shm_addr, conf->link_id, RL_NO_FLAGS,
					       &ipc_rpmsg_inst->rpmsg_lite_context);
#elif defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
		ipc_rpmsg_inst->rpmsg_lite_inst = rpmsg_lite_remote_init(
			(void *)conf->shm_addr, conf->link_id, RL_NO_FLAGS, NULL);
#else
		ipc_rpmsg_inst->rpmsg_lite_inst =
			rpmsg_lite_remote_init((void *)conf->shm_addr, conf->link_id, RL_NO_FLAGS);
#endif
		rpmsg_lite_wait_for_link_up(ipc_rpmsg_inst->rpmsg_lite_inst, RL_BLOCK);
	}

	if (ipc_rpmsg_inst->rpmsg_lite_inst == NULL) {
		return -1;
	}

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
	ipc_rpmsg_inst->ns_handle =
		rpmsg_ns_bind(ipc_rpmsg_inst->rpmsg_lite_inst, ns_bind_cb, ipc_rpmsg_inst,
			      &ipc_rpmsg_inst->rpmsg_lite_ns_context);
#else
	ipc_rpmsg_inst->ns_handle =
		rpmsg_ns_bind(ipc_rpmsg_inst->rpmsg_lite_inst, ns_bind_cb, ipc_rpmsg_inst);
#endif /* RL_USE_STATIC_API */

	if (ipc_rpmsg_inst->ns_handle == NULL) {
		return -1;
	}

	data->tx_buffer_size = (RL_BUFFER_PAYLOAD_SIZE);
	if (data->tx_buffer_size < 0) {
		err = -EINVAL;
		goto error;
	}

	atomic_set(&data->state, STATE_INITED);
	return 0;

error:
	/* Back to the ready state */
	atomic_set(&data->state, STATE_READY);
	return err;
}

static int close(const struct device *instance)
{
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_inst;
	int err;

	if (!atomic_cas(&data->state, STATE_INITED, STATE_BUSY)) {
		return -EALREADY;
	}

	ipc_rpmsg_inst = &data->ipc_rpmsg_inst;

	if (!check_endpoints_freed(ipc_rpmsg_inst)) {
		return -EBUSY;
	}

	err = rpmsg_lite_deinit(ipc_rpmsg_inst->rpmsg_lite_inst);
	if (err != 0) {
		goto error;
	}

	err = mbox_deinit(instance);
	if (err != 0) {
		goto error;
	}

	memset(ipc_rpmsg_inst, 0, sizeof(struct ipc_rpmsg_lite_instance));

	atomic_set(&data->state, STATE_READY);
	return 0;

error:
	/* Back to the inited state */
	atomic_set(&data->state, STATE_INITED);
	return err;
}

static int get_tx_buffer_size(const struct device *instance, void *token)
{
	struct backend_data_t *data = instance->data;

	return data->tx_buffer_size;
}

static int get_tx_buffer(const struct device *instance, void *token, void **r_data, uint32_t *size,
			 k_timeout_t wait)
{
	struct backend_data_t *data = instance->data;
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;
	void *payload;

	ipc_rpmsg_ept = (struct ipc_rpmsg_lite_ept *)token;

	/* Endpoint is not registered with instance */
	if (!ipc_rpmsg_ept) {
		return -ENOENT;
	}

	if (!r_data || !size) {
		return -EINVAL;
	}

	/* If not specified wait K_FOREVER or K_NO_WAIT then
	 * let's wait for approximately 15 seconds == 150 * RL_MS_PER_INTERVAL
	 */
	uint32_t wait_time = 150;

	if (K_TIMEOUT_EQ(wait, K_FOREVER)) {
		wait_time = RL_BLOCK;
	}

	if (K_TIMEOUT_EQ(wait, K_NO_WAIT)) {
		wait_time = RL_DONT_BLOCK;
	}

	/* The user requested a specific size */
	if ((*size) && (*size > data->tx_buffer_size)) {
		/* Too big to fit */
		*size = data->tx_buffer_size;
		return -ENOMEM;
	}

	struct ipc_rpmsg_lite_ept_priv *priv_data = &ipc_rpmsg_ept->priv_data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_lite_inst = priv_data->priv_inst_ref;

	payload = rpmsg_lite_alloc_tx_buffer(ipc_rpmsg_lite_inst->rpmsg_lite_inst, size, wait_time);

	/* This should really only be valid for K_NO_WAIT */
	if (!payload) {
		return -ENOBUFS;
	}

	(*r_data) = payload;

	return 0;
}

static int hold_rx_buffer(const struct device *instance, void *token, void *data)
{
	/* Not supported by RPMSG-Lite */
	return -ENOTSUP;
}

static int release_rx_buffer(const struct device *instance, void *token, void *data)
{
	struct ipc_rpmsg_lite_ept *ipc_rpmsg_ept;

	ipc_rpmsg_ept = (struct ipc_rpmsg_lite_ept *)token;

	/* Endpoint is not registered with instance */
	if (!ipc_rpmsg_ept) {
		return -ENOENT;
	}

	struct ipc_rpmsg_lite_ept_priv *priv_data = &ipc_rpmsg_ept->priv_data;
	struct ipc_rpmsg_lite_instance *ipc_rpmsg_lite_inst = priv_data->priv_inst_ref;

	rpmsg_lite_release_rx_buffer(ipc_rpmsg_lite_inst->rpmsg_lite_inst, data);

	return 0;
}

static int drop_tx_buffer(const struct device *instance, void *token, const void *data)
{
	/* Not supported by RPMSG-Lite */
	return -ENOTSUP;
}

const static struct ipc_service_backend backend_ops = {
	.open_instance = open,
	.close_instance = close,
	.register_endpoint = register_ept,
	.deregister_endpoint = deregister_ept,
	.send = send,
	.send_nocopy = send_nocopy,
	.drop_tx_buffer = drop_tx_buffer,
	.get_tx_buffer = get_tx_buffer,
	.get_tx_buffer_size = get_tx_buffer_size,
	.hold_rx_buffer = hold_rx_buffer,
	.release_rx_buffer = release_rx_buffer,
};

static int backend_init(const struct device *instance)
{
	struct backend_config_t *conf = (struct backend_config_t *)instance->config;
	struct backend_data_t *data = instance->data;

	__ASSERT((conf->link_id < NUM_INSTANCES) == 1U, "The instance-index set in device tree has "
							"to be less then Number of IPC Instances");

	g_inst_conf_ref[conf->link_id] = conf;
	g_inst_data_ref[conf->link_id] = data;

	data->role = conf->role;

	k_mutex_init(&data->ipc_rpmsg_inst.mtx);
	atomic_set(&data->state, STATE_READY);

	return 0;
}

#if defined(CONFIG_ARCH_POSIX)
/* TODO: Not tested yet */
#define BACKEND_PRE(i)      extern char IPC##i##_shm_buffer[];
#define BACKEND_SHM_ADDR(i) (const uintptr_t) IPC##i##_shm_buffer
#else
#define BACKEND_PRE(i)
#define BACKEND_SHM_ADDR(i) DT_REG_ADDR(DT_INST_PHANDLE(i, memory_region))
#endif /* defined(CONFIG_ARCH_POSIX) */

#define DEFINE_BACKEND_DEVICE(i)                                                                   \
	BACKEND_PRE(i)                                                                             \
	static struct backend_config_t backend_config_##i = {                                      \
		.role = DT_ENUM_IDX_OR(DT_DRV_INST(i), role, ROLE_HOST),                           \
		.link_id = DT_INST_PROP_OR(i, link_id, i),                                         \
		.shm_size = DT_REG_SIZE(DT_INST_PHANDLE(i, memory_region)),                        \
		.shm_addr = BACKEND_SHM_ADDR(i),                                                   \
		.mbox_tx = MBOX_DT_SPEC_INST_GET(i, tx),                                           \
		.mbox_rx = MBOX_DT_SPEC_INST_GET(i, rx),                                           \
		.wq_prio = COND_CODE_1(DT_INST_NODE_HAS_PROP(i, zephyr_priority),	\
			   (DT_INST_PROP_BY_IDX(i, zephyr_priority, 0)),		\
			   (0)),                           \
			 .wq_prio_type = COND_CODE_1(DT_INST_NODE_HAS_PROP(i, zephyr_priority),	\
			   (DT_INST_PROP_BY_IDX(i, zephyr_priority, 1)),		\
			   (PRIO_PREEMPT)),       \
				  .buffer_size = DT_INST_PROP_OR(i, zephyr_buffer_size,            \
								 (RL_BUFFER_PAYLOAD_SIZE)),        \
				  .id = i,                                                         \
	};                                                                                         \
                                                                                                   \
	static struct backend_data_t backend_data_##i;                                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(i, &backend_init, NULL, &backend_data_##i, &backend_config_##i,      \
			      POST_KERNEL, CONFIG_IPC_SERVICE_REG_BACKEND_PRIORITY, &backend_ops);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_BACKEND_DEVICE)

#define BACKEND_CONFIG_INIT(n) &backend_config_##n,
