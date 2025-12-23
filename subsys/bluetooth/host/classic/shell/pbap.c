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
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/pbap.h>
#include <zephyr/shell/shell.h>

#include "host/shell/bt.h"
#include "common/bt_shell_private.h"

struct bt_pbap_app {
	struct bt_pbap_pce pbap_pce;
        struct bt_pbap_pse pbap_pse;
	struct bt_pbap pbap;
	uint32_t conn_id;
	struct net_buf *tx_buf;
	struct bt_conn *conn;
        bool peer_srm;
        bool peer_srmp;
        uint16_t peer_mopl;
};
#define APPL_PBAP_MAX_COUNT 1U
static struct bt_pbap_app g_pbap[APPL_PBAP_MAX_COUNT];

static struct bt_pbap_app *g_pbap_app = NULL;

uint8_t rspcode = BT_PBAP_RSP_CODE_SUCCESS;

#define PBAP_APPL_PARAM_MAX_COUNT     10U
#define PBAP_APPL_PARAM_DATA_MAX_SIZE 10U
struct bt_obex_tlv gpbap_appl_param[PBAP_APPL_PARAM_MAX_COUNT];
uint8_t gpbap_appl_param_data[PBAP_APPL_PARAM_MAX_COUNT][PBAP_APPL_PARAM_DATA_MAX_SIZE];
uint8_t appl_param_count = 0U;

#define PBAP_MOPL  CONFIG_BT_GOEP_RFCOMM_MTU

NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_MAX_CONN, BT_RFCOMM_BUF_SIZE(PBAP_MOPL),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

#define APP_PBAP_PWD_MAX_LENGTH 50U
uint8_t pwd[APP_PBAP_PWD_MAX_LENGTH];

struct pbap_hdr {
	const uint8_t *value;
	uint16_t length;
};

static uint8_t pbap_ascii_to_unicode(const char *src, uint8_t *dest, uint16_t dest_size,
				     bool big_endian)
{
	uint8_t src_len;
	uint8_t required_size;
	uint8_t i;

	if (src == NULL || dest == NULL) {
		return 0;
	}

	src_len = strlen(src);
	required_size = (src_len + 1U) * 2U;

	if (dest_size < required_size) {
		bt_shell_error("Buffer too small: required %u, available %u", required_size,
			       dest_size);
		return 0; // 缓冲区太小
	}

	if (big_endian) {
		for (i = 0; i < src_len; i++) {
			dest[i * 2U] = 0x00;
			dest[i * 2U + 1U] = (uint8_t)src[i];
		}
	} else {
		for (i = 0; i < src_len; i++) {
			dest[i * 2U] = (uint8_t)src[i];
			dest[i * 2U + 1U] = 0x00;
		}
	}

	dest[src_len * 2] = 0x00;
	dest[src_len * 2 + 1] = 0x00;

	return required_size;
}

static uint8_t pbap_unicode_to_ascii(const uint8_t *src, uint16_t src_len, char *dest,
				     uint16_t dest_size, bool big_endian)
{
	uint8_t ascii_len;
	uint8_t i;

	if (src == NULL || dest == NULL) {
		return 0;
	}

	if (src_len % 2 != 0) {
		bt_shell_error("Invalid Unicode string length: %u", src_len);
		return 0;
	}

	ascii_len = src_len / 2;
	if (ascii_len > 0 && src[src_len - 2] == 0x00 && src[src_len - 1] == 0x00) {
		ascii_len--;
	}

	if (dest_size < (ascii_len + 1)) {
		bt_shell_error("Buffer too small: required %u, available %u",
			       ascii_len + 1, dest_size);
		return 0;
	}

	if (big_endian) {
		for (i = 0; i < ascii_len; i++) {
			dest[i] = (char)src[i * 2 + 1];
		}
	} else {
		for (i = 0; i < ascii_len; i++) {
			dest[i] = (char)src[i * 2];
		}
	}

	dest[ascii_len] = '\0';

	return ascii_len + 1;
}

static int stringTonum_64(char *str, uint64_t *value, uint8_t base)
{
	char *p = str;
	uint64_t total = 0;
	uint8_t chartoint = 0;
	if (base == 10) {
		while (p && *p != '\0') {
			if (*p >= '0' && *p <= '9') {
				total = total * 10 + (*p - '0');
			} else {
				return -EINVAL;
			}
			p++;
		}
	} else if (base == 16) {
		while (p && *p != '\0') {
			if (*p >= '0' && *p <= '9') {
				chartoint = *p - '0';
			} else if (*p >= 'a' && *p <= 'f') {
				chartoint = *p - 'a' + 10;
			} else if (*p >= 'A' && *p <= 'F') {
				chartoint = *p - 'A' + 10;
			} else {
				return -EINVAL;
			}
			total = 16 * total + chartoint;
			p++;
		}
	}
	*value = total;
	return 0;
}

static struct bt_pbap_app *g_pbap_allocate(struct bt_conn *conn)
{
	for (uint8_t i = 0; i < APPL_PBAP_MAX_COUNT; i++) {
		if (g_pbap[i].conn == NULL) {
			g_pbap[i].conn = conn;
			return &g_pbap[i];
		}
	}
	return NULL;
}

static void g_pbap_free(struct bt_pbap_app *pbap_app)
{
	pbap_app->conn = NULL;
}

static int cmd_alloc_buf(const struct shell *sh, size_t argc, char **argv)
{
	if (g_pbap_app == NULL) {
		bt_shell_error("g_pbap_app is not using");
		return -EINVAL;
	}

	if (g_pbap_app->tx_buf != NULL) {
		bt_shell_error("Buf %p is using", g_pbap_app->tx_buf);
		return -EBUSY;
	}

	g_pbap_app->tx_buf = bt_pbap_create_pdu(&g_pbap_app->pbap, &tx_pool);
	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_error("Fail to allocate tx buffer");
		return -ENOBUFS;
	}

	return 0;
}

static int cmd_release_buf(const struct shell *sh, size_t argc, char **argv)
{
	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_error("No buf is using");
		return -EINVAL;
	}

	net_buf_unref(g_pbap_app->tx_buf);
	g_pbap_app->tx_buf = NULL;

	return 0;
}

static void pbap_connected(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, uint8_t version,
			   uint16_t mopl, struct net_buf *buf)
{
	int err;
	struct bt_pbap_app *pbap_app = CONTAINER_OF(pbap_pce, struct bt_pbap_app, pbap_pce);

	bt_shell_print("pbap connect result %u, mopl %d ", rsp_code, mopl);
        pbap_app->peer_mopl = mopl;
	err = bt_obex_get_header_conn_id(buf, &pbap_app->conn_id);
	if (err != 0) {
		bt_shell_print("no connection id");
	}
}

static void pbap_disconnected(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
	if (rsp_code == BT_PBAP_RSP_CODE_OK) {
		bt_shell_print("pbap disconnect success %d", rsp_code);
	} else {
		bt_shell_print("pbap disconnect fail %d", rsp_code);
	}

	g_pbap_app->conn = NULL;
}

static int pbap_check_srm(struct net_buf *buf)
{
	uint8_t srm;
	int err;

	err = bt_obex_get_header_srm(buf, &srm);
	if (err != 0) {
		bt_shell_error("Failed to get header SRM %d", err);
		return err;
	}

	if (0x01 != srm) {
		bt_shell_error("SRM  mismatched %u != 0x01", srm);
		return -EINVAL;
	}

	return 0;
}

static int pbap_check_srmp(struct net_buf *buf)
{
	uint8_t srmp;
	int err;

	err = bt_obex_get_header_srm_param(buf, &srmp);
	if (err != 0) {
		bt_shell_error("Failed to get header SRMP %d", err);
		return err;
	}

	if (0x01 != srmp) {
		bt_shell_error("SRMP mismatched %u != 0x01", srmp);
		return -EINVAL;
	}

	return 0;
}

static void pull_cb(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, struct net_buf *buf, char *type)
{
	int err;
	struct pbap_hdr body;
	struct bt_pbap_app *pbap_app = CONTAINER_OF(pbap_pce, struct bt_pbap_app, pbap_pce);

	body.length = 0;
        rspcode = rsp_code;
	if (rsp_code == BT_PBAP_RSP_CODE_CONTINUE) {
		err = bt_obex_get_header_body(buf, &body.length, &body.value);
		if (err != 0) {
			bt_shell_print("Fail to get body or no body %d", err);
		}

                if (pbap_app->pbap.goep._goep_v2) {
                        pbap_app->peer_srmp = false;
                        pbap_app->peer_srm = false;
                        if (!bt_obex_has_header(buf, BT_OBEX_HEADER_ID_SRM)) {
                                bt_shell_print("Fail to get header srm %d", err);
                        } else {
                                if (pbap_check_srm(buf) != 0) {
                                        bt_shell_error("header srm is wrong");
                                }
                                bt_shell_print("get header srm success");
                                pbap_app->peer_srm = true;
                                if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_SRM_PARAM)) {
                                        if (pbap_check_srmp(buf) != 0) {
                                                bt_shell_error("header srmp is wrong");
                                        } else {
                                                bt_shell_print("get header srmp success");
                                                pbap_app->peer_srmp = true;
                                        }
                                }
                        }
                }
	} else if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS) {
		err = bt_obex_get_header_end_body(buf, &body.length, &body.value);
		if (err != 0) {
			bt_shell_print("Fail to get body or no body %d", err);
		}
		g_pbap_app->tx_buf = NULL;
	}
	if (body.length && body.value) {

		bt_shell_print("\n=========body=========\n");
		bt_shell_print("%.*s\r\n", body.length, body.value);
		bt_shell_print("=========body=========\n");
	}
	return;
}
static void pbap_pull_phonebook(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code, struct net_buf *buf)
{
	pull_cb(pbap_pce, rsp_code, buf, BT_PBAP_PULL_PHONEBOOK_TYPE);
}

static void pbap_pull_vcardlisting(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code,
				   struct net_buf *buf)
{
	pull_cb(pbap_pce, rsp_code, buf, BT_PBAP_PULL_VCARD_LISTING_TYPE);
}

static void pbap_pull_vcardentry(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code,
				 struct net_buf *buf)
{
	pull_cb(pbap_pce, rsp_code, buf, BT_PBAP_PULL_VCARD_ENTRY_TYPE);
}

static void pbap_set_path(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
	if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS) {
		bt_shell_print("set path success.");
	} else {
		bt_shell_print("set path fail.");
	}
}

static void pbap_abort(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf)
{
	if (rsp_code == BT_PBAP_RSP_CODE_SUCCESS) {
		bt_shell_print("abort success.");
	} else {
		bt_shell_print("abort fail.");
	}
}

// // void pbap_get_auth_info(struct bt_pbap_pce *pbap)
// // {
// // 	memcpy(pwd, "0000", 4U);
// // 	pbap->pwd = pwd;
// // }

struct bt_pbap_pce_cb pce_cb = {
	.connect = pbap_connected,
	.disconnect = pbap_disconnected,
	.pull_phonebook = pbap_pull_phonebook,
	.pull_vcardlisting = pbap_pull_vcardlisting,
	.pull_vcardentry = pbap_pull_vcardentry,
	.set_path = pbap_set_path,
        .abort = pbap_abort,
	// .get_auth_info = pbap_get_auth_info,
};

static void pbap_pce_transport_connected(struct bt_conn *conn, struct bt_pbap *pbap)
{
	bt_shell_print("PBAP PCE %p transport connected on %p", pbap, conn);
}

static void pbap_pce_transport_disconnected(struct bt_pbap *pbap)
{
	struct bt_pbap_app *g_app = CONTAINER_OF(pbap, struct bt_pbap_app, pbap);

	g_pbap_free(g_app);
	bt_shell_print("PBAP PCE %p transport disconnected", pbap);
}

static struct bt_pbap_transport_ops pbap_pce_transport_ops = {
	.connected = pbap_pce_transport_connected,
	.disconnected = pbap_pce_transport_disconnected,
};

static int pbap_transport_connect(uint8_t channel, uint16_t psm)
{
	int err;

	if (channel == 0 && psm == 0) {
		bt_shell_print("Invalid channel and psm");
		return -EINVAL;
	}

	g_pbap_app = g_pbap_allocate(default_conn);
	if (g_pbap_app == NULL) {
		bt_shell_print("No available pbap");
		return -EINVAL;
	}

	g_pbap_app->pbap.ops = &pbap_pce_transport_ops;
	if (channel != 0 && psm == 0) {
		err = bt_pbap_rfcomm_connect(default_conn, &g_pbap_app->pbap, channel);
	} else {
		err = bt_pbap_l2cap_connect(default_conn, &g_pbap_app->pbap, psm);
	}

	if (err != 0) {
		bt_shell_print("Fail to send tranpsport connect %d", err);
		g_pbap_app->conn = NULL;
		return err;
	}

	return 0;
}

static int transport_disconnect(bool is_rfcomm)
{
	int err;

	if (default_conn == NULL) {
		bt_shell_error("Not connected");
		return -ENOEXEC;
	}

	if (g_pbap_app->conn == NULL) {
		bt_shell_error("No pbap transport connection");
		return -ENOEXEC;
	}

	if (is_rfcomm) {
		err = bt_pbap_rfcomm_disconnect(&g_pbap_app->pbap);
	} else {
		err = bt_pbap_l2cap_disconnect(&g_pbap_app->pbap);
	}

	if (err != 0) {
		bt_shell_error("Fail to send disconnect cmd (err %d)", err);
	} else {
		bt_shell_print("PBAP disconnection pending");
	}
	return err;
}

static int cmd_connect_rfcomm(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t channel = (uint8_t)strtoul(argv[1], NULL, 16);

	return pbap_transport_connect(channel, 0);
}

static int cmd_connect_l2cap(const struct shell *sh, size_t argc, char *argv[])
{
	uint16_t psm = (uint16_t)strtoul(argv[1], NULL, 16);

	return pbap_transport_connect(0, psm);
}

static int cmd_disconnect_rfcomm(const struct shell *sh, size_t argc, char *argv[])
{
	return transport_disconnect(true);
}

static int cmd_disconnect_l2cap(const struct shell *sh, size_t argc, char *argv[])
{
	return transport_disconnect(false);
}

static int cmd_connect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	uint16_t mopl = (uint16_t)strtoul(argv[1], NULL, 16);

	if (g_pbap_app == NULL) {
		bt_shell_print("No available pbap");
		return -EINVAL;
	}

	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_print("No available tx buf");
		return -EINVAL;
	}
	g_pbap_app->pbap_pce._mopl = mopl;
	g_pbap_app->pbap_pce._peer_feature = 0x3FF;
	g_pbap_app->pbap_pce.pwd = NULL;
	if (argc > 3) {
		memcpy(pwd, argv[3], strlen(argv[3]));
		g_pbap_app->pbap_pce.pwd = pwd;
	}
	err = bt_pbap_add_header_target(g_pbap_app->tx_buf);
	if (err != 0) {
		bt_shell_print("Fail to add target %d", err);
		goto failed;
	}

	err = bt_pbap_pce_connect(&g_pbap_app->pbap, &g_pbap_app->pbap_pce, &pce_cb,
				  g_pbap_app->tx_buf);
	if (err != 0) {
		bt_shell_print("Fail to connect %d", err);
	}

	g_pbap_app->tx_buf = NULL;
	return err;

failed:
	net_buf_unref(g_pbap_app->tx_buf);
	g_pbap_app->tx_buf = NULL;
	return err;
}

static int cmd_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	err = bt_pbap_pce_disconnect(&g_pbap_app->pbap_pce, NULL);
	if (err != 0) {
		bt_shell_error("Fail to disconnect %d", err);
	}
	return err;
}

static int pbap_pce_pull(char *type, char *name, bool srmp)
{
	int err = 0;
	int length;
	uint8_t unicode_trans[50] = {0};

	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_print("Fail to get tx buf");
		return -EINVAL;
	}

        if (rspcode == BT_PBAP_RSP_CODE_SUCCESS) {
                err = bt_obex_add_header_conn_id(g_pbap_app->tx_buf, g_pbap_app->conn_id);
                if (err != 0) {
                        bt_shell_print("Fail to add connection id %d", err);
                        goto failed;
                }

                if (g_pbap_app->pbap.goep._goep_v2) {
                        err = bt_pbap_add_header_srm(g_pbap_app->tx_buf);
                        if (err != 0) {
                                bt_shell_print("Fail to add srm header %d", err);
                                goto failed;
                        }
                        if (srmp) {
                                err = bt_pbap_add_header_srm_param(g_pbap_app->tx_buf);
                                if (err != 0) {
                                        bt_shell_print("Fail to add srmp header %d", err);
                                        goto failed;
                                }
                        }
                }

                length = 0;
                if (name != NULL) {
                        length = pbap_ascii_to_unicode((uint8_t *)name, (uint8_t *)unicode_trans,
                                                sizeof(unicode_trans), true);
                }

                err = bt_obex_add_header_name(g_pbap_app->tx_buf, length, unicode_trans);
                if (err != 0) {
                        bt_shell_print("Fail to add name header %d", err);
                        goto failed;
                }

                err = bt_obex_add_header_type(g_pbap_app->tx_buf, strlen(type) + 1U, type);
                if (err != 0) {
                        bt_shell_print("Fail to add type header %d", err);
                        goto failed;
                }
        }

	if (strcmp(type, BT_PBAP_PULL_PHONEBOOK_TYPE) == 0) {
		err = bt_pbap_pce_pull_phonebook(&g_pbap_app->pbap_pce, g_pbap_app->tx_buf);
	} else if (strcmp(type, BT_PBAP_PULL_VCARD_LISTING_TYPE) == 0) {
		err = bt_pbap_pce_pull_vcardlisting(&g_pbap_app->pbap_pce, g_pbap_app->tx_buf);
	} else if (strcmp(type, BT_PBAP_PULL_VCARD_ENTRY_TYPE) == 0) {
		err = bt_pbap_pce_pull_vcardentry(&g_pbap_app->pbap_pce, g_pbap_app->tx_buf);
	}

	if (err != 0) {
		bt_shell_print("Fail to create pull phonebook cmd %d", err);
		goto failed;
	}

	g_pbap_app->tx_buf = NULL;
	return 0;
failed:
	net_buf_unref(g_pbap_app->tx_buf);
	g_pbap_app->tx_buf = NULL;
	return err;
}

static int cmd_pull_pb(const struct shell *sh, size_t argc, char *argv[])
{
	bool srmp = false;
	int err;

	if (argc > 2) {
		srmp = (uint8_t)strtoul(argv[2], NULL, 16) == 0 ? false : true;
	}

	err = pbap_pce_pull(BT_PBAP_PULL_PHONEBOOK_TYPE, argv[1], srmp);
	if (err != 0) {
		bt_shell_error("Fail to send pull phonebook %d cmd", err);
	}
	return err;
}

static int cmd_pull_vcardlisting(const struct shell *sh, size_t argc, char *argv[])
{
	bool srmp = false;
	int err;

	if (argc > 2) {
		srmp = (uint8_t)strtoul(argv[2], NULL, 16) == 0 ? false : true;
	}

	err = pbap_pce_pull(BT_PBAP_PULL_VCARD_LISTING_TYPE, argv[1], srmp);
	if (err != 0) {
		bt_shell_error("Fail to send pull vcardlisting %d cmd", err);
	}
	return err;
}

static int cmd_pull_vcardentry(const struct shell *sh, size_t argc, char *argv[])
{
	bool srmp = false;
	int err;

	if (argc > 2) {
		srmp = (uint8_t)strtoul(argv[2], NULL, 16) == 0 ? false : true;
	}

	err = pbap_pce_pull(BT_PBAP_PULL_VCARD_ENTRY_TYPE, argv[1], srmp);
	if (err != 0) {
		bt_shell_error("Fail to send pull vcardentry %d cmd", err);
	}
	return err;
}

static int cmd_set_path(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t len = 0;
	int err;
	uint8_t flags = (uint8_t)strtoul(argv[1], NULL, 16);
	uint8_t unicode_trans[50] = {0};

	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_print("Fail to get tx buf");
		return -EINVAL;
	}

	if (argc > 2) {
		len = pbap_ascii_to_unicode((uint8_t *)argv[2], (uint8_t *)unicode_trans,
					    sizeof(unicode_trans), true);
	}

	err = bt_obex_add_header_conn_id(g_pbap_app->tx_buf, g_pbap_app->conn_id);
	if (err != 0) {
		bt_shell_print("Fail to add connection id %d", err);
		goto failed;
	}

	err = bt_obex_add_header_name(g_pbap_app->tx_buf, len, unicode_trans);
	if (err != 0) {
		bt_shell_print("Fail to add name header %d", err);
		goto failed;
	}

	err = bt_pbap_pce_setpath(&g_pbap_app->pbap_pce, flags, g_pbap_app->tx_buf);
	if (err != 0) {
		bt_shell_print("Fail to create set path cmd %d", err);
		goto failed;
	}

	g_pbap_app->tx_buf = NULL;
	return 0;
failed:
	net_buf_unref(g_pbap_app->tx_buf);
	g_pbap_app->tx_buf = NULL;
	return err;
}

static int cmd_abort(const struct shell *sh, size_t argc, char **argv)
{
        int err;

	if (g_pbap_app->conn == NULL) {
		shell_error(sh, "No bip transport connection");
		return -ENOEXEC;
	}

	err = bt_pbap_pce_abort(&g_pbap_app->pbap_pce, g_pbap_app->tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send abort req %d", err);
	}

        g_pbap_app->tx_buf = NULL;
	return err;
}

// // static int cmd_add_appl_param(const struct shell *sh, size_t argc, char **argv)
// // {
// // 	uint64_t value = 0;
// // 	if (argc < 2) {
// // 		shell_help(sh);
// // 		shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);
// // 		return SHELL_CMD_HELP_PRINTED;
// // 	}

// // 	if (appl_param_count > PBAP_APPL_PARAM_MAX_COUNT) {
// // 		shell_error(sh, "No space of TLV array, add app_param and clear tlvs");
// // 		return -EAGAIN;
// // 	}

// // 	if (!g_pbap_app->tx_buf) {
// // 		bt_shell_print("No available tx buf");
// // 		return -EINVAL;
// // 	}

// // 	uint8_t *tag = argv[0];

// // 	if (!strcmp(tag, "ps")) {
// // 		gpbap_appl_param[appl_param_count].type =
// // 			BT_PBAP_APPL_PARAM_TAG_ID_PROPERTY_SELECTOR;
// // 		gpbap_appl_param[appl_param_count].data_len = 8U;
// // 		stringTonum_64(argv[1U], &value, 10);
// // 		sys_put_be64(value, &gpbap_appl_param_data[appl_param_count][0]);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "f")) {
// // 		gpbap_appl_param[appl_param_count].type = BT_PBAP_APPL_PARAM_TAG_ID_FORMAT;
// // 		gpbap_appl_param[appl_param_count].data_len = 1U;
// // 		gpbap_appl_param_data[appl_param_count][0] = strtoul(argv[1U], NULL, 10);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "mlc")) {
// // 		gpbap_appl_param[appl_param_count].type = BT_PBAP_APPL_PARAM_TAG_ID_MAX_LIST_COUNT;
// // 		gpbap_appl_param[appl_param_count].data_len = 2U;
// // 		sys_put_be16((uint16_t)strtoul(argv[1U], NULL, 10),
// // 			     &gpbap_appl_param_data[appl_param_count][0]);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "lso")) {
// // 		gpbap_appl_param[appl_param_count].type =
// // 			BT_PBAP_APPL_PARAM_TAG_ID_LIST_START_OFFSET;
// // 		gpbap_appl_param[appl_param_count].data_len = 2U;
// // 		sys_put_be16((uint16_t)strtoul(argv[1U], NULL, 10),
// // 			     &gpbap_appl_param_data[appl_param_count][0]);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "rnmc")) {
// // 		gpbap_appl_param[appl_param_count].type =
// // 			BT_PBAP_APPL_PARAM_TAG_ID_RESET_NEW_MISSED_CALLS;
// // 		gpbap_appl_param[appl_param_count].data_len = 1U;
// // 		gpbap_appl_param_data[appl_param_count][0] = strtoul(argv[1U], NULL, 10);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "vcs")) {
// // 		gpbap_appl_param[appl_param_count].type = BT_PBAP_APPL_PARAM_TAG_ID_VCARD_SELECTOR;
// // 		gpbap_appl_param[appl_param_count].data_len = 8U;
// // 		stringTonum_64(argv[1U], &value, 10);
// // 		sys_put_be64(value, &gpbap_appl_param_data[appl_param_count][0]);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "vcso")) {
// // 		gpbap_appl_param[appl_param_count].type =
// // 			BT_PBAP_APPL_PARAM_TAG_ID_VCARD_SELECTOR_OPERATOR;
// // 		gpbap_appl_param[appl_param_count].data_len = 1U;
// // 		gpbap_appl_param_data[appl_param_count][0] = strtoul(argv[1U], NULL, 10);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "o")) {
// // 		gpbap_appl_param[appl_param_count].type = BT_PBAP_APPL_PARAM_TAG_ID_ORDER;
// // 		gpbap_appl_param[appl_param_count].data_len = 1U;
// // 		gpbap_appl_param_data[appl_param_count][0] = strtoul(argv[1U], NULL, 10);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "sv")) {
// // 		gpbap_appl_param[appl_param_count].type = BT_PBAP_APPL_PARAM_TAG_ID_SEARCH_VALUE;
// // 		gpbap_appl_param[appl_param_count].data_len = strlen(argv[1]);
// // 		memcpy(&gpbap_appl_param_data[appl_param_count][0], argv[1], strlen(argv[1]));
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else if (!strcmp(tag, "sp")) {
// // 		gpbap_appl_param[appl_param_count].type = BT_PBAP_APPL_PARAM_TAG_ID_SEARCH_PROPERTY;
// // 		gpbap_appl_param[appl_param_count].data_len = 1U;
// // 		gpbap_appl_param_data[appl_param_count][0] = strtoul(argv[1U], NULL, 10);
// // 		gpbap_appl_param[appl_param_count].data =
// // 			&gpbap_appl_param_data[appl_param_count][0];
// // 	} else {
// // 		shell_error(sh, "No available appl param");
// // 		return -EINVAL;
// // 	}
// // 	appl_param_count++;
// // 	return 0;
// // }

// // SHELL_STATIC_SUBCMD_SET_CREATE(
// // 	pbap_add_appl_params,
// // 	SHELL_CMD_ARG(ps, NULL, "PropertySelector : 8bytes", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(f, NULL, "Format : 1byte", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(mlc, NULL, "MaxListCount : 2bytes", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(lso, NULL, "ListStartOffset : 2bytes", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(rnmc, NULL, "ResetNewMissedCalls : 1byte", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(vcs, NULL, "vCardSelector : 8bytes", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(vcso, NULL, "vCardSelectorOperator : 1byte", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(o, NULL, "Order : 1byte", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(sv, NULL, "SearchValue : string", cmd_add_appl_param, 2, 0),
// // 	SHELL_CMD_ARG(sp, NULL, "SearchProperty : 1byte", cmd_add_appl_param, 2, 0),
// // 	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	pce_cmds, SHELL_CMD_ARG(connect_rfcomm, NULL, "<channel>", cmd_connect_rfcomm, 2, 0),
	SHELL_CMD_ARG(disconnect_rfcomm, NULL, "", cmd_disconnect_rfcomm, 1, 0),
	SHELL_CMD_ARG(connect_l2cap, NULL, "<channel>", cmd_connect_l2cap, 2, 0),
	SHELL_CMD_ARG(disconnect_l2cap, NULL, "", cmd_disconnect_l2cap, 1, 0),
	SHELL_CMD_ARG(connect, NULL, "<mpl> [password]", cmd_connect, 2, 1),
	SHELL_CMD_ARG(disconnect, NULL, "", cmd_disconnect, 2, 0),
	SHELL_CMD_ARG(pull_pb, NULL, "<name> [srmp(0/1)]", cmd_pull_pb, 2, 1),
	SHELL_CMD_ARG(pull_vcardlisting, NULL, "<name> [srmp(0/1)]", cmd_pull_vcardlisting, 2, 1),
	SHELL_CMD_ARG(pull_vcardentry, NULL, "<name> [srmp(0/1)]", cmd_pull_vcardentry, 2, 1),
	SHELL_CMD_ARG(set_path, NULL, "<Flags> [name]", cmd_set_path, 2, 1),
        SHELL_CMD_ARG(abort, NULL, "", cmd_abort, 1, 0),
        SHELL_SUBCMD_SET_END);


static void pbap_pse_connected(struct bt_pbap_pse *pbap_pse, uint8_t version,
			uint16_t mopl, struct net_buf *buf)
{
        struct bt_pbap_app *g_app = CONTAINER_OF(pbap_pse, struct bt_pbap_app, pbap_pse);
        bt_shell_print("pbap connect version %u, mopl %d ", version, mopl);
        g_app->peer_mopl = mopl;
}

static void pbap_pse_disconnected(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
        bt_shell_print("pbap disconnect");
}

static void pbap_pse_pull_cb(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
	struct bt_pbap_app *g_app = CONTAINER_OF(pbap_pse, struct bt_pbap_app, pbap_pse);
        int err;

        if (pbap_pse->_pbap->goep._goep_v2) {
                if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_SRM)) {
                        g_app->peer_srm = true;
                }

                if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_SRM_PARAM)) {
                        g_app->peer_srmp = true;
                }
        }

        if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_NAME)) {
                uint16_t len;
                const uint8_t *unicode_name;
                uint8_t ascii_name[25] = {0};
                err = bt_obex_get_header_name(buf, &len, &unicode_name);
                if (err != 0) {
                        bt_shell_error("fail get header name");
                }
                len = pbap_unicode_to_ascii(unicode_name, len, ascii_name, 25, true);
                bt_shell_print("name = %.*s", len,ascii_name);
        }
}

static void pbap_pse_pull_phonebook(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
        bt_shell_print("pbap_pse get pull_phonebook request");
        pbap_pse_pull_cb(pbap_pse, buf);
}

static void pbap_pse_pull_vcardlisting(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
        bt_shell_print("pbap_pse_pull_vcardlisting");
        pbap_pse_pull_cb(pbap_pse, buf);
}

static void pbap_pse_pull_vcardentry(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
        bt_shell_print("pbap_pse_pull_vcardentry");
        pbap_pse_pull_cb(pbap_pse, buf);
}

static void pbap_pse_setpath(struct bt_pbap_pse *pbap_pse, uint8_t flags, struct net_buf *buf)
{
        const uint8_t *name;
        uint16_t len;
        int err;

        if (flags == BT_PBAP_SET_PATH_FLAGS_UP) {
                bt_shell_print("set path to parent folder");
        } else if (flags == BT_PBAP_SET_PATH_FLAGS_DOWN_OR_ROOT) {
                if (!bt_obex_has_header(buf, BT_OBEX_HEADER_ID_NAME)) {
                        bt_shell_print("set path to ROOT folder");
                } else {
                        err = bt_obex_get_header_name(buf, &len, &name);
                        if (err != 0 || len == 3) {
                                bt_shell_print("set path to ROOT folder");
                        } else {
                                uint8_t ascii_name[25] = {0};
                                len = pbap_unicode_to_ascii(name, len, ascii_name, 25, true);
                                bt_shell_print("set path to children %.*s folder", len, ascii_name);
                        }
                }
        }
}

static void pbap_pse_abort(struct bt_pbap_pse *pbap_pse, struct net_buf *buf)
{
        bt_shell_print("receive abort req");
}

struct bt_pbap_pse_cb pse_cb = {
        .connect = pbap_pse_connected,
        .disconnect = pbap_pse_disconnected,
        .pull_phonebook = pbap_pse_pull_phonebook,
        .pull_vcardlisting = pbap_pse_pull_vcardlisting,
        .pull_vcardentry = pbap_pse_pull_vcardentry,
        .setpath = pbap_pse_setpath,
        .abort = pbap_pse_abort,
};

static struct bt_pbap_pse_rfcomm rfcomm_server;
static struct bt_pbap_pse_l2cap l2cap_server;

static void pbap_pse_transport_connected(struct bt_conn *conn, struct bt_pbap *pbap)
{
        bt_shell_print("PBAP %p transport connected on %p", pbap, conn);
}

static void pbap_pse_transport_disconnected(struct bt_pbap *pbap)
{
	struct bt_pbap_app *g_app = CONTAINER_OF(pbap, struct bt_pbap_app, pbap);

	g_pbap_free(g_app);
	bt_shell_print("PBAP %p transport disconnected", pbap);
}


static struct bt_pbap_transport_ops pbap_pse_transport_ops = {
	.connected = pbap_pse_transport_connected,
	.disconnected =pbap_pse_transport_disconnected,
};

static int rfcomm_accept(struct bt_conn *conn, struct bt_pbap_pse_rfcomm *pbap_pse,
		      struct bt_pbap **pbap)
{
        g_pbap_app = g_pbap_allocate(default_conn);
        if (g_pbap_app == NULL) {
		bt_shell_error("PBAP instance not allocated");
		return -EINVAL;
	}

	g_pbap_app->pbap.ops = &pbap_pse_transport_ops;
	*pbap = &g_pbap_app->pbap;

	return 0;
}

static int l2cap_accept(struct bt_conn *conn, struct bt_pbap_pse_l2cap *pbap_pse,
		      struct bt_pbap **pbap)
{
        g_pbap_app = g_pbap_allocate(default_conn);
        if (g_pbap_app == NULL) {
		bt_shell_error("PBAP instance not allocated");
		return -EINVAL;
	}

	g_pbap_app->pbap.ops = &pbap_pse_transport_ops;
	*pbap = &g_pbap_app->pbap;

	return 0;
}

static int cmd_rfcomm_register(const struct shell *sh, size_t argc, char **argv)
{
        int err;
	uint8_t channel;

	if (rfcomm_server.server.rfcomm.channel != 0) {
		shell_error(sh, "RFCOMM has been registered");
		return -EBUSY;
	}

	channel = (uint8_t)strtoul(argv[1], NULL, 16);

	rfcomm_server.server.rfcomm.channel = channel;
	rfcomm_server.accept = rfcomm_accept;
	err = bt_pbap_pse_rfcomm_register(&rfcomm_server);
	if (err != 0) {
		shell_error(sh, "Fail to register RFCOMM server (error %d)", err);
		rfcomm_server.server.rfcomm.channel = 0;
		return -ENOEXEC;
	}
	shell_print(sh, "RFCOMM server (channel %02x) is registered",
		    rfcomm_server.server.rfcomm.channel);
	return 0;
}

static int cmd_l2cap_register(const struct shell *sh, size_t argc, char **argv)
{
        int err;
	uint16_t psm;

	if (l2cap_server.server.l2cap.psm != 0) {
		shell_error(sh, "l2cap has been registered");
		return -EBUSY;
	}

	psm = (uint8_t)strtoul(argv[1], NULL, 16);

	l2cap_server.server.l2cap.psm = psm;
	l2cap_server.accept = l2cap_accept;
	err = bt_pbap_pse_l2cap_register(&l2cap_server);
	if (err != 0) {
		shell_error(sh, "Fail to register RFCOMM server (error %d)", err);
		l2cap_server.server.l2cap.psm = 0;
		return -ENOEXEC;
	}
	shell_print(sh, "L2cap server (psm %02x) is registered",
		    l2cap_server.server.l2cap.psm);
	return 0;
}

static int cmd_register(const struct shell *sh, size_t argc, char **argv)
{
        int err;

        g_pbap_app = g_pbap_allocate(NULL);
        if (g_pbap_app == NULL) {
		shell_error(sh, "PBAP instance not allocated");
		return -EINVAL;
	}

        g_pbap_app->pbap_pse.mopl = 600;
	err = bt_pbap_pse_register(&g_pbap_app->pbap, &g_pbap_app->pbap_pse, &pse_cb);
	if (err != 0) {
		shell_error(sh, "Fail to register server %d", err);
	}

	return err;
}

static int cmd_connect_rsp(const struct shell *sh, size_t argc, char **argv)
{
        uint8_t rsp_code;
	const char *rsp;
	int err;

	if (default_conn == NULL) {
		bt_shell_error("Not connected");
		return -ENOEXEC;
	}

	if (g_pbap_app->conn == NULL) {
		bt_shell_error("No pbap transport connection");
		return -ENOEXEC;
	}

	rsp = argv[1];
	if (!strcmp(rsp, "unauth")) {
		rsp_code = BT_OBEX_RSP_CODE_UNAUTH;
	} else if (!strcmp(rsp, "success")) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else if (!strcmp(rsp, "error")) {
		if (argc < 3) {
			shell_error(sh, "[rsp_code] is needed if the rsp is %s", rsp);
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}
		rsp_code = (uint8_t)strtoul(argv[2], NULL, 16);
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	err = bt_pbap_pse_connect_rsp(&g_pbap_app->pbap_pse, rsp_code, g_pbap_app->tx_buf);
	if (err != 0) {
		shell_error(sh, "Fail to send conn rsp %d", err);
                net_buf_unref(g_pbap_app->tx_buf);
	}

        g_pbap_app->tx_buf = NULL;
	return err;
}

static const char pbap_pse_phonebook_example[] =
    "BEGIN:VCARD\n\
VERSION:2.1\n\
FN;CHARSET=UTF-8:descvs\n\
N;CHARSET=UTF-8:descvs\n\
END:VCARD";

static const char pbap_pse_vcard_listing[] =
    "<?xml version=\"1.0\"?><!DOCTYPE vcard-listing SYSTEM \"vcard-listing.dtd\"><vCard-listing version=\"1.0\">\
<card handle=\"1.vcf\" name=\"qwe\"/><card handle=\"2.vcf\" name=\"qwe\"/><card handle=\"3.vcf\" name=\"qwe\"/>\
<card handle=\"4.vcf\" name=\"1155\"/><card handle=\"5.vcf\"/>/vCard-listing>";

static const char pbap_pse_vcard_entry[] =
    "BEGIN:VCARD\n\
VERSION:2.1\n\
FN:\n\
N:\n\
TEL;X-0:1155\n\
X-IRMC-CALL-DATETIME;DIALED:20220913T110607\n\
END:VCARD";

static int pbap_get_rsp(const struct shell *sh, size_t argc, char *argv[], const char *type)
{
        uint8_t rsp_code;
	const char *rsp;
	int err = 0;
        bool srmp;
        uint16_t length;
        const char *value;

	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_print("Fail to get tx buf");
		return -EINVAL;
	}

        srmp = (uint8_t)strtoul(argv[1], NULL, 16) == 0 ? false : true;
	rsp = argv[2];
	if (strcmp(rsp, "continue") == 0) {
		rsp_code = BT_OBEX_RSP_CODE_CONTINUE;
	} else if (!strcmp(rsp, "success") == 0) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else if (!strcmp(rsp, "error") == 0) {
		if (argc < 4) {
			shell_error(sh, "[rsp_code] is needed if the rsp is %s", rsp);
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}
		rsp_code = (uint8_t)strtoul(argv[3], NULL, 16);
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

        if (g_pbap_app->pbap_pse._pbap->goep._goep_v2) {
                if (rsp_code == BT_OBEX_RSP_CODE_SUCCESS && g_pbap_app->peer_srm) {
                        err = bt_pbap_add_header_srm(g_pbap_app->tx_buf);
                        if (err != 0) {
                                shell_error(sh, "Fail to add SRM header %d", err);
                                return err;
                        }

                        if (srmp) {
                                err = bt_pbap_add_header_srm_param(g_pbap_app->tx_buf);
                                if (err != 0) {
                                        shell_error(sh, "Fail to add SRMP header %d", err);
                                        return err;
                                }
                        }
                }
        }

        if (strcmp(type, BT_PBAP_PULL_PHONEBOOK_TYPE) == 0) {
                value = pbap_pse_phonebook_example;
        } else if (strcmp(type, BT_PBAP_PULL_VCARD_LISTING_TYPE) == 0) {
                value = pbap_pse_vcard_listing;
        } else if (strcmp(type, BT_PBAP_PULL_VCARD_ENTRY_TYPE) == 0) {
                value = pbap_pse_vcard_entry;
        } else {
                shell_error(sh, "Unknown type %s", type);
                return -EINVAL;
        }

        length = g_pbap_app->peer_mopl < g_pbap_app->pbap_pse._pbap->goep.obex.tx.mtu ? g_pbap_app->peer_mopl :
                                                                g_pbap_app->pbap_pse._pbap->goep.obex.tx.mtu;
        length = length < strlen(value) ? length : strlen(value);

        if (rsp_code ==  BT_OBEX_RSP_CODE_SUCCESS) {
                err = bt_obex_add_header_end_body(g_pbap_app->tx_buf, length, value);
                if (err != 0) {
                                shell_error(sh, "Fail to add phonebook data %d", err);
                                return err;
                }
        } else if (rsp_code == BT_OBEX_RSP_CODE_CONTINUE) {
                err = bt_obex_add_header_body(g_pbap_app->tx_buf, length, value);
                if (err != 0) {
                        shell_error(sh, "Fail to add phonebook data %d", err);
                        return err;
                }
        }
        if (strcmp(type, BT_PBAP_PULL_PHONEBOOK_TYPE) == 0) {
                err = bt_pbap_pse_pull_phonebook_rsp(&g_pbap_app->pbap_pse, rsp_code, g_pbap_app->tx_buf);
        } else if (strcmp(type, BT_PBAP_PULL_VCARD_LISTING_TYPE) == 0) {
                err = bt_pbap_pse_pull_vcardlisting_rsp(&g_pbap_app->pbap_pse, rsp_code, g_pbap_app->tx_buf);
        } else if (strcmp(type, BT_PBAP_PULL_VCARD_ENTRY_TYPE) == 0) {
                err = bt_pbap_pse_pull_vcardentry_rsp(&g_pbap_app->pbap_pse, rsp_code, g_pbap_app->tx_buf);
        }

        if (err != 0) {
                shell_error(sh, "Fail to send pull rsp %d", err);
                net_buf_unref(g_pbap_app->tx_buf);
        }

        g_pbap_app->tx_buf = NULL;
	return err;
}
static int cmd_pull_phonebook_rsp(const struct shell *sh, size_t argc, char *argv[])
{
	return pbap_get_rsp(sh, argc, argv, BT_PBAP_PULL_PHONEBOOK_TYPE);
}

static int cmd_pull_vcardlisting_rsp(const struct shell *sh, size_t argc, char *argv[])
{
	return pbap_get_rsp(sh, argc, argv, BT_PBAP_PULL_VCARD_LISTING_TYPE);
}

static int cmd_pull_vcardentry_rsp(const struct shell *sh, size_t argc, char *argv[])
{
	return pbap_get_rsp(sh, argc, argv, BT_PBAP_PULL_VCARD_ENTRY_TYPE);
}

static int cmd_setpath_rsp(const struct shell *sh, size_t argc, char *argv[])
{
        const char *rsp;
        uint8_t rsp_code;
        int err;

	rsp = argv[1];
	if (strcmp(rsp, "success") == 0) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else if (!strcmp(rsp, "error")) {
		if (argc < 3) {
			shell_error(sh, "[rsp_code] is needed if the rsp is %s", rsp);
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}
		rsp_code = (uint8_t)strtoul(argv[2], NULL, 16);
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_print("Fail to get tx buf");
		return -EINVAL;
	}

        err = bt_pbap_pse_setpath_rsp(&g_pbap_app->pbap_pse, rsp_code, g_pbap_app->tx_buf);
        if (err != 0) {
                shell_error(sh, "Fail to send setpath rsp %d", err);
                net_buf_unref(g_pbap_app->tx_buf);
        }

        g_pbap_app->tx_buf = NULL;
        return err;

}

static int cmd_abort_rsp(const struct shell *sh, size_t argc, char *argv[])
{
        const char *rsp;
        uint8_t rsp_code;
        int err;

	rsp = argv[1];
	if (strcmp(rsp, "success") == 0) {
		rsp_code = BT_OBEX_RSP_CODE_SUCCESS;
	} else if (!strcmp(rsp, "error")) {
		if (argc < 3) {
			shell_error(sh, "[rsp_code] is needed if the rsp is %s", rsp);
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}
		rsp_code = (uint8_t)strtoul(argv[2], NULL, 16);
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	if (g_pbap_app->tx_buf == NULL) {
		bt_shell_print("Fail to get tx buf");
		return -EINVAL;
	}

        err = bt_pbap_pse_abort_rsp(&g_pbap_app->pbap_pse, rsp_code, g_pbap_app->tx_buf);
        if (err != 0) {
                shell_error(sh, "Fail to send abort rsp %d", err);
                net_buf_unref(g_pbap_app->tx_buf);
        }

        g_pbap_app->tx_buf = NULL;
        return err;

}

SHELL_STATIC_SUBCMD_SET_CREATE(pse_cmds,
	SHELL_CMD_ARG(rfcomm_register, NULL, "[channel]", cmd_rfcomm_register, 2, 0),
	SHELL_CMD_ARG(l2cap_register, NULL, "[psm]", cmd_l2cap_register, 2, 0),
        SHELL_CMD_ARG(register, NULL, "", cmd_register, 1, 0),
        SHELL_CMD_ARG(connect_rsp, NULL, "<rsp: unauth,success,error> [rsp_code]", cmd_connect_rsp, 2, 1),
	SHELL_CMD_ARG(pull_phonebook_rsp, NULL, "<wait: 0,1> <rsp: continue, success, error> [rsp_code]",
		      cmd_pull_phonebook_rsp, 2, 1),
	SHELL_CMD_ARG(pull_vcardlisting_rsp, NULL, "<wait: 0,1> <rsp: continue, success, error> [rsp_code]",
		      cmd_pull_vcardlisting_rsp, 2, 1),
	SHELL_CMD_ARG(pull_vcardentry_rsp, NULL, "<wait: 0,1> <rsp: continue, success, error> [rsp_code]",
		      cmd_pull_vcardentry_rsp, 2, 1),
	SHELL_CMD_ARG(setpath_rsp, NULL, "<rsp: success, error> [rsp_code]",
		      cmd_setpath_rsp, 1, 1),
	SHELL_CMD_ARG(abort_rsp, NULL, "<rsp: success, error> [rsp_code]",
		      cmd_abort_rsp, 1, 1),
	SHELL_SUBCMD_SET_END
);

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
        SHELL_CMD_ARG(alloc_buf, NULL, "Alloc tx buffer", cmd_alloc_buf, 1, 0),
	SHELL_CMD_ARG(release_buf, NULL, "Free allocated tx buffer", cmd_release_buf, 1, 0),
	SHELL_CMD_ARG(pce, &pce_cmds, "Client sets", cmd_common, 1, 0),
	SHELL_CMD_ARG(pse, &pse_cmds, "Server sets", cmd_common, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_ARG_REGISTER(pbap, &pbap_cmds, "Bluetooth pbap shell commands", cmd_common, 1, 1);
