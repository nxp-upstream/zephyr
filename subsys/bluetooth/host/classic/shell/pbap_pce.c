/** @file
 * @brief Bluetooth PBAP shell module
 *
 * Provide some Bluetooth shell commands that can be useful to applications.
 */
/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/types.h>
#include <stdlib.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/pbap.h>
#include <zephyr/shell/shell.h>

#include "host/shell/bt.h"
#include "common/bt_shell_private.h"

struct bt_pbap_pce_app {
	struct bt_pbap_pce pbap_pce;
	struct net_buf *tx_buf;
};

static struct bt_pbap_pce_app pbap_pce_app;

#define PBAP_MOPL CONFIG_BT_GOEP_RFCOMM_MTU

NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_MAX_CONN, BT_RFCOMM_BUF_SIZE(PBAP_MOPL),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
struct pbap_hdr
{
	uint8_t *value;
	uint16_t length;
};
static void pbap_connected(struct bt_pbap_pce *pbap, uint16_t mpl){
    bt_shell_print("pbap connect success %d", mpl);
}

static void pbap_disconnected(struct bt_pbap_pce *pbap, uint8_t rsp_code){
	if (rsp_code == BT_PBAP_RSP_CODE_OK){
		bt_shell_print("pbap disconnect success %d", rsp_code);
	}
	else{
		bt_shell_print("pbap disconnect fail %d", rsp_code);
	}
}

void pbap_pull_phonebook(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
	int err;
	struct pbap_hdr body;

	bt_shell_print("pbap_pull_phonebook");
	if (rsp_code == BT_PBAP_RSP_CODE_CONTINUE){
		err = bt_pbap_pce_get_body(buf, &body.length, &body.value);
		if (err){
			bt_shell_print("Fail to get body or no body %d", err);
		}

		// pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
		// if (!pbap_pce_app.tx_buf){
		// 	bt_shell_print("Fail to allocate tx buf");
		// 	return;
		// }

		// err = bt_pbap_pce_pull_phonebook_create_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, NULL, false);
		// if (err){
		// 	net_buf_unref(pbap_pce_app.tx_buf);
		// 	bt_shell_print("Fail create pull phonebook cmd  %d", err);
		// 	return;
		// }

		// err = bt_pbap_pce_send_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf);
		// if (err){
		// 	net_buf_unref(pbap_pce_app.tx_buf);
		// 	bt_shell_print("Fail to send command %d",err);
		// }

	}else if(rsp_code == BT_PBAP_RSP_CODE_SUCCESS){
		err = bt_pbap_pce_get_end_body(buf, &body.length, &body.value);
		if (err){
			bt_shell_print("Fail to get body or no body %d", err);
		}
	}
	net_buf_unref(buf);
	// if (body.length && body.value){
	
	// 	printk("\n=========body=========\n");
	// 	// printk("%s\n", body.value);
	// 	printk("=========body=========\n");
	// }
	return;
}

void pbap_pull_vcardlisting(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
	int err;
	struct pbap_hdr body;
	bt_shell_print("pbap_pull_vcardlisting callback");

	if (rsp_code == BT_PBAP_RSP_CODE_CONTINUE){
		err = bt_pbap_pce_get_body(buf, &body.length, &body.value);
		if (err){
			bt_shell_print("Fail to get body or no body %d", err);
		}

		// pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
		// if (!pbap_pce_app.tx_buf){
		// 	bt_shell_print("Fail to allocate tx buf");
		// 	return;
		// }
		// err = bt_pbap_pce_pull_vcardlisting_create_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, NULL, false);
		// if (err){
		// 	net_buf_unref(pbap_pce_app.tx_buf);
		// 	bt_shell_print("Fail create pull vcardlisting cmd  %d", err);
		// 	return;
		// }

		// err = bt_pbap_pce_send_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf);
		// if (err){
		// 	net_buf_unref(pbap_pce_app.tx_buf);
		// 	bt_shell_print("Fail to send command %d",err);
		// }

	}else if(rsp_code == BT_PBAP_RSP_CODE_SUCCESS){
		err = bt_pbap_pce_get_end_body(buf, &body.length, &body.value);
		if (err){
			bt_shell_print("Fail to get body or no body %d", err);
		}
	}
	net_buf_unref(buf);
	// if (body.length && body.value){
	
	// 	printk("\n=========body=========\n");
	// 	// printk("%s\n", body.value);
	// 	printk("=========body=========\n");
	// }
	return;
}

void pbap_pull_vcardentry(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
	int err;
	struct pbap_hdr body;
	bt_shell_print("pbap_pull_vcardentry callback");
	if (rsp_code == BT_PBAP_RSP_CODE_CONTINUE){
		err = bt_pbap_pce_get_body(buf, &body.length, &body.value);
		if (err){
			bt_shell_print("Fail to get body or no body %d", err);
		}

		// pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
		// if (!pbap_pce_app.tx_buf){
		// 	bt_shell_print("Fail to allocate tx buf");
		// 	return;
		// }

		// err = bt_pbap_pce_pull_vcardentry_create_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, NULL, false);
		// if (err){
		// 	net_buf_unref(pbap_pce_app.tx_buf);
		// 	bt_shell_print("Fail create pull vcardentry cmd  %d", err);
		// 	return;
		// }

		// err = bt_pbap_pce_send_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf);
		// if (err){
		// 	net_buf_unref(pbap_pce_app.tx_buf);
		// 	bt_shell_print("Fail to send command %d",err);
		// }

	}else if(rsp_code == BT_PBAP_RSP_CODE_SUCCESS){
		err = bt_pbap_pce_get_end_body(buf, &body.length, &body.value);
		if (err){
			bt_shell_print("Fail to get body or no body %d", err);
		}
	}
	net_buf_unref(buf);
	// if (body.length && body.value){
	
	// 	printk("\n=========body=========\n");
	// 	// printk("%s\n", body.value);
	// 	printk("=========body=========\n");
	// }
}

void pbap_set_path(struct bt_pbap_pce *pbap, uint8_t rsp_code)
{
	if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS){
		bt_shell_print("set path success.");
	}else{
		bt_shell_print("set path fail.");
	}
}


struct bt_pbap_pce_cb cb = {
    .connect = pbap_connected,
	.disconnect = pbap_disconnected,
	.pull_phonebook = pbap_pull_phonebook,
	.pull_vcardlisting = pbap_pull_vcardlisting,
	.pull_vcardentry = pbap_pull_vcardentry,
	.set_path = pbap_set_path,
};

static int cmd_register(const struct shell *sh, size_t argc, char *argv[])
{

    return bt_pbap_pce_register(&cb);

}

static int cmd_connect_rfcomm(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t channel = (uint8_t)strtoul(argv[1], NULL, 16);
	pbap_pce_app.pbap_pce.mpl = 600;
	pbap_pce_app.pbap_pce.peer_feature = 0x3FF;
	pbap_pce_app.pbap_pce.pwd = '0000';
    return bt_pbap_pce_rfcomm_connect(default_conn, channel, &pbap_pce_app.pbap_pce);
}

static int cmd_connect_l2cap(const struct shell *sh, size_t argc, char *argv[])
{
	uint16_t psm = (uint8_t)strtoul(argv[1], NULL, 16);
	pbap_pce_app.pbap_pce.mpl = 600;
    return bt_pbap_pce_rfcomm_connect(default_conn, psm, &pbap_pce_app.pbap_pce);
}

static int cmd_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
	return bt_pbap_pce_disconnect(&pbap_pce_app.pbap_pce);
}

static int cmd_pull_pb(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	char *name = argv[1];

	pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
	if (!pbap_pce_app.tx_buf){
		bt_shell_print("Fail to allocate tx buf");
		return -EINVAL;
	}

	err = bt_pbap_pce_pull_phonebook_create_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, name, false);
	if (err){
		bt_shell_print("Fail to create pull phonebook cmd %d", err);
		net_buf_unref(pbap_pce_app.tx_buf);
	}
	return err;
}

static int cmd_pull_vcardlisting(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	char *name = argv[1];

	pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
	if (!pbap_pce_app.tx_buf){
		bt_shell_print("Fail to allocate tx buf");
		return -EINVAL;
	}

	err = bt_pbap_pce_pull_vcardlisting_create_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, name, false);
	if (err){
		bt_shell_print("Fail to create pull vcardlisting cmd %d", err);
		net_buf_unref(pbap_pce_app.tx_buf);
	}
	return err;
}

static int cmd_pull_vcardentry(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	char *name = argv[1];

	pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
	if (!pbap_pce_app.tx_buf){
		bt_shell_print("Fail to allocate tx buf");
		return -EINVAL;
	}

	err = bt_pbap_pce_pull_vcardentry_create_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, name, false);
	if (err){
		bt_shell_print("Fail to create pull vcardlistentry cmd %d", err);
		net_buf_unref(pbap_pce_app.tx_buf);
	}
	return err;
}

static int cmd_set_path(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	char *name = argv[1];

	pbap_pce_app.tx_buf  = bt_pbap_create_pdu(&pbap_pce_app.pbap_pce, &tx_pool);
	if (!pbap_pce_app.tx_buf){
		bt_shell_print("Fail to allocate tx buf");
		return -EINVAL;
	}

	err = bt_pbap_pce_set_path(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf, name);
	if (err){
		bt_shell_print("Fail to send set path cmd %d", err);
		net_buf_unref(pbap_pce_app.tx_buf);
	}
	return err; 
}

static int cmd_cmd_send(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	err = bt_pbap_pce_send_cmd(&pbap_pce_app.pbap_pce, pbap_pce_app.tx_buf);
	if (err){
		net_buf_unref(pbap_pce_app.tx_buf);
		bt_shell_print("Fail to send command %d",err);
	}
	return err;
}


static int cmd_common(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -ENOEXEC;
}

SHELL_STATIC_SUBCMD_SET_CREATE(pbap_cmds,
	SHELL_CMD_ARG(register, NULL, "", cmd_register, 1, 0),
	SHELL_CMD_ARG(connect-rfcomm, NULL, "<channel>", cmd_connect_rfcomm, 2, 0),
	SHELL_CMD_ARG(connect-l2cap, NULL, "<channel>", cmd_connect_l2cap, 2, 0),
	SHELL_CMD_ARG(disconnect, NULL, "<channel>", cmd_disconnect, 1, 0),
	SHELL_CMD_ARG(pull_pb_create, NULL, "<name>  <srmp>", cmd_pull_pb, 2, 0),
	SHELL_CMD_ARG(pull_vcardlisting_create, NULL, "<name>  <srmp>", cmd_pull_vcardlisting, 2, 0),
	SHELL_CMD_ARG(pull_vcardentry_create, NULL, "<name>  <srmp>", cmd_pull_vcardentry, 2, 0),
	SHELL_CMD_ARG(setpath, NULL, "<name>", cmd_set_path, 2, 0),
	SHELL_CMD_ARG(cmd_send, NULL, "<NULL>", cmd_cmd_send, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(pbap, &pbap_cmds, "Bluetooth pbap shell commands", cmd_common, 1, 1);