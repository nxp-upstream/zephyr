/* btp_map.c - Bluetooth MAP Tester */

/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/map.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "btp/btp.h"

#define LOG_MODULE_NAME bttester_map
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#define MAP_MAX_INSTANCES 4

/* MAP Client MAS instance tracking */
struct mce_mas_instance {
	struct bt_conn *conn;
	struct bt_map_mce_mas mce_mas;
	uint8_t instance_id;
	bool in_use;
};

/* MAP Client MNS instance tracking */
struct mce_mns_instance {
	struct bt_conn *conn;
	struct bt_map_mce_mns mce_mns;
	struct bt_map_mce_mns_rfcomm_server rfcomm_server;
	struct bt_map_mce_mns_l2cap_server l2cap_server;
	uint8_t instance_id;
	bool in_use;
};

/* MAP Server MAS instance tracking */
struct mse_mas_instance {
	struct bt_conn *conn;
	struct bt_map_mse_mas mse_mas;
	struct bt_map_mse_mas_rfcomm_server rfcomm_server;
	struct bt_map_mse_mas_l2cap_server l2cap_server;
	uint8_t instance_id;
	bool in_use;
};

/* MAP Server MNS instance tracking */
struct mse_mns_instance {
	struct bt_conn *conn;
	struct bt_map_mse_mns mse_mns;
	uint8_t instance_id;
	bool in_use;
};

static struct mce_mas_instance mce_mas_instances[MAP_MAX_INSTANCES];
static struct mce_mns_instance mce_mns_instances[MAP_MAX_INSTANCES];
static struct mse_mas_instance mse_mas_instances[MAP_MAX_INSTANCES];
static struct mse_mns_instance mse_mns_instances[MAP_MAX_INSTANCES];

/* Helper functions to find instances */
static struct mce_mas_instance *find_mce_mas_instance(const bt_addr_t *address, uint8_t instance_id)
{
	struct bt_conn *conn;

	conn = bt_conn_lookup_addr_br(address);
	if (!conn) {
		return NULL;
	}

	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (mce_mas_instances[i].in_use &&
		    mce_mas_instances[i].conn == conn &&
		    mce_mas_instances[i].instance_id == instance_id) {
			bt_conn_unref(conn);
			return &mce_mas_instances[i];
		}
	}

	bt_conn_unref(conn);
	return NULL;
}

static struct mce_mas_instance *get_free_mce_mas_instance(void)
{
	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (!mce_mas_instances[i].in_use) {
			memset(&mce_mas_instances[i], 0, sizeof(mce_mas_instances[i]));
			mce_mas_instances[i].in_use = true;
			return &mce_mas_instances[i];
		}
	}
	return NULL;
}

static struct mce_mns_instance *find_mce_mns_instance(const bt_addr_t *address, uint8_t instance_id)
{
	struct bt_conn *conn;

	conn = bt_conn_lookup_addr_br(address);
	if (!conn) {
		return NULL;
	}

	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (mce_mns_instances[i].in_use &&
		    mce_mns_instances[i].conn == conn &&
		    mce_mns_instances[i].instance_id == instance_id) {
			bt_conn_unref(conn);
			return &mce_mns_instances[i];
		}
	}

	bt_conn_unref(conn);
	return NULL;
}

static struct mce_mns_instance *get_free_mce_mns_instance(void)
{
	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (!mce_mns_instances[i].in_use) {
			memset(&mce_mns_instances[i], 0, sizeof(mce_mns_instances[i]));
			mce_mns_instances[i].in_use = true;
			return &mce_mns_instances[i];
		}
	}
	return NULL;
}

static struct mse_mas_instance *find_mse_mas_instance(const bt_addr_t *address, uint8_t instance_id)
{
	struct bt_conn *conn;

	conn = bt_conn_lookup_addr_br(address);
	if (!conn) {
		return NULL;
	}

	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (mse_mas_instances[i].in_use &&
		    mse_mas_instances[i].conn == conn &&
		    mse_mas_instances[i].instance_id == instance_id) {
			bt_conn_unref(conn);
			return &mse_mas_instances[i];
		}
	}

	bt_conn_unref(conn);
	return NULL;
}

static struct mse_mas_instance *get_free_mse_mas_instance(void)
{
	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (!mse_mas_instances[i].in_use) {
			memset(&mse_mas_instances[i], 0, sizeof(mse_mas_instances[i]));
			mse_mas_instances[i].in_use = true;
			return &mse_mas_instances[i];
		}
	}
	return NULL;
}

static struct mse_mns_instance *find_mse_mns_instance(const bt_addr_t *address, uint8_t instance_id)
{
	struct bt_conn *conn;

	conn = bt_conn_lookup_addr_br(address);
	if (!conn) {
		return NULL;
	}

	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (mse_mns_instances[i].in_use &&
		    mse_mns_instances[i].conn == conn &&
		    mse_mns_instances[i].instance_id == instance_id) {
			bt_conn_unref(conn);
			return &mse_mns_instances[i];
		}
	}

	bt_conn_unref(conn);
	return NULL;
}

static struct mse_mns_instance *get_free_mse_mns_instance(void)
{
	for (int i = 0; i < MAP_MAX_INSTANCES; i++) {
		if (!mse_mns_instances[i].in_use) {
			memset(&mse_mns_instances[i], 0, sizeof(mse_mns_instances[i]));
			mse_mns_instances[i].in_use = true;
			return &mse_mns_instances[i];
		}
	}
	return NULL;
}

/* Helper to get address from connection */
static void get_addr_from_conn(struct bt_conn *conn, bt_addr_t *addr)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) == 0) {
		bt_addr_copy(addr, info.br.dst);
	}
}

/* MAP Client MAS callbacks */
static void mce_mas_rfcomm_connected_cb(struct bt_conn *conn, struct bt_map_mce_mas *mce_mas)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_rfcomm_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_RFCOMM_CONNECTED, &ev, sizeof(ev));
}

static void mce_mas_rfcomm_disconnected_cb(struct bt_map_mce_mas *mce_mas)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_rfcomm_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_RFCOMM_DISCONNECTED, &ev, sizeof(ev));
}

static void mce_mas_l2cap_connected_cb(struct bt_conn *conn, struct bt_map_mce_mas *mce_mas)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_l2cap_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_L2CAP_CONNECTED, &ev, sizeof(ev));
}

static void mce_mas_l2cap_disconnected_cb(struct bt_map_mce_mas *mce_mas)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_l2cap_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_L2CAP_DISCONNECTED, &ev, sizeof(ev));
}

static void mce_mas_connected_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, uint8_t version,
				 uint16_t mopl, struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_connected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->version = version;
	ev->mopl = sys_cpu_to_le16(mopl);
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_CONNECTED, ev, ev_len);
	free(ev);
}

static void mce_mas_disconnected_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				    struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_disconnected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_DISCONNECTED, ev, ev_len);
	free(ev);
}

static void mce_mas_abort_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code, struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_abort_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_ABORT, ev, ev_len);
	free(ev);
}

static void mce_mas_set_ntf_reg_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				   struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_set_ntf_reg_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_SET_NTF_REG, ev, ev_len);
	free(ev);
}

static void mce_mas_set_folder_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				  struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_set_folder_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_SET_FOLDER, ev, ev_len);
	free(ev);
}

static void mce_mas_get_folder_listing_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
					  struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_get_folder_listing_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_GET_FOLDER_LISTING, ev, ev_len);
	free(ev);
}

static void mce_mas_get_msg_listing_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				       struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_get_msg_listing_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_GET_MSG_LISTING, ev, ev_len);
	free(ev);
}

static void mce_mas_get_msg_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
			       struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_get_msg_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_GET_MSG, ev, ev_len);
	free(ev);
}

static void mce_mas_set_msg_status_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				      struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_set_msg_status_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_SET_MSG_STATUS, ev, ev_len);
	free(ev);
}

static void mce_mas_push_msg_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_push_msg_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_PUSH_MSG, ev, ev_len);
	free(ev);
}

static void mce_mas_update_inbox_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				    struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_update_inbox_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_UPDATE_INBOX, ev, ev_len);
	free(ev);
}

static void mce_mas_get_mas_inst_info_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
					 struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_get_mas_inst_info_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_GET_MAS_INST_INFO, ev, ev_len);
	free(ev);
}

static void mce_mas_set_owner_status_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
					struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_set_owner_status_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_SET_OWNER_STATUS, ev, ev_len);
	free(ev);
}

static void mce_mas_get_owner_status_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
					struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_get_owner_status_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_GET_OWNER_STATUS, ev, ev_len);
	free(ev);
}

static void mce_mas_get_convo_listing_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
					 struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_get_convo_listing_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_GET_CONVO_LISTING, ev, ev_len);
	free(ev);
}

static void mce_mas_set_ntf_filter_cb(struct bt_map_mce_mas *mce_mas, uint8_t rsp_code,
				      struct net_buf *buf)
{
	struct mce_mas_instance *inst = CONTAINER_OF(mce_mas, struct mce_mas_instance, mce_mas);
	struct btp_map_mce_mas_set_ntf_filter_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MAS_EV_SET_NTF_FILTER, ev, ev_len);
	free(ev);
}

static const struct bt_map_mce_mas_cb mce_mas_cb = {
	.rfcomm_connected = mce_mas_rfcomm_connected_cb,
	.rfcomm_disconnected = mce_mas_rfcomm_disconnected_cb,
	.l2cap_connected = mce_mas_l2cap_connected_cb,
	.l2cap_disconnected = mce_mas_l2cap_disconnected_cb,
	.connected = mce_mas_connected_cb,
	.disconnected = mce_mas_disconnected_cb,
	.abort = mce_mas_abort_cb,
	.set_ntf_reg = mce_mas_set_ntf_reg_cb,
	.set_folder = mce_mas_set_folder_cb,
	.get_folder_listing = mce_mas_get_folder_listing_cb,
	.get_msg_listing = mce_mas_get_msg_listing_cb,
	.get_msg = mce_mas_get_msg_cb,
	.set_msg_status = mce_mas_set_msg_status_cb,
	.push_msg = mce_mas_push_msg_cb,
	.update_inbox = mce_mas_update_inbox_cb,
	.get_mas_inst_info = mce_mas_get_mas_inst_info_cb,
	.set_owner_status = mce_mas_set_owner_status_cb,
	.get_owner_status = mce_mas_get_owner_status_cb,
	.get_convo_listing = mce_mas_get_convo_listing_cb,
	.set_ntf_filter = mce_mas_set_ntf_filter_cb,
};

/* MAP Client MNS callbacks */
static void mce_mns_rfcomm_connected_cb(struct bt_conn *conn, struct bt_map_mce_mns *mce_mns)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_rfcomm_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_RFCOMM_CONNECTED, &ev, sizeof(ev));
}

static void mce_mns_rfcomm_disconnected_cb(struct bt_map_mce_mns *mce_mns)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_rfcomm_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_RFCOMM_DISCONNECTED, &ev, sizeof(ev));
}

static void mce_mns_l2cap_connected_cb(struct bt_conn *conn, struct bt_map_mce_mns *mce_mns)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_l2cap_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_L2CAP_CONNECTED, &ev, sizeof(ev));
}

static void mce_mns_l2cap_disconnected_cb(struct bt_map_mce_mns *mce_mns)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_l2cap_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_L2CAP_DISCONNECTED, &ev, sizeof(ev));
}

static void mce_mns_connected_cb(struct bt_map_mce_mns *mce_mns, uint8_t version, uint16_t mopl,
				 struct net_buf *buf)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_connected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->version = version;
	ev->mopl = sys_cpu_to_le16(mopl);
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_CONNECTED, ev, ev_len);
	free(ev);
}

static void mce_mns_disconnected_cb(struct bt_map_mce_mns *mce_mns, struct net_buf *buf)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_disconnected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_DISCONNECTED, ev, ev_len);
	free(ev);
}

static void mce_mns_abort_cb(struct bt_map_mce_mns *mce_mns, struct net_buf *buf)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_abort_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_ABORT, ev, ev_len);
	free(ev);
}

static void mce_mns_send_event_cb(struct bt_map_mce_mns *mce_mns, bool final, struct net_buf *buf)
{
	struct mce_mns_instance *inst = CONTAINER_OF(mce_mns, struct mce_mns_instance, mce_mns);
	struct btp_map_mce_mns_send_event_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MCE_MNS_EV_SEND_EVENT, ev, ev_len);
	free(ev);
}

static const struct bt_map_mce_mns_cb mce_mns_cb = {
	.rfcomm_connected = mce_mns_rfcomm_connected_cb,
	.rfcomm_disconnected = mce_mns_rfcomm_disconnected_cb,
	.l2cap_connected = mce_mns_l2cap_connected_cb,
	.l2cap_disconnected = mce_mns_l2cap_disconnected_cb,
	.connected = mce_mns_connected_cb,
	.disconnected = mce_mns_disconnected_cb,
	.abort = mce_mns_abort_cb,
	.send_event = mce_mns_send_event_cb,
};

/* MAP Server MAS callbacks */
static void mse_mas_rfcomm_connected_cb(struct bt_conn *conn, struct bt_map_mse_mas *mse_mas)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_rfcomm_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_RFCOMM_CONNECTED, &ev, sizeof(ev));
}

static void mse_mas_rfcomm_disconnected_cb(struct bt_map_mse_mas *mse_mas)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_rfcomm_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_RFCOMM_DISCONNECTED, &ev, sizeof(ev));
}

static void mse_mas_l2cap_connected_cb(struct bt_conn *conn, struct bt_map_mse_mas *mse_mas)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_l2cap_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_L2CAP_CONNECTED, &ev, sizeof(ev));
}

static void mse_mas_l2cap_disconnected_cb(struct bt_map_mse_mas *mse_mas)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_l2cap_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_L2CAP_DISCONNECTED, &ev, sizeof(ev));
}

static void mse_mas_connected_cb(struct bt_map_mse_mas *mse_mas, uint8_t version, uint16_t mopl,
				 struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_connected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->version = version;
	ev->mopl = sys_cpu_to_le16(mopl);
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_CONNECTED, ev, ev_len);
	free(ev);
}

static void mse_mas_disconnected_cb(struct bt_map_mse_mas *mse_mas, struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_disconnected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_DISCONNECTED, ev, ev_len);
	free(ev);
}

static void mse_mas_abort_cb(struct bt_map_mse_mas *mse_mas, struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_abort_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_ABORT, ev, ev_len);
	free(ev);
}

static void mse_mas_set_ntf_reg_cb(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_set_ntf_reg_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_SET_NTF_REG, ev, ev_len);
	free(ev);
}

static void mse_mas_set_folder_cb(struct bt_map_mse_mas *mse_mas, uint8_t flags,
				  struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_set_folder_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->flags = flags;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_SET_FOLDER, ev, ev_len);
	free(ev);
}

static void mse_mas_get_folder_listing_cb(struct bt_map_mse_mas *mse_mas, bool final,
					  struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_get_folder_listing_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_GET_FOLDER_LISTING, ev, ev_len);
	free(ev);
}

static void mse_mas_get_msg_listing_cb(struct bt_map_mse_mas *mse_mas, bool final,
				       struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_get_msg_listing_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_GET_MSG_LISTING, ev, ev_len);
	free(ev);
}

static void mse_mas_get_msg_cb(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_get_msg_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_GET_MSG, ev, ev_len);
	free(ev);
}

static void mse_mas_set_msg_status_cb(struct bt_map_mse_mas *mse_mas, bool final,
				      struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_set_msg_status_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_SET_MSG_STATUS, ev, ev_len);
	free(ev);
}

static void mse_mas_push_msg_cb(struct bt_map_mse_mas *mse_mas, bool final, struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_push_msg_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_PUSH_MSG, ev, ev_len);
	free(ev);
}

static void mse_mas_update_inbox_cb(struct bt_map_mse_mas *mse_mas, bool final,
				    struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_update_inbox_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_UPDATE_INBOX, ev, ev_len);
	free(ev);
}

static void mse_mas_get_mas_inst_info_cb(struct bt_map_mse_mas *mse_mas, bool final,
					 struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_get_mas_inst_info_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_GET_MAS_INST_INFO, ev, ev_len);
	free(ev);
}

static void mse_mas_set_owner_status_cb(struct bt_map_mse_mas *mse_mas, bool final,
					struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_set_owner_status_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_SET_OWNER_STATUS, ev, ev_len);
	free(ev);
}

static void mse_mas_get_owner_status_cb(struct bt_map_mse_mas *mse_mas, bool final,
					struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_get_owner_status_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_GET_OWNER_STATUS, ev, ev_len);
	free(ev);
}

static void mse_mas_get_convo_listing_cb(struct bt_map_mse_mas *mse_mas, bool final,
					 struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_get_convo_listing_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_GET_CONVO_LISTING, ev, ev_len);
	free(ev);
}

static void mse_mas_set_ntf_filter_cb(struct bt_map_mse_mas *mse_mas, bool final,
				      struct net_buf *buf)
{
	struct mse_mas_instance *inst = CONTAINER_OF(mse_mas, struct mse_mas_instance, mse_mas);
	struct btp_map_mse_mas_set_ntf_filter_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;
	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->final = final ? 1 : 0;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MAS_EV_SET_NTF_FILTER, ev, ev_len);
	free(ev);
}

static const struct bt_map_mse_mas_cb mse_mas_cb = {
	.rfcomm_connected = mse_mas_rfcomm_connected_cb,
	.rfcomm_disconnected = mse_mas_rfcomm_disconnected_cb,
	.l2cap_connected = mse_mas_l2cap_connected_cb,
	.l2cap_disconnected = mse_mas_l2cap_disconnected_cb,
	.connected = mse_mas_connected_cb,
	.disconnected = mse_mas_disconnected_cb,
	.abort = mse_mas_abort_cb,
	.set_ntf_reg = mse_mas_set_ntf_reg_cb,
	.set_folder = mse_mas_set_folder_cb,
	.get_folder_listing = mse_mas_get_folder_listing_cb,
	.get_msg_listing = mse_mas_get_msg_listing_cb,
	.get_msg = mse_mas_get_msg_cb,
	.set_msg_status = mse_mas_set_msg_status_cb,
	.push_msg = mse_mas_push_msg_cb,
	.update_inbox = mse_mas_update_inbox_cb,
	.get_mas_inst_info = mse_mas_get_mas_inst_info_cb,
	.set_owner_status = mse_mas_set_owner_status_cb,
	.get_owner_status = mse_mas_get_owner_status_cb,
	.get_convo_listing = mse_mas_get_convo_listing_cb,
	.set_ntf_filter = mse_mas_set_ntf_filter_cb,
};

/* MAP Server MNS callbacks */
static void mse_mns_rfcomm_connected_cb(struct bt_conn *conn, struct bt_map_mse_mns *mse_mns)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_rfcomm_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_RFCOMM_CONNECTED, &ev, sizeof(ev));
}

static void mse_mns_rfcomm_disconnected_cb(struct bt_map_mse_mns *mse_mns)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_rfcomm_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_RFCOMM_DISCONNECTED, &ev, sizeof(ev));
}

static void mse_mns_l2cap_connected_cb(struct bt_conn *conn, struct bt_map_mse_mns *mse_mns)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_l2cap_connected_ev ev;

	get_addr_from_conn(conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_L2CAP_CONNECTED, &ev, sizeof(ev));
}

static void mse_mns_l2cap_disconnected_cb(struct bt_map_mse_mns *mse_mns)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_l2cap_disconnected_ev ev;

	get_addr_from_conn(inst->conn, &ev.address);
	ev.instance_id = inst->instance_id;

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_L2CAP_DISCONNECTED, &ev, sizeof(ev));
}

static void mse_mns_connected_cb(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code, uint8_t version,
				 uint16_t mopl, struct net_buf *buf)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_connected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->version = version;
	ev->mopl = sys_cpu_to_le16(mopl);
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_CONNECTED, ev, ev_len);
	free(ev);
}

static void mse_mns_disconnected_cb(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code,
				    struct net_buf *buf)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_disconnected_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_DISCONNECTED, ev, ev_len);
	free(ev);
}

static void mse_mns_abort_cb(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code, struct net_buf *buf)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_abort_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_ABORT, ev, ev_len);
	free(ev);
}

static void mse_mns_send_event_cb(struct bt_map_mse_mns *mse_mns, uint8_t rsp_code,
				  struct net_buf *buf)
{
	struct mse_mns_instance *inst = CONTAINER_OF(mse_mns, struct mse_mns_instance, mse_mns);
	struct btp_map_mse_mns_send_event_ev *ev;
	uint16_t buf_len = buf ? buf->len : 0;
	uint16_t ev_len = sizeof(*ev) + buf_len;

	ev = malloc(ev_len);
	if (!ev) {
		return;
	}

	get_addr_from_conn(inst->conn, &ev->address);
	ev->instance_id = inst->instance_id;
	ev->rsp_code = rsp_code;
	ev->buf_len = sys_cpu_to_le16(buf_len);
	if (buf_len > 0) {
		memcpy(ev->buf, buf->data, buf_len);
	}

	tester_event(BTP_SERVICE_ID_MAP, BTP_MAP_MSE_MNS_EV_SEND_EVENT, ev, ev_len);
	free(ev);
}

static const struct bt_map_mse_mns_cb mse_mns_cb = {
	.rfcomm_connected = mse_mns_rfcomm_connected_cb,
	.rfcomm_disconnected = mse_mns_rfcomm_disconnected_cb,
	.l2cap_connected = mse_mns_l2cap_connected_cb,
	.l2cap_disconnected = mse_mns_l2cap_disconnected_cb,
	.connected = mse_mns_connected_cb,
	.disconnected = mse_mns_disconnected_cb,
	.abort = mse_mns_abort_cb,
	.send_event = mse_mns_send_event_cb,
};

/* BTP command handlers - MAP Client MAS */
static uint8_t mce_mas_rfcomm_connect(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_rfcomm_connect_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct bt_conn *conn;

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mce_mas_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mce_mas_cb_register(&inst->mce_mas, &mce_mas_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	conn = bt_conn_lookup_addr_br(&cp->address);
	if (!conn) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mce_mas_rfcomm_connect(conn, &inst->mce_mas, cp->channel) < 0) {
		bt_conn_unref(conn);
		return BTP_STATUS_FAILED;
	}

	bt_conn_unref(conn);
	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_rfcomm_disconnect(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_rfcomm_disconnect_cmd *cp = cmd;
	struct mce_mas_instance *inst;

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mce_mas_rfcomm_disconnect(&inst->mce_mas) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_l2cap_connect(const void *cmd, uint16_t cmd_len,
				     void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_l2cap_connect_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct bt_conn *conn;

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mce_mas_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mce_mas_cb_register(&inst->mce_mas, &mce_mas_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	conn = bt_conn_lookup_addr_br(&cp->address);
	if (!conn) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mce_mas_l2cap_connect(conn, &inst->mce_mas, sys_le16_to_cpu(cp->psm)) < 0) {
		bt_conn_unref(conn);
		return BTP_STATUS_FAILED;
	}

	bt_conn_unref(conn);
	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_l2cap_disconnect(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_l2cap_disconnect_cmd *cp = cmd;
	struct mce_mas_instance *inst;

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mce_mas_l2cap_disconnect(&inst->mce_mas) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_connect(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_connect_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_connect(&inst->mce_mas, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_disconnect(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_disconnect_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_disconnect(&inst->mce_mas, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_abort(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_abort_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_abort(&inst->mce_mas, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_set_folder(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_set_folder_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_set_folder(&inst->mce_mas, cp->flags, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_set_ntf_reg(const void *cmd, uint16_t cmd_len,
				   void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_set_ntf_reg_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_set_ntf_reg(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_get_folder_listing(const void *cmd, uint16_t cmd_len,
					  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_get_folder_listing_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_get_folder_listing(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_get_msg_listing(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_get_msg_listing_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_get_msg_listing(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_get_msg(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_get_msg_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_get_msg(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_set_msg_status(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_set_msg_status_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_set_msg_status(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_push_msg(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_push_msg_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_push_msg(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_update_inbox(const void *cmd, uint16_t cmd_len,
				    void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_update_inbox_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_update_inbox(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_get_mas_inst_info(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_get_mas_inst_info_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_get_mas_inst_info(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_set_owner_status(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_set_owner_status_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_set_owner_status(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_get_owner_status(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_get_owner_status_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_get_owner_status(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_get_convo_listing(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_get_convo_listing_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_get_convo_listing(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mas_set_ntf_filter(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mas_set_ntf_filter_cmd *cp = cmd;
	struct mce_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mas_create_pdu(&inst->mce_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mas_set_ntf_filter(&inst->mce_mas, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* BTP command handlers - MAP Client MNS */
static int mce_mns_accept_rfcomm(struct bt_conn *conn,
				 struct bt_map_mce_mns_rfcomm_server *server,
				 struct bt_map_mce_mns **mce_mns)
{
	struct mce_mns_instance *inst;

	inst = get_free_mce_mns_instance();
	if (!inst) {
		return -ENOMEM;
	}

	inst->conn = bt_conn_ref(conn);
	*mce_mns = &inst->mce_mns;

	return 0;
}

static int mce_mns_accept_l2cap(struct bt_conn *conn,
				struct bt_map_mce_mns_l2cap_server *server,
				struct bt_map_mce_mns **mce_mns)
{
	struct mce_mns_instance *inst;

	inst = get_free_mce_mns_instance();
	if (!inst) {
		return -ENOMEM;
	}

	inst->conn = bt_conn_ref(conn);
	*mce_mns = &inst->mce_mns;

	return 0;
}

static uint8_t mce_mns_rfcomm_register(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_rfcomm_register_cmd *cp = cmd;
	struct mce_mns_instance *inst;

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mce_mns_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mce_mns_cb_register(&inst->mce_mns, &mce_mns_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	inst->rfcomm_server.server.accept = NULL;
	inst->rfcomm_server.accept = mce_mns_accept_rfcomm;

	if (bt_map_mce_mns_rfcomm_register(&inst->rfcomm_server) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_rfcomm_disconnect(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_rfcomm_disconnect_cmd *cp = cmd;
	struct mce_mns_instance *inst;

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mce_mns_rfcomm_disconnect(&inst->mce_mns) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_l2cap_register(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_l2cap_register_cmd *cp = cmd;
	struct mce_mns_instance *inst;

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mce_mns_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mce_mns_cb_register(&inst->mce_mns, &mce_mns_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	inst->l2cap_server.server.accept = NULL;
	inst->l2cap_server.accept = mce_mns_accept_l2cap;

	if (bt_map_mce_mns_l2cap_register(&inst->l2cap_server) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_l2cap_disconnect(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_l2cap_disconnect_cmd *cp = cmd;
	struct mce_mns_instance *inst;

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mce_mns_l2cap_disconnect(&inst->mce_mns) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_connect(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_connect_cmd *cp = cmd;
	struct mce_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mns_create_pdu(&inst->mce_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mns_connect(&inst->mce_mns, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_disconnect(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_disconnect_cmd *cp = cmd;
	struct mce_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mns_create_pdu(&inst->mce_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mns_disconnect(&inst->mce_mns, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_abort(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_abort_cmd *cp = cmd;
	struct mce_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mns_create_pdu(&inst->mce_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mns_abort(&inst->mce_mns, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mce_mns_send_event(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mce_mns_send_event_cmd *cp = cmd;
	struct mce_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mce_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mce_mns_create_pdu(&inst->mce_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mce_mns_send_event(&inst->mce_mns, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* BTP command handlers - MAP Server MAS */
static int mse_mas_accept_rfcomm(struct bt_conn *conn,
				 struct bt_map_mse_mas_rfcomm_server *server,
				 struct bt_map_mse_mas **mse_mas)
{
	struct mse_mas_instance *inst;

	inst = get_free_mse_mas_instance();
	if (!inst) {
		return -ENOMEM;
	}

	inst->conn = bt_conn_ref(conn);
	*mse_mas = &inst->mse_mas;

	return 0;
}

static int mse_mas_accept_l2cap(struct bt_conn *conn,
				struct bt_map_mse_mas_l2cap_server *server,
				struct bt_map_mse_mas **mse_mas)
{
	struct mse_mas_instance *inst;

	inst = get_free_mse_mas_instance();
	if (!inst) {
		return -ENOMEM;
	}

	inst->conn = bt_conn_ref(conn);
	*mse_mas = &inst->mse_mas;

	return 0;
}

static uint8_t mse_mas_rfcomm_register(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_rfcomm_register_cmd *cp = cmd;
	struct mse_mas_instance *inst;

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mse_mas_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mse_mas_cb_register(&inst->mse_mas, &mse_mas_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	inst->rfcomm_server.server.accept = NULL;
	inst->rfcomm_server.accept = mse_mas_accept_rfcomm;

	if (bt_map_mse_mas_rfcomm_register(&inst->rfcomm_server) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_rfcomm_disconnect(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_rfcomm_disconnect_cmd *cp = cmd;
	struct mse_mas_instance *inst;

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mse_mas_rfcomm_disconnect(&inst->mse_mas) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_l2cap_register(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_l2cap_register_cmd *cp = cmd;
	struct mse_mas_instance *inst;

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mse_mas_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mse_mas_cb_register(&inst->mse_mas, &mse_mas_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	inst->l2cap_server.server.accept = NULL;
	inst->l2cap_server.accept = mse_mas_accept_l2cap;

	if (bt_map_mse_mas_l2cap_register(&inst->l2cap_server) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_l2cap_disconnect(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_l2cap_disconnect_cmd *cp = cmd;
	struct mse_mas_instance *inst;

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mse_mas_l2cap_disconnect(&inst->mse_mas) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_connect(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_connect_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_connect(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_disconnect(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_disconnect_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_disconnect(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_abort(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_abort_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_abort(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_set_folder(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_set_folder_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_set_folder(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_set_ntf_reg(const void *cmd, uint16_t cmd_len,
				   void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_set_ntf_reg_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_set_ntf_reg(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_get_folder_listing(const void *cmd, uint16_t cmd_len,
					  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_get_folder_listing_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_get_folder_listing(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_get_msg_listing(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_get_msg_listing_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_get_msg_listing(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_get_msg(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_get_msg_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_get_msg(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_set_msg_status(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_set_msg_status_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_set_msg_status(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_push_msg(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_push_msg_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_push_msg(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_update_inbox(const void *cmd, uint16_t cmd_len,
				    void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_update_inbox_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_update_inbox(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_get_mas_inst_info(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_get_mas_inst_info_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_get_mas_inst_info(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_set_owner_status(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_set_owner_status_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_set_owner_status(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_get_owner_status(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_get_owner_status_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_get_owner_status(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_get_convo_listing(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_get_convo_listing_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_get_convo_listing(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mas_set_ntf_filter(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mas_set_ntf_filter_cmd *cp = cmd;
	struct mse_mas_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mas_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mas_create_pdu(&inst->mse_mas, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mas_set_ntf_filter(&inst->mse_mas, cp->rsp_code, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* BTP command handlers - MAP Server MNS */
static uint8_t mse_mns_rfcomm_connect(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_rfcomm_connect_cmd *cp = cmd;
	struct mse_mns_instance *inst;
	struct bt_conn *conn;

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mse_mns_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mse_mns_cb_register(&inst->mse_mns, &mse_mns_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	conn = bt_conn_lookup_addr_br(&cp->address);
	if (!conn) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mse_mns_rfcomm_connect(conn, &inst->mse_mns, cp->channel) < 0) {
		bt_conn_unref(conn);
		return BTP_STATUS_FAILED;
	}

	bt_conn_unref(conn);
	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_rfcomm_disconnect(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_rfcomm_disconnect_cmd *cp = cmd;
	struct mse_mns_instance *inst;

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mse_mns_rfcomm_disconnect(&inst->mse_mns) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_l2cap_connect(const void *cmd, uint16_t cmd_len,
				     void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_l2cap_connect_cmd *cp = cmd;
	struct mse_mns_instance *inst;
	struct bt_conn *conn;

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		inst = get_free_mse_mns_instance();
		if (!inst) {
			return BTP_STATUS_FAILED;
		}

		inst->conn = bt_conn_lookup_addr_br(&cp->address);
		if (!inst->conn) {
			inst->in_use = false;
			return BTP_STATUS_FAILED;
		}
		inst->instance_id = cp->instance_id;
	}

	if (bt_map_mse_mns_cb_register(&inst->mse_mns, &mse_mns_cb) < 0) {
		if (inst->conn) {
			bt_conn_unref(inst->conn);
		}
		inst->in_use = false;
		return BTP_STATUS_FAILED;
	}

	conn = bt_conn_lookup_addr_br(&cp->address);
	if (!conn) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mse_mns_l2cap_connect(conn, &inst->mse_mns, sys_le16_to_cpu(cp->psm)) < 0) {
		bt_conn_unref(conn);
		return BTP_STATUS_FAILED;
	}

	bt_conn_unref(conn);
	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_l2cap_disconnect(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_l2cap_disconnect_cmd *cp = cmd;
	struct mse_mns_instance *inst;

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (bt_map_mse_mns_l2cap_disconnect(&inst->mse_mns) < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_connect(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_connect_cmd *cp = cmd;
	struct mse_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mns_create_pdu(&inst->mse_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mns_connect(&inst->mse_mns, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_disconnect(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_disconnect_cmd *cp = cmd;
	struct mse_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mns_create_pdu(&inst->mse_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mns_disconnect(&inst->mse_mns, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_abort(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_abort_cmd *cp = cmd;
	struct mse_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mns_create_pdu(&inst->mse_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mns_abort(&inst->mse_mns, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t mse_mns_send_event(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_map_mse_mns_send_event_cmd *cp = cmd;
	struct mse_mns_instance *inst;
	struct net_buf *buf = NULL;
	uint16_t buf_len;

	if (cmd_len < sizeof(*cp)) {
		return BTP_STATUS_FAILED;
	}

	buf_len = sys_le16_to_cpu(cp->buf_len);
	if (cmd_len != sizeof(*cp) + buf_len) {
		return BTP_STATUS_FAILED;
	}

	inst = find_mse_mns_instance(&cp->address, cp->instance_id);
	if (!inst) {
		return BTP_STATUS_FAILED;
	}

	if (buf_len > 0) {
		buf = bt_map_mse_mns_create_pdu(&inst->mse_mns, NULL);
		if (!buf) {
			return BTP_STATUS_FAILED;
		}
		net_buf_add_mem(buf, cp->buf, buf_len);
	}

	if (bt_map_mse_mns_send_event(&inst->mse_mns, cp->final, buf) < 0) {
		if (buf) {
			net_buf_unref(buf);
		}
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_map_read_supported_commands_rp *rp = rsp;

	*rsp_len = tester_supported_commands(BTP_SERVICE_ID_MAP, rp->data);
	*rsp_len += sizeof(*rp);

	return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_MAP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_RFCOMM_CONNECT,
		.expect_len = sizeof(struct btp_map_mce_mas_rfcomm_connect_cmd),
		.func = mce_mas_rfcomm_connect,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_RFCOMM_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mce_mas_rfcomm_disconnect_cmd),
		.func = mce_mas_rfcomm_disconnect,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_L2CAP_CONNECT,
		.expect_len = sizeof(struct btp_map_mce_mas_l2cap_connect_cmd),
		.func = mce_mas_l2cap_connect,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_L2CAP_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mce_mas_l2cap_disconnect_cmd),
		.func = mce_mas_l2cap_disconnect,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_CONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_connect,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_DISCONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_disconnect,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_ABORT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_abort,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_SET_FOLDER,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_set_folder,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_SET_NTF_REG,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_set_ntf_reg,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_GET_FOLDER_LISTING,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_get_folder_listing,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_GET_MSG_LISTING,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_get_msg_listing,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_GET_MSG,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_get_msg,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_SET_MSG_STATUS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_set_msg_status,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_PUSH_MSG,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_push_msg,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_UPDATE_INBOX,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_update_inbox,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_GET_MAS_INST_INFO,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_get_mas_inst_info,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_SET_OWNER_STATUS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_set_owner_status,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_GET_OWNER_STATUS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_get_owner_status,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_GET_CONVO_LISTING,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_get_convo_listing,
	},
	{
		.opcode = BTP_MAP_MCE_MAS_SET_NTF_FILTER,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mas_set_ntf_filter,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_RFCOMM_REGISTER,
		.expect_len = sizeof(struct btp_map_mce_mns_rfcomm_register_cmd),
		.func = mce_mns_rfcomm_register,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_RFCOMM_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mce_mns_rfcomm_disconnect_cmd),
		.func = mce_mns_rfcomm_disconnect,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_L2CAP_REGISTER,
		.expect_len = sizeof(struct btp_map_mce_mns_l2cap_register_cmd),
		.func = mce_mns_l2cap_register,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_L2CAP_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mce_mns_l2cap_disconnect_cmd),
		.func = mce_mns_l2cap_disconnect,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_CONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mns_connect,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_DISCONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mns_disconnect,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_ABORT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mns_abort,
	},
	{
		.opcode = BTP_MAP_MCE_MNS_SEND_EVENT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mce_mns_send_event,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_RFCOMM_REGISTER,
		.expect_len = sizeof(struct btp_map_mse_mas_rfcomm_register_cmd),
		.func = mse_mas_rfcomm_register,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_RFCOMM_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mse_mas_rfcomm_disconnect_cmd),
		.func = mse_mas_rfcomm_disconnect,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_L2CAP_REGISTER,
		.expect_len = sizeof(struct btp_map_mse_mas_l2cap_register_cmd),
		.func = mse_mas_l2cap_register,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_L2CAP_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mse_mas_l2cap_disconnect_cmd),
		.func = mse_mas_l2cap_disconnect,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_CONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_connect,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_DISCONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_disconnect,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_ABORT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_abort,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_SET_FOLDER,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_set_folder,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_SET_NTF_REG,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_set_ntf_reg,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_GET_FOLDER_LISTING,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_get_folder_listing,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_GET_MSG_LISTING,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_get_msg_listing,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_GET_MSG,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_get_msg,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_SET_MSG_STATUS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_set_msg_status,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_PUSH_MSG,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_push_msg,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_UPDATE_INBOX,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_update_inbox,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_GET_MAS_INST_INFO,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_get_mas_inst_info,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_SET_OWNER_STATUS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_set_owner_status,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_GET_OWNER_STATUS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_get_owner_status,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_GET_CONVO_LISTING,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_get_convo_listing,
	},
	{
		.opcode = BTP_MAP_MSE_MAS_SET_NTF_FILTER,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mas_set_ntf_filter,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_RFCOMM_CONNECT,
		.expect_len = sizeof(struct btp_map_mse_mns_rfcomm_connect_cmd),
		.func = mse_mns_rfcomm_connect,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_RFCOMM_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mse_mns_rfcomm_disconnect_cmd),
		.func = mse_mns_rfcomm_disconnect,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_L2CAP_CONNECT,
		.expect_len = sizeof(struct btp_map_mse_mns_l2cap_connect_cmd),
		.func = mse_mns_l2cap_connect,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_L2CAP_DISCONNECT,
		.expect_len = sizeof(struct btp_map_mse_mns_l2cap_disconnect_cmd),
		.func = mse_mns_l2cap_disconnect,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_CONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mns_connect,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_DISCONNECT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mns_disconnect,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_ABORT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mns_abort,
	},
	{
		.opcode = BTP_MAP_MSE_MNS_SEND_EVENT,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = mse_mns_send_event,
	},
};

uint8_t tester_init_map(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_MAP, handlers,
					 ARRAY_SIZE(handlers));

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_map(void)
{
	return BTP_STATUS_SUCCESS;
}
