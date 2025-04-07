/* sdp_client.c - Bluetooth classic SDP client smoke test */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <errno.h>
#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>

#include <zephyr/shell/shell.h>

extern struct bt_conn *default_conn;

// SHELL_STATIC_SUBCMD_SET_CREATE(sdp_client_cmds,
// 	SHELL_CMD_ARG(ss_discovery, NULL, "<UUID>", cmd_ss_discovery, 2, 0),
// 	SHELL_CMD_ARG(sa_discovery, NULL, "<Service Record Handle>", cmd_sa_discovery, 2, 0),
// 	SHELL_CMD_ARG(ssa_discovery, NULL, "<UUID>", cmd_ssa_discovery, 2, 0),
// 	SHELL_SUBCMD_SET_END
// );

static int cmd_default_handler(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_REGISTER(smp_init, NULL, "Bluetooth classic SMP initiator shell commands",
		   cmd_default_handler);
