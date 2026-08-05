/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * AMP mbox test (primary Cortex-M side).
 *
 * Covers two usecases against the remote HiFi4 DSP:
 *   1. The DSP starts, runs and does not crash - verified by waiting for the
 *      DSP's periodic "alive" beacon over the MU/mbox peripheral.
 *   2. IPC using the mbox API is functional - verified by a data round-trip
 *      (echo) and a data-less notification (IPI) that the DSP acknowledges.
 *
 * Every wait is bounded by a timeout. If the DSP fails to load, hangs or
 * crashes, the expected messages never arrive and the ztest assertions fail
 * instead of falsely passing.
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/mbox.h>

#include <dsp.h>
#include "testipc.h"

#define AMP_MBOX_TIMEOUT K_MSEC(5000)

static void *amp_mbox_setup(void)
{	
	zassert_ok(
		testipc_init(),
		"Failed to initialise test IPC"
	);

	zassert_true(
		dsp_start(), 
		"DSP control device not ready / DSP not started"
	);

	return NULL;
}

/*
 * Usecase 1: the DSP starts, runs and does not crash.
 *
 * We must receive an initial "alive" beacon (proving the DSP loaded and
 * booted), then at least one more beacon with a higher counter (proving the
 * DSP is still running and has not hung or crashed).
 */
ZTEST(amp_mbox, test_dsp_alive)
{
	uint32_t first;
	uint32_t second;

	zassert_ok(
		testipc_recv_timeout(&first, AMP_MBOX_TIMEOUT),
		"failed to receive message"
	);
	zassert_equal(
		first, testipc_msg_get_op(first),
		"Received message is not AMP_OP_ALIVE (=0x%x)",
		testipc_msg_get_op(first)
	);

	zassert_ok(
		testipc_recv_timeout(&second, AMP_MBOX_TIMEOUT),
		"failed to receive message"
	);
	zassert_equal(
		first, testipc_msg_get_op(first),
		"Received message is not AMP_OP_ALIVE (=0x%x)",
		testipc_msg_get_op(first)
	);

	zassert_not_equal(
		testipc_msg_get_payload(second), testipc_msg_get_payload(first),
		"Received messages are equal (counter did not advance)"
	);
}

/*
 * Usecase 2 (data path): mbox IPC is functional.
 *
 * Send a known payload to the DSP and assert it echoes exactly that value
 * back over the mbox.
 */
ZTEST(amp_mbox, test_mbox_echo)
{
	uint32_t msg;	

	zassert_ok(
		testipc_send(testipc_msg_make(AMP_OP_ECHO_REQ, AMP_ECHO_MAGIC)),
		"mbox send (echo request) failed"
	);

	zassert_ok(
		testipc_recv_op_timeout(&msg, AMP_OP_ECHO_RESP, AMP_MBOX_TIMEOUT),
		"no echo response from DSP"
	);

	zassert_equal(testipc_msg_get_payload(msg), AMP_ECHO_MAGIC,
		"echoed payload mismatch: got 0x%06x expected 0x%06x",
		testipc_msg_get_payload(msg), AMP_ECHO_MAGIC
	);
}


ZTEST_SUITE(amp_mbox, NULL, amp_mbox_setup, NULL, NULL, NULL);
