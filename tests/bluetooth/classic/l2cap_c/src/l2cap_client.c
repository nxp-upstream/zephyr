/* sdp_client.c - Bluetooth classic SDP client smoke test */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>

#include <zephyr/shell/shell.h>

#include "host/shell/bt.h"
#include "common/bt_shell_private.h"

#define DATA_BREDR_MTU 48

NET_BUF_POOL_FIXED_DEFINE(data_tx_pool, 1, BT_L2CAP_SDU_BUF_SIZE(DATA_BREDR_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
NET_BUF_POOL_FIXED_DEFINE(data_rx_pool, 1, DATA_BREDR_MTU, 8, NULL);

struct l2cap_br_chan {
	struct bt_l2cap_br_chan chan;
#if defined(CONFIG_BT_L2CAP_RET_FC)
	struct k_fifo l2cap_recv_fifo;
	bool hold_credit;
#endif /* CONFIG_BT_L2CAP_RET_FC */
};

static int l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	struct l2cap_br_chan *br_chan = CONTAINER_OF(chan, struct l2cap_br_chan, chan.chan);

	bt_shell_print("Incoming data channel %p len %u", chan, buf->len);

	if (buf->len) {
		bt_shell_hexdump(buf->data, buf->len);
	}

#if defined(CONFIG_BT_L2CAP_RET_FC)
	if (br_chan->hold_credit) {
		k_fifo_put(&br_chan->l2cap_recv_fifo, buf);
		return -EINPROGRESS;
	}
#endif /* CONFIG_BT_L2CAP_RET_FC */
	(void)br_chan;

	return 0;
}

static struct net_buf *l2cap_alloc_buf(struct bt_l2cap_chan *chan)
{
	bt_shell_print("Channel %p requires buffer", chan);

	return net_buf_alloc(&data_rx_pool, K_NO_WAIT);
}

static void l2cap_connected(struct bt_l2cap_chan *chan)
{
	struct l2cap_br_chan *br_chan = CONTAINER_OF(chan, struct l2cap_br_chan, chan.chan);

	bt_shell_print("Channel connected");

#if defined(CONFIG_BT_L2CAP_RET_FC)
	switch (br_chan->chan.rx.mode) {
	case BT_L2CAP_BR_LINK_MODE_BASIC:
		bt_shell_print("It is basic mode");
		if (br_chan->hold_credit) {
			br_chan->hold_credit = false;
			bt_shell_warn("hold_credit is unsupported in basic mode");
		}
		break;
	case BT_L2CAP_BR_LINK_MODE_RET:
		bt_shell_print("It is retransmission mode");
		break;
	case BT_L2CAP_BR_LINK_MODE_FC:
		bt_shell_print("It is flow control mode");
		break;
	case BT_L2CAP_BR_LINK_MODE_ERET:
		bt_shell_print("It is enhance retransmission mode");
		break;
	case BT_L2CAP_BR_LINK_MODE_STREAM:
		bt_shell_print("It is streaming mode");
		break;
	default:
		bt_shell_error("It is unknown mode");
		break;
	}
#endif /* CONFIG_BT_L2CAP_RET_FC */
	(void)br_chan;
}

static void l2cap_disconnected(struct bt_l2cap_chan *chan)
{
#if defined(CONFIG_BT_L2CAP_RET_FC)
	struct net_buf *buf;
	struct l2cap_br_chan *br_chan = CONTAINER_OF(chan, struct l2cap_br_chan, chan.chan);
#endif /* CONFIG_BT_L2CAP_RET_FC */

	bt_shell_print("Channel disconnected");

#if defined(CONFIG_BT_L2CAP_RET_FC)
	do {
		buf = k_fifo_get(&br_chan->l2cap_recv_fifo, K_NO_WAIT);
		if (buf != NULL) {
			net_buf_unref(buf);
		}
	} while (buf != NULL);
#endif /* CONFIG_BT_L2CAP_RET_FC */
}

static const struct bt_l2cap_chan_ops l2cap_ops = {
	.alloc_buf = l2cap_alloc_buf,
	.recv = l2cap_recv,
	.connected = l2cap_connected,
	.disconnected = l2cap_disconnected,
};

static struct l2cap_br_chan l2cap_chan = {
	.chan = {
		.chan.ops = &l2cap_ops,
		/* Set for now min. MTU */
		.rx.mtu = DATA_BREDR_MTU,
	},
#if defined(CONFIG_BT_L2CAP_RET_FC)
	.l2cap_recv_fifo = Z_FIFO_INITIALIZER(l2cap_chan.l2cap_recv_fifo),
#endif /* CONFIG_BT_L2CAP_RET_FC */
};

static int cmd_connect(const struct shell *sh, size_t argc, char *argv[])
{
	uint16_t psm;
	struct bt_conn_info info;
	int err;

	if (!default_conn) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	if (l2cap_chan.chan.chan.conn) {
		bt_shell_error("No channels available");
		return -ENOMEM;
	}

	err = bt_conn_get_info(default_conn, &info);
	if ((err < 0) || (info.type != BT_CONN_TYPE_BR)) {
		shell_error(sh, "Invalid conn type");
		return -ENOEXEC;
	}

	psm = strtoul(argv[1], NULL, 16);

#if defined(CONFIG_BT_L2CAP_RET_FC)
	if (!strcmp(argv[2], "base")) {
		l2cap_chan.chan.rx.mode = BT_L2CAP_BR_LINK_MODE_BASIC;
	} else if (!strcmp(argv[2], "ret")) {
		l2cap_chan.chan.rx.mode = BT_L2CAP_BR_LINK_MODE_RET;
		l2cap_chan.chan.rx.max_transmit = 3;
	} else if (!strcmp(argv[2], "fc")) {
		l2cap_chan.chan.rx.mode = BT_L2CAP_BR_LINK_MODE_FC;
		l2cap_chan.chan.rx.max_transmit = 3;
	} else if (!strcmp(argv[2], "eret")) {
		l2cap_chan.chan.rx.mode = BT_L2CAP_BR_LINK_MODE_ERET;
		l2cap_chan.chan.rx.max_transmit = 3;
	} else if (!strcmp(argv[2], "stream")) {
		l2cap_chan.chan.rx.mode = BT_L2CAP_BR_LINK_MODE_STREAM;
		l2cap_chan.chan.rx.max_transmit = 0;
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	l2cap_chan.hold_credit = false;
	l2cap_chan.chan.rx.optional = false;
	l2cap_chan.chan.rx.extended_control = false;

	for (size_t index = 3; index < argc; index++) {
		if (!strcmp(argv[index], "hold_credit")) {
			l2cap_chan.hold_credit = true;
		} else if (!strcmp(argv[index], "mode_optional")) {
			l2cap_chan.chan.rx.optional = true;
		} else if (!strcmp(argv[index], "extended_control")) {
			l2cap_chan.chan.rx.extended_control = true;
		} else if (!strcmp(argv[index], "sec")){
			l2cap_chan.chan.required_sec_level = strtoul(argv[++index], NULL, 16);
		}else{
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}
	}

	if ((l2cap_chan.chan.rx.extended_control) &&
	    ((l2cap_chan.chan.rx.mode != BT_L2CAP_BR_LINK_MODE_ERET) &&
	     (l2cap_chan.chan.rx.mode != BT_L2CAP_BR_LINK_MODE_STREAM))) {
		shell_error(sh, "[extended_control] only supports mode eret and stream");
		return -ENOEXEC;
	}

	if (l2cap_chan.hold_credit && (l2cap_chan.chan.rx.mode == BT_L2CAP_BR_LINK_MODE_BASIC)) {
		shell_error(sh, "[hold_credit] cannot support basic mode");
		return -ENOEXEC;
	}

	l2cap_chan.chan.rx.max_window = CONFIG_BT_L2CAP_MAX_WINDOW_SIZE;
#endif /* CONFIG_BT_L2CAP_RET_FC */

	err = bt_l2cap_chan_connect(default_conn, &l2cap_chan.chan.chan, psm);
	if (err < 0) {
		shell_error(sh, "Unable to connect to psm %u (err %d)", psm, err);
	} else {
		shell_print(sh, "L2CAP connection pending");
	}

	return err;
}

static int cmd_l2cap_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	err = bt_l2cap_chan_disconnect(&l2cap_chan.chan.chan);
	if (err) {
		shell_error(sh, "Unable to disconnect: %u", -err);
	}

	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(l2cap_client_cmds,
	SHELL_CMD_ARG(connect, NULL, "<psm> <mode> [option]", cmd_connect, 2, 3),
	SHELL_CMD_ARG(disconnect, NULL, "", cmd_l2cap_disconnect, 1, 0),
	SHELL_SUBCMD_SET_END
);

static int cmd_default_handler(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_REGISTER(l2cap_client, &l2cap_client_cmds, "Bluetooth classic SDP client shell commands",
		   cmd_default_handler);
