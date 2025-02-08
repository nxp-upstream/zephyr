/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/conn.h>

#include "common/assert.h"

#include <zephyr/bluetooth/classic/spp.h>

#include "host/hci_core.h"
#include "host/conn_internal.h"
#include "l2cap_br_internal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_spp);

NET_BUF_POOL_FIXED_DEFINE(spp_pool, 1, DATA_MTU, CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct bt_spp_server *spp_servers;

static void spp_connected(struct bt_rfcomm_dlc *dlc)
{
	LOG_DBG("Dlc %p connected", dlc);
	printf("Dlc %p connected\n", dlc);
}

static void spp_disconnected(struct bt_rfcomm_dlc *dlc)
{
	LOG_DBG("Dlc %p disconnected", dlc);
	printf("Dlc %p disconnected\n", dlc);
}

static void spp_sent(struct bt_rfcomm_dlc *dlc, int err)
{
	return 0;
}

static void spp_recv(struct bt_rfcomm_dlc *dlc, struct net_buf *buf)
{
	uint32_t index;

	LOG_DBG("Incoming data dlc %p len %u", dlc, buf->len);

	printf("SPP received: ");
	for (index = 0; index < buf->len; index++)
    {
        printf("%c", buf->data[index]);
    }
}

static struct bt_rfcomm_dlc_ops rfcomm_ops = {
	.recv		= spp_recv,
	.connected	= spp_connected,
	.disconnected	= spp_disconnected,
};

static struct bt_rfcomm_dlc rfcomm_dlc = {
    .ops = &rfcomm_ops,
	.mtu = 30,
};

static int spp_accept(struct bt_conn *conn, struct bt_rfcomm_server *server,
			 struct bt_rfcomm_dlc **dlc)
{
	LOG_DBG("Incoming RFCOMM conn %p", conn);

	if (rfcomm_dlc.session) {
		LOG_WRN("No channels available");
		return -ENOMEM;
	}

	*dlc = &rfcomm_dlc;

	return 0;
}


int bt_spp_server_register(struct bt_spp_server *spp_server, uint8_t channel,
             struct bt_sdp_record *spp_rec)
{
    int ret;

    /* register rfcomm server */
    spp_server->rfcomm_server.channel = channel;
    spp_server->rfcomm_server.accept = spp_accept;

    ret = bt_rfcomm_server_register(&spp_server->rfcomm_server);
	if (ret < 0) {
		LOG_WRN("Unable to register channel %x", ret);
		spp_server->rfcomm_server.channel = 0U;
		return -ENOEXEC;
	}

    /* register sdp service */
    LOG_DBG("RFCOMM channel %u registered",
		spp_server->rfcomm_server.channel);
    bt_sdp_register_service(spp_rec);

    spp_server->_next = spp_servers;
	spp_servers = spp_server;

	return 0;
}

int bt_spp_connect(struct bt_conn *conn, uint8_t channel)
{
	int err;

	if (!conn) {
		LOG_WRN("Not connected");
		return -ENOEXEC;
	}

	err = bt_rfcomm_dlc_connect(conn, &rfcomm_dlc, channel);
	if (err < 0) {
		LOG_WRN("Unable to connect to channel %d (err %u)",
			    channel, err);
	} else {
		LOG_DBG("RFCOMM connection pending");
	}

	return err;
}

int bt_spp_disconnect(uint8_t channel)
{
	int err;

	err = bt_rfcomm_dlc_disconnect(&rfcomm_dlc);
	if (err) {
		LOG_WRN("Unable to disconnect: %u", -err);
	}

	return err;
}

int bt_spp_send(uint8_t *data)
{
	int ret, len;
	struct net_buf *buf;

	buf = bt_rfcomm_create_pdu(&spp_pool);
	/* Should reserve one byte in tail for FCS */
	len = MIN(rfcomm_dlc.mtu, net_buf_tailroom(buf) - 1);

	net_buf_add_mem(buf, data, len);
	ret = bt_rfcomm_dlc_send(&rfcomm_dlc, buf);
	if (ret < 0) {
		LOG_WRN("Unable to send: %d", -ret);
		net_buf_unref(buf);
		return -ENOEXEC;
	}

	return 0;
}