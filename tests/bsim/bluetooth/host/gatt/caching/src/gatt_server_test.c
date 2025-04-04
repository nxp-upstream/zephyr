/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "babblekit/testcase.h"
#include "babblekit/flags.h"
#include "babblekit/sync.h"
#include "common.h"

extern enum bst_result_t bst_result;

DEFINE_FLAG_STATIC(flag_is_connected);
DEFINE_FLAG_STATIC(flag_is_encrypted);

static struct bt_conn *g_conn;

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		TEST_FAIL("Failed to connect to %s (%u)", addr, err);

		return;
	}

	printk("Connected to %s\n", addr);

	g_conn = bt_conn_ref(conn);
	SET_FLAG(flag_is_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != g_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(g_conn);

	g_conn = NULL;
	UNSET_FLAG(flag_is_connected);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err security_err)
{
	if (security_err == BT_SECURITY_ERR_SUCCESS && level > BT_SECURITY_L1) {
		SET_FLAG(flag_is_encrypted);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

#define CHRC_SIZE 10
#define ARRAY_ITEM(i, _) i
static const uint8_t chrc_data[] = { LISTIFY(CHRC_SIZE, ARRAY_ITEM, (,)) }; /* 1, 2, 3 ... */

static ssize_t read_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	printk("Characteristic read\n");

	return bt_gatt_attr_read(conn, attr, buf, len, offset, (const void *)chrc_data, CHRC_SIZE);
}

BT_GATT_SERVICE_DEFINE(test_svc, BT_GATT_PRIMARY_SERVICE(TEST_SERVICE_UUID),
		       BT_GATT_CHARACTERISTIC(TEST_CHRC_UUID, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
					      read_test_chrc, NULL, NULL));

static struct bt_gatt_attr additional_attributes[] = {
	BT_GATT_CHARACTERISTIC(TEST_ADDITIONAL_CHRC_UUID, 0, BT_GATT_PERM_NONE, NULL, NULL, NULL),
};

static struct bt_gatt_service additional_gatt_service = BT_GATT_SERVICE(additional_attributes);

static void test_main_common(bool connect_eatt)
{
	int err;
	const struct bt_data ad[] = { BT_DATA_BYTES(BT_DATA_FLAGS,
						    (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)) };

	TEST_ASSERT(bk_sync_init() == 0, "Failed to open backchannel");

	err = bt_enable(NULL);
	if (err != 0) {
		TEST_FAIL("Bluetooth init failed (err %d)", err);

		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		TEST_FAIL("Advertising failed to start (err %d)", err);

		return;
	}

	printk("Advertising successfully started\n");

	WAIT_FOR_FLAG(flag_is_connected);

	if (connect_eatt) {
		WAIT_FOR_FLAG(flag_is_encrypted);

		err = bt_eatt_connect(g_conn, CONFIG_BT_EATT_MAX);
		if (err) {
			TEST_FAIL("Failed to connect EATT channels (err %d)", err);

			return;
		}
	}

	/* Wait for client to do discovery and configuration */
	bk_sync_wait();

	printk("Registering additional service\n");
	err = bt_gatt_service_register(&additional_gatt_service);
	if (err < 0) {
		TEST_FAIL("Registering additional service failed (err %d)", err);
	}

	/* Signal to client that additional service is registered */
	bk_sync_send();

	/* Wait for client to be done reading */
	bk_sync_wait();

	TEST_PASS("GATT server passed");
}

static void test_main_eatt(void)
{
	test_main_common(true);
}

static void test_main_no_eatt(void)
{
	test_main_common(false);
}

static const struct bst_test_instance test_gatt_server[] = {
	{
		.test_id = "gatt_server_eatt",
		.test_main_f = test_main_eatt,
	},
	{
		.test_id = "gatt_server_no_eatt",
		.test_main_f = test_main_no_eatt,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_server_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_gatt_server);
}
