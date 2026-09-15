/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/classic/bip.h>
#include <zephyr/bluetooth/classic/obex.h>

#include "host/shell/bt.h"
#include "common/bt_shell_private.h"

struct bt_bip_app {
	struct bt_bip_client client;
	struct bt_bip bip;
	struct bt_bip_server server;
	struct bt_conn *conn;
	struct net_buf *tx_buf;
	uint16_t client_mopl;
	uint16_t server_mopl;
	uint32_t conn_id;
};

extern struct bt_bip_app bip_app;
extern struct bt_bip_server_cb bip_server_cb;
extern struct bt_bip_client_cb bip_client_cb;

static struct bt_bip_server secondary_server;
static struct bt_bip_client secondary_client;

#define IMAGE_PARTIAL_FILE_NAME \
	"\x00\x31\x00\x30\x00\x30\x00\x30\x00\x30\x00\x30\x00\x31\x00\x00"

static int cmd_sec_set_feats_funcs(const struct shell *sh, size_t argc, char *argv[])
{
	int err = 0;
	uint16_t features;
	uint32_t functions;

	features = shell_strtoul(argv[1], 0, &err);
	if (err != 0) {
		shell_error(sh, "Invalid features %s", argv[1]);
		return -ENOEXEC;
	}

	functions = shell_strtoul(argv[2], 0, &err);
	if (err != 0) {
		shell_error(sh, "Invalid functions %s", argv[2]);
		return -ENOEXEC;
	}

	bip_app.bip._supp_feats = features;
	bip_app.bip._supp_funcs = functions;

	return 0;
}

static int cmd_sec_reg(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	uint8_t type;

	err = 0;
	type = shell_strtoul(argv[1], 0, &err);
	if (err != 0) {
		shell_error(sh, "Invalid type %s", argv[1]);
		return -ENOEXEC;
	}

	err = bt_bip_secondary_server_register(&bip_app.bip, &secondary_server,
					       type, NULL, &bip_server_cb,
					       &bip_app.client);
	if (err != 0) {
		shell_error(sh, "Failed to register secondary server %d", err);
		return err;
	}

	return 0;
}

static int cmd_sec_unreg(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	err = bt_bip_server_unregister(&secondary_server);
	if (err != 0) {
		shell_error(sh, "Failed to unregister secondary server %d", err);
		return err;
	}

	return 0;
}

static int cmd_sec_conn(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	uint8_t type;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	if (bip_app.conn == NULL) {
		shell_error(sh, "No bip transport connection");
		return -ENOEXEC;
	}

	err = 0;
	type = shell_strtoul(argv[1], 0, &err);
	if (err != 0) {
		shell_error(sh, "Invalid type %s", argv[1]);
		return -ENOEXEC;
	}

	err = bt_bip_secondary_client_connect(&bip_app.bip, &secondary_client,
					      type, &bip_client_cb,
					      bip_app.tx_buf, &bip_app.server);
	if (err != 0) {
		shell_error(sh, "Fail to send secondary conn req %d", err);
	} else {
		bip_app.tx_buf = NULL;
	}

	return err;
}

static int cmd_sec_server_conn(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t rsp_code;
	const char *rsp;
	int err;

	rsp = argv[1];
	if (!strcmp(rsp, "success")) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else if (!strcmp(rsp, "error")) {
		if (argc < 3) {
			shell_error(sh, "[rsp_code] is needed");
			return -ENOEXEC;
		}
		rsp_code = (uint8_t)strtoul(argv[2], NULL, 16);
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	err = bt_bip_connect_rsp(&secondary_server, rsp_code, bip_app.tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send sec conn rsp %d", err);
	} else {
		bip_app.tx_buf = NULL;
	}
	return err;
}

static int cmd_sec_server_abort(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t rsp_code;
	const char *rsp;
	int err;

	rsp = argv[1];
	if (!strcmp(rsp, "success")) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else if (!strcmp(rsp, "error")) {
		if (argc < 3) {
			shell_error(sh, "[rsp_code] is needed");
			return -ENOEXEC;
		}
		rsp_code = (uint8_t)strtoul(argv[2], NULL, 16);
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	err = bt_bip_abort_rsp(&secondary_server, rsp_code, bip_app.tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send sec abort rsp %d", err);
	} else {
		bip_app.tx_buf = NULL;
	}
	return err;
}

static int cmd_sec_server_get_partial_image(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t rsp_code;
	const char *rsp;
	int err;

	rsp = argv[1];
	if (!strcmp(rsp, "error")) {
		if (argc < 3) {
			shell_error(sh, "[rsp_code] is needed");
			return -ENOEXEC;
		}
		rsp_code = (uint8_t)strtoul(argv[2], NULL, 16);
	} else if (!strcmp(rsp, "noerror")) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	err = bt_bip_get_partial_image_rsp(&secondary_server, rsp_code, bip_app.tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send sec get_partial_image rsp %d", err);
	} else {
		bip_app.tx_buf = NULL;
	}
	return err;
}

static int cmd_sec_get_partial_image(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	uint32_t partial_file_len = 0xffffffff;
	uint32_t partial_file_start_offset = 0;
	struct bt_obex_tlv appl_params[] = {
		{BT_BIP_APPL_PARAM_TAG_ID_PARTIAL_FILE_LEN,
		 sizeof(partial_file_len),
		 (const uint8_t *)&partial_file_len},
		{BT_BIP_APPL_PARAM_TAG_ID_PARTIAL_FILE_START_OFFSET,
		 sizeof(partial_file_start_offset),
		 (const uint8_t *)&partial_file_start_offset},
	};

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	if (bip_app.conn == NULL) {
		shell_error(sh, "No bip transport connection");
		return -ENOEXEC;
	}

	if (bip_app.tx_buf == NULL) {
		shell_error(sh, "No tx buffer, call 'bip alloc-buf' first");
		return -ENOBUFS;
	}

	err = bt_obex_add_header_conn_id(bip_app.tx_buf, bip_app.conn_id);
	if (err != 0) {
		shell_error(sh, "Fail to add conn id header %d", err);
		return err;
	}

	err = bt_obex_add_header_type(bip_app.tx_buf,
				      sizeof(BT_BIP_HDR_TYPE_GET_PARTIAL_IMAGE),
				      BT_BIP_HDR_TYPE_GET_PARTIAL_IMAGE);
	if (err != 0) {
		shell_error(sh, "Fail to add type header %d", err);
		return err;
	}

	err = bt_obex_add_header_name(bip_app.tx_buf,
				      sizeof(IMAGE_PARTIAL_FILE_NAME) - 1,
				      IMAGE_PARTIAL_FILE_NAME);
	if (err != 0) {
		shell_error(sh, "Fail to add name header %d", err);
		return err;
	}

	err = bt_obex_add_header_app_param(bip_app.tx_buf,
					   ARRAY_SIZE(appl_params),
					   appl_params);
	if (err != 0) {
		shell_error(sh, "Fail to add app param header %d", err);
		return err;
	}

	err = bt_bip_get_partial_image(&secondary_client, true, bip_app.tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send sec get_partial_image req %d", err);
	} else {
		bip_app.tx_buf = NULL;
	}
	return err;
}

static int cmd_sec_abort(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	if (bip_app.conn == NULL) {
		shell_error(sh, "No bip transport connection");
		return -ENOEXEC;
	}

	err = bt_bip_abort(&secondary_client, bip_app.tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send sec abort req %d", err);
	} else {
		bip_app.tx_buf = NULL;
	}
	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(bip_client_cmds,
		SHELL_CMD_ARG(sec_set_feats_funcs, NULL,
			      "<features> <functions>",
			      cmd_sec_set_feats_funcs, 3, 0),
		SHELL_CMD_ARG(sec_reg, NULL, "<type> Register secondary server",
			      cmd_sec_reg, 2, 0),
		SHELL_CMD_ARG(sec_unreg, NULL, "Unregister secondary server",
			      cmd_sec_unreg, 1, 0),
		SHELL_CMD_ARG(sec_conn, NULL, "<type> Secondary client connect",
			      cmd_sec_conn, 2, 0),
		SHELL_CMD_ARG(sec_server_conn, NULL, "<success|error> [rsp_code]",
			      cmd_sec_server_conn, 2, 1),
		SHELL_CMD_ARG(sec_server_abort, NULL, "<success|error> [rsp_code]",
			      cmd_sec_server_abort, 2, 1),
		SHELL_CMD_ARG(sec_server_get_partial_image, NULL,
			      "<noerror|error> [rsp_code]",
			      cmd_sec_server_get_partial_image, 2, 1),
		SHELL_CMD_ARG(sec_get_partial_image, NULL,
			      "Secondary client get_partial_image",
			      cmd_sec_get_partial_image, 1, 0),
		SHELL_CMD_ARG(sec_abort, NULL, "Secondary client abort",
			      cmd_sec_abort, 1, 0),
		SHELL_SUBCMD_SET_END
);

static int cmd_bip_client(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return 1;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -ENOEXEC;
}

SHELL_CMD_ARG_REGISTER(bip_client_test, &bip_client_cmds, "Bluetooth test bip client test sh commands",
		       cmd_bip_client, 1, 1);
