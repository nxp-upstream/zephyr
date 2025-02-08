/** @file
 * @brief Bluetooth SPP shell module
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
#include <zephyr/bluetooth/classic/spp.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>

#include <zephyr/shell/shell.h>

#include "host/shell/bt.h"
#include "common/bt_shell_private.h"

#define HELP_NONE "[none]"

static struct bt_sdp_attribute spp_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 12),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 5),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_RFCOMM)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
				BT_SDP_ARRAY_8(BT_RFCOMM_CHAN_SPP)
			},
			)
		},
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(0x0102)
			},
			)
		},
		)
	),
	BT_SDP_SERVICE_NAME("Serial Port"),
};

static struct bt_sdp_record spp_rec = BT_SDP_RECORD(spp_attrs);

static struct bt_spp_server spp_server;

static int cmd_register(const struct shell *sh, size_t argc, char *argv[])
{
	int ret;

    ret = bt_spp_server_register(&spp_server, BT_RFCOMM_CHAN_SPP, &spp_rec);
	if (ret < 0) {
		shell_error(sh, "Unable to register channel %x", ret);
		return -ENOEXEC;
	} else {
		shell_print(sh, "RFCOMM channel %u registered",
			    spp_server.rfcomm_server.channel);
	}
    return ret;
}

static int cmd_connect(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t channel;
	int err;

	if (!default_conn) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	channel = strtoul(argv[1], NULL, 16);

	err = bt_spp_connect(default_conn, channel);
	if (err < 0) {
		shell_error(sh, "Unable to connect to channel %d (err %u)",
			    channel, err);
	} else {
		shell_print(sh, "RFCOMM connection pending");
	}

	return err;
}

static int cmd_send(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t buf_data[DATA_MTU] = { [0 ... (DATA_MTU - 1)] = 0xff };
	int ret, count = 1;

	if (argc > 1) {
		count = strtoul(argv[1], NULL, 10);
	}

	while (count--) {
		ret = bt_spp_send(buf_data);
		if (ret < 0) {
			shell_error(sh, "Unable to send: %d", -ret);
			return -ENOEXEC;
		}
	}

	return 0;
}

static int cmd_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
	bt_spp_disconnect(BT_RFCOMM_CHAN_SPP);
}

SHELL_STATIC_SUBCMD_SET_CREATE(spp_cmds,
	SHELL_CMD_ARG(register, NULL, HELP_NONE, cmd_register, 1, 0),
	SHELL_CMD_ARG(connect, NULL, "<channel>", cmd_connect, 2, 0),
	SHELL_CMD_ARG(disconnect, NULL, HELP_NONE, cmd_disconnect, 1, 0),
	SHELL_CMD_ARG(send, NULL, "<number of packets>", cmd_send, 2, 0),
	SHELL_SUBCMD_SET_END
);

static int cmd_spp(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		/* shell returns 1 when help is printed */
		return 1;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -ENOEXEC;
}

SHELL_CMD_ARG_REGISTER(spp, &spp_cmds, "Bluetooth SPP shell commands",
		       cmd_spp, 1, 1);