/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * AMP mbox test (remote HiFi4 DSP side).
 *
 * On boot the DSP periodically emits an "alive" beacon over the MU/mbox so the
 * ARM core can confirm it started and keeps running. It also responds to echo
 * requests and data-less IPI notifications sent by the ARM core, closing the
 * loop for the mbox IPC usecase.
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/devicetree.h>

#include "testipc.h"

#define ALIVE_PERIOD_MS 250

int main(void)
{
	int ret;
	uint32_t beacon = 0;

    k_sleep(K_MSEC(500));
	printk("[DSP] Hello World! %s\n", CONFIG_BOARD_TARGET);
    k_sleep(K_MSEC(500));

	ret = testipc_init();
	if (ret < 0) {
		printk("[DSP] Failed to init IPC: %d\n", ret);
		return ret;
	}

	/* Periodically prove we are alive and running. */
	while (true) {
		if (testipc_has()) {
			uint32_t msg;
			ret = testipc_recv(&msg);
			if (ret < 0) {
				printk("[DSP] Failed to recv: %d\n", ret);
				return ret;
			}

			if (testipc_msg_get_op(msg) == AMP_OP_ECHO_REQ) {
				ret = testipc_send(testipc_msg_make(
					AMP_OP_ECHO_RESP,
					testipc_msg_get_payload(msg)
				));

				if (ret < 0) {
					printk("[DSP] Failed to send: %d\n", ret);
					return ret;
				}
			}

			k_sleep(K_MSEC(500));
		} else {
			ret = testipc_send(testipc_msg_make(AMP_OP_ALIVE, beacon));
			if (ret < 0) {
				printk("[DSP] Failed to send: %d\n", ret);
				return ret;
			}

			beacon = (beacon + 1);
			k_msleep(ALIVE_PERIOD_MS);
		}
	}

	return 0;
}
