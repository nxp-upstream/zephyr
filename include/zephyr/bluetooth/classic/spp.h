/** @file
 *  @brief Bluetooth SPP handling
 */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_SPP_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_SPP_H_

/**
 * @brief SPP
 * @defgroup bt_spp SPP
 * @ingroup bluetooth
 * @{
 */

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_MTU 48

struct bt_spp_server
{
    /** The rfcomm server */
    struct bt_rfcomm_server rfcomm_server;
    
    struct bt_spp_server *_next;
};

int bt_spp_server_register(struct bt_spp_server *server, uint8_t channel, struct bt_sdp_record *spp_rec);
int bt_spp_connect(struct bt_conn *conn, uint8_t channel);
int bt_spp_send(uint8_t *data);
int bt_spp_disconnect(uint8_t channel);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_SPP_H_ */
