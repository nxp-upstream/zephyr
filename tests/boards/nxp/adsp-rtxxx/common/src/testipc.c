/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Minimal request/response IPC over the NXP i.MX MU (via the Zephyr MBOX API).
 *
 * Design note - why there is no application-level acknowledgement:
 *
 * The MU DATA path is already reliable and flow controlled in hardware.
 * mbox_send_dt() (backed by MU_SendMsg) blocks until the transmit register is
 * empty, i.e. until the peer's Rx-full interrupt has consumed the previous
 * word, and every word delivered raises exactly one receive callback. Unlike
 * the MU general-interrupt "doorbell" (a single latching bit that can coalesce
 * and be lost), a DATA word can neither coalesce nor be dropped.
 *
 * This test protocol is strict request/response ping-pong: a side never issues
 * a new message until it has received the response to the previous one, so at
 * most one message is ever in flight per direction. That makes an explicit ack
 * unnecessary - the hardware back-pressure of MU_SendMsg is the flow control,
 * and a single rxmsg slot plus a binary semaphore is sufficient to hand a
 * received word to the waiting thread.
 *
 * An earlier revision layered an application ack (either a data-less doorbell
 * or an AMP_OP_IPI_ACK word) on top of a shared tx slot guarded by an
 * in-flight flag. That was both racy (the ack aliased across protocol phases
 * on the single MU register) and self-poisoning (any early return left the
 * in-flight flag set, wedging every later send with -EAGAIN). Removing the ack
 * removes that whole class of failure.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>

#include "testipc.h"

static const struct mbox_dt_spec mbox_tx = MBOX_DT_SPEC_GET(DT_NODELABEL(mailboxes), tx);
static const struct mbox_dt_spec mbox_rx = MBOX_DT_SPEC_GET(DT_NODELABEL(mailboxes), rx);

static struct k_sem sem_rx;

static uint32_t rxmsg;

static size_t txed;
static size_t rxed;

static void mbox_rx_cb(const struct device *dev, mbox_channel_id_t channel,
		       void *user_data, struct mbox_msg *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	/*
	 * Only DATA words are used by this protocol. A NULL (data-less
	 * doorbell) callback is not expected; ignore it defensively.
	 */
	if (data == NULL) {
		return;
	}

	rxmsg = *(const uint32_t *)data->data;
	k_sem_give(&sem_rx);
}

int testipc_init(void)
{
    int ret;

    if (!device_is_ready(mbox_tx.dev)) {
        return -ENODEV;
    }

    if (!device_is_ready(mbox_rx.dev)) {
        return -ENODEV;
    }

    k_sem_init(&sem_rx, 0, 1);

    ret = mbox_register_callback_dt(&mbox_rx, mbox_rx_cb, NULL);
    if (ret < 0) {
        return ret;
    }

    ret = mbox_set_enabled_dt(&mbox_rx, true);
    if (ret < 0) {
        return ret;
    }

    rxmsg = 0;

    txed = 0;
    rxed = 0;

    return 0;
}

int testipc_send_timeout(uint32_t msg, k_timeout_t timeout)
{
    int ret;

    /*
     * The timeout is not used: the MU DATA path is flow controlled in
     * hardware (mbox_send_dt blocks only until the previous word has been
     * consumed by the peer), and this protocol keeps at most one message in
     * flight per direction, so the transmit register is empty on entry and
     * the send returns immediately. Liveness of the peer is detected by the
     * receive-side timeout in testipc_recv_timeout().
     */
    ARG_UNUSED(timeout);

    /* Local buffer: mbox_send_dt() copies synchronously before returning. */
    uint32_t tx = msg;
    struct mbox_msg m = {.data = &tx, .size = sizeof(tx)};

    ret = mbox_send_dt(&mbox_tx, &m);
    if (ret < 0) {
        return ret;
    }

    txed++;

    return 0;
}

int testipc_send(uint32_t msg) 
{
    return testipc_send_timeout(msg, K_FOREVER);
}

int testipc_report_error(int retcode)
{
    return testipc_send(testipc_msg_make(AMP_OP_ERROR, retcode));
}

int testipc_recv(uint32_t *msg)
{
    return testipc_recv_timeout(msg, K_FOREVER);
}

int testipc_recv_timeout(uint32_t *msg, k_timeout_t timeout)
{
    int ret = k_sem_take(&sem_rx, timeout);
    if (ret < 0) {
        return ret;
    }

    *msg = rxmsg;
    rxmsg = 0;

    rxed++;

    return 0;
}

int testipc_recv_op_timeout(uint32_t *msg, uint8_t op, k_timeout_t timeout)
{
    int ret;

    while (true) {
        ret = testipc_recv_timeout(msg, timeout);
        if (ret < 0) {
            return ret;
        }

        if (testipc_msg_get_op(*msg) == op) {
            return 0;
        }
    }
}

int testipc_recv_op(uint32_t *msg, uint8_t op)
{
    return testipc_recv_op_timeout(msg, op, K_FOREVER);
}

bool testipc_has(void) 
{
    return k_sem_count_get(&sem_rx) == 1;
}

