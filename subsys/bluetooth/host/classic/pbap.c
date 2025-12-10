/* pbap.c - Phone Book Access Profile handling */

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

#include "psa/crypto.h"

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/goep.h>
#include <zephyr/bluetooth/classic/pbap.h>

#include "host/conn_internal.h"
#include "l2cap_br_internal.h"
#include "rfcomm_internal.h"
#include "obex_internal.h"

#define LOG_LEVEL CONFIG_BT_PBAP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_pbap);

#define PBAP_PWD_MAX_LENGTH 16U

NET_BUF_POOL_FIXED_DEFINE(bt_pbap_pool, CONFIG_BT_MAX_CONN,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_GOEP_RFCOMM_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

const uint8_t pbap_target_id[] = {0x79U, 0x61U, 0x35U, 0xf0U, 0xf0U, 0xc5U, 0x11U, 0xd8U,
				  0x09U, 0x66U, 0x08U, 0x00U, 0x20U, 0x0cU, 0x9aU, 0x66U};

#define PBAP_PCE_NAME_SUFFIX            ".vcf"
#define PBAP_PCE_VCARDENTRY_NAME_PREFIX "X-BT-UID:"

static int bt_pbap_generate_auth_challenge(const uint8_t *pwd, uint8_t *auth_chal_req);
static int bt_pbap_generate_auth_response(const uint8_t *pwd, uint8_t *auth_chal_req,
					  uint8_t *auth_chal_rsp);
static int bt_pbap_verify_auth(uint8_t *auth_chal_req, uint8_t *auth_chal_rsp, const uint8_t *pwd);

static struct bt_sdp_attribute pbap_pce_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(BT_SDP_ATTR_SVCLASS_ID_LIST,
		    /* ServiceClassIDList */
		    BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3), // 35 03
		    BT_SDP_DATA_ELEM_LIST(
			    {
				    BT_SDP_TYPE_SIZE(BT_SDP_UUID16),         // 19
				    BT_SDP_ARRAY_16(BT_SDP_PBAP_PCE_SVCLASS) // 11 2E
			    }, )),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST, BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8), // 35 08
		BT_SDP_DATA_ELEM_LIST({BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),         // 35 06
				       BT_SDP_DATA_ELEM_LIST(
					       {
						       BT_SDP_TYPE_SIZE(BT_SDP_UUID16),     // 19
						       BT_SDP_ARRAY_16(BT_SDP_PBAP_SVCLASS) // 11 30
					       },
					       {
						       BT_SDP_TYPE_SIZE(BT_SDP_UINT16), // 09
						       BT_SDP_ARRAY_16(0x0102U)         // 01 02
					       }, )}, )),
	BT_SDP_SERVICE_NAME("Phonebook Access PCE"),
};

static struct bt_sdp_record pbap_pce_rec = BT_SDP_RECORD(pbap_pce_attrs);

static bool endwith(char *str, char *suffix)
{
	uint8_t str_len;
	uint8_t suffix_len;

	if (str == NULL || suffix == NULL) {
		return false;
	}

	str_len = (uint8_t)strlen(str);
	suffix_len = (uint8_t)strlen(suffix);

	if (str_len < suffix_len) {
		return 0;
	}

	return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static bool startwith(char *str, char *prefix)
{
	uint8_t str_len;
	uint8_t prefix_len;

	if (str == NULL || prefix == NULL) {
		return false;
	}

	str_len = strlen(str);
	prefix_len = strlen(prefix);

	if (prefix_len > str_len) {
		return false;
	}

	return strncmp(str, prefix, prefix_len) == 0;
}

// int bt_pbap_pce_register(struct bt_pbap_pce *pbap_pce, struct bt_pbap_pce_cb *cb)
// {
// 	int err;

// 	if (cb == NULL) {
// 		LOG_ERR("Invalid callback parameter");
// 		return -EINVAL;
// 	}

// 	pbap_pce->cb = cb;

// 	err = bt_sdp_register_service(&pbap_pce_rec);
// 	if (err != 0) {
// 		LOG_WRN("Fail to register SDP service");
// 	}

// 	return err;
// }

static void pbap_goep_transport_connected(struct bt_conn *conn, struct bt_goep *goep)
{
	int err;
	struct net_buf *buf;
	struct bt_obex_tlv data;
	struct bt_pbap_pce *pbap_pce;

	if (goep == NULL) {
		LOG_ERR("Invalid GOEP parameter");
		return;
	}

	pbap_pce = CONTAINER_OF(goep, struct bt_pbap_pce, _goep);

	buf = bt_goep_create_pdu(&pbap_pce->_goep, &bt_pbap_pool);
	if (buf == NULL) {
		LOG_ERR("Fail to allocate tx buffer");
		return;
	}

	err = bt_obex_add_header_target(buf, (uint16_t)sizeof(pbap_target_id), pbap_target_id);
	if (err != 0) {
		LOG_WRN("Fail to add header target");
		net_buf_unref(buf);
		return;
	}

	pbap_pce->local_auth = false;
	if (pbap_pce->pwd != NULL) {
		err = bt_pbap_generate_auth_challenge(pbap_pce->pwd,
						      (uint8_t *)pbap_pce->_auth_challenge_nonce);
		if (err != 0) {
			LOG_ERR("Failed to generate auth challenge (err %d)", err);
			net_buf_unref(buf);
			return;
		}

		data.type = BT_OBEX_CHALLENGE_TAG_NONCE;
		data.data_len = BT_OBEX_CHALLENGE_TAG_NONCE_LEN;
		data.data = pbap_pce->_auth_challenge_nonce;
		err = bt_obex_add_header_auth_challenge(buf, 1U, &data);
		if (err != 0) {
			LOG_WRN("Fail to add auth_challenge");
			net_buf_unref(buf);
			return;
		}

		pbap_pce->local_auth = true;
	}

	if (pbap_pce->_peer_feature != 0) {
		uint32_t feature_value;

		feature_value = sys_get_be32((uint8_t *)&pbap_pce->_peer_feature);
		data.type = BT_PBAP_APPL_PARAM_TAG_ID_SUPPORTED_FEATURES;
		data.data_len = sizeof(feature_value);
		data.data = (uint8_t *)&feature_value;
		err = bt_obex_add_header_app_param(buf, 1U, &data);
		if (err != 0) {
			LOG_WRN("Fail to add support feature %d", err);
			net_buf_unref(buf);
			return;
		}
	} else {
		pbap_pce->_peer_feature = PSE_ASSUMED_SUPPORT_FEATURE;
	}

	/** IPhone issue */
	if (!pbap_pce->_goep._goep_v2 && pbap_pce->_mopl > pbap_pce->_goep.obex.rx.mtu - 1U) {
		LOG_DBG("Adjusting MPL from %u to %u for GOEP v1", pbap_pce->_mopl,
			pbap_pce->_goep.obex.rx.mtu - 1U);
		pbap_pce->_mopl = pbap_pce->_goep.obex.rx.mtu - 1U;
	}

	err = bt_obex_connect(&pbap_pce->_client, pbap_pce->_mopl, buf);
	if (err != 0) {
		net_buf_unref(buf);
		LOG_ERR("Fail to send conn req %d", err);
	}

	return;
}

static void pbap_goep_transport_disconnected(struct bt_goep *goep)
{
	struct bt_pbap_pce *pbap_pce;

	pbap_pce = CONTAINER_OF(goep, struct bt_pbap_pce, _goep);

	if (pbap_pce->cb->disconnect) {
		pbap_pce->cb->disconnect(pbap_pce, BT_PBAP_RSP_CODE_OK);
	}

	atomic_set(&pbap_pce->_state, BT_PBAP_DISCONNECTED);
}

static struct bt_goep_transport_ops pbap_goep_transport_ops = {
	.connected = pbap_goep_transport_connected,
	.disconnected = pbap_goep_transport_disconnected,
};

static bool bt_pbap_find_tlv_param_cb(struct bt_obex_tlv *hdr, void *user_data)
{
	struct bt_obex_tlv *tlv;

	tlv = (struct bt_obex_tlv *)user_data;

	if (hdr->type == tlv->type) {
		tlv->data = hdr->data;
		tlv->data_len = hdr->data_len;
		return false;
	}

	return true;
}

static void pbap_pce_connect(struct bt_obex_client *client, uint8_t rsp_code, uint8_t version,
			     uint16_t mopl, struct net_buf *buf)
{
	int err;
	uint16_t length = 0;
	const uint8_t *auth;
	uint8_t bt_auth_data[BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN] = {0};
	struct bt_obex_tlv bt_auth_challenge = {0};
	struct bt_obex_tlv bt_auth_response = {0};
	struct bt_pbap_pce *pbap_pce;
	struct net_buf *tx_buf;

	if (client == NULL) {
		LOG_ERR("Invalid client parameter");
		return;
	}

	pbap_pce = CONTAINER_OF(client, struct bt_pbap_pce, _client);

	err = bt_obex_get_header_conn_id(buf, &pbap_pce->_conn_id);
	if (err != 0) {
		LOG_ERR("Failed to get connection ID (err %d)", err);
		goto disconnect;
	}

	if (rsp_code == BT_PBAP_RSP_CODE_UNAUTH) {
		tx_buf = bt_goep_create_pdu(&pbap_pce->_goep, &bt_pbap_pool);
		if (tx_buf == NULL) {
			LOG_WRN("Fail to allocate tx buffer");
			goto disconnect;
		}

		err = bt_obex_get_header_auth_challenge(buf, &length, &auth);
		if (err != 0) {
			LOG_WRN("No available auth challenge");
			goto disconnect;
		}

		bt_auth_challenge.type = BT_OBEX_CHALLENGE_TAG_NONCE;
		bt_pbap_tlv_parse(length, auth, bt_pbap_find_tlv_param_cb, &bt_auth_challenge);

		if (pbap_pce->pwd == NULL) {
			if (pbap_pce->cb->get_auth_info != NULL) {
				pbap_pce->cb->get_auth_info(pbap_pce);
			} else {
				LOG_WRN("No available authication info");
				goto disconnect;
			}

			if (pbap_pce->pwd == NULL || strlen(pbap_pce->pwd) == 0 ||
			    strlen(pbap_pce->pwd) > PBAP_PWD_MAX_LENGTH) {
				LOG_WRN("No available pwd");
				goto disconnect;
			}
		}
		bt_auth_response.data = bt_auth_data;
		err = bt_pbap_generate_auth_response(pbap_pce->pwd,
						     (uint8_t *)bt_auth_challenge.data,
						     (uint8_t *)bt_auth_response.data);
		if (err != 0) {
			goto disconnect;
		}

		bt_auth_response.type = BT_OBEX_RESPONSE_TAG_REQ_DIGEST;
		bt_auth_response.data_len = BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN;
		err = bt_obex_add_header_auth_rsp(tx_buf, 1U, &bt_auth_response);
		if (err != 0) {
			LOG_WRN("Fail to add auth_challenge");
			goto disconnect;
		}

		if (pbap_pce->local_auth) {
			bt_auth_challenge.data = pbap_pce->_auth_challenge_nonce;

			err = bt_obex_add_header_auth_challenge(tx_buf, 1, &bt_auth_challenge);
			if (err != 0) {
				LOG_WRN("Fail to add auth_challenge");
				goto disconnect;
			}
		}

		err = bt_obex_connect(&pbap_pce->_client, pbap_pce->_mopl, tx_buf);
		if (err != 0) {
			LOG_WRN("Fail to send conn req %d", err);
			goto disconnect;
		}
	}

	if (pbap_pce->local_auth && rsp_code == BT_PBAP_RSP_CODE_OK) {
		err = bt_obex_get_header_auth_rsp(buf, &length, &auth);
		if (err != 0) {
			LOG_WRN("No available auth_response");
			goto disconnect;
		}
		bt_auth_response.type = BT_OBEX_RESPONSE_TAG_REQ_DIGEST;
		bt_pbap_tlv_parse(length, auth, bt_pbap_find_tlv_param_cb, &bt_auth_response);
		err = bt_pbap_verify_auth(pbap_pce->_auth_challenge_nonce,
					  (uint8_t *)bt_auth_response.data, pbap_pce->pwd);
		if (err == 0) {
			LOG_INF("auth success");
		} else {
			LOG_WRN("auth fail");
			goto disconnect;
		}
	}

	if (pbap_pce->cb->connect != NULL && rsp_code == BT_PBAP_RSP_CODE_OK) {
		pbap_pce->cb->connect(pbap_pce, rsp_code, version, mopl, buf);
		atomic_set(&pbap_pce->_state, BT_PBAP_CONNECTED);
		atomic_set(&pbap_pce->_state, BT_PBAP_IDEL);
	}
	return;

disconnect:
	net_buf_unref(buf);

	err = bt_pbap_pce_disconnect(pbap_pce, true);
	if (err != 0) {
		LOG_WRN("Fail to send disconnect command");
	}
	return;
}

static void pbap_pce_clear_pending_request(struct bt_pbap_pce *pbap_pce)
{
	pbap_pce->_rsp_cb = NULL;
	pbap_pce->_req_type = NULL;
}

static void pbap_pce_disconnect(struct bt_obex_client *client, uint8_t rsp_code,
				struct net_buf *buf)
{
	int err;
	struct bt_pbap_pce *pbap_pce;

	if (client == NULL) {
		LOG_ERR("Invalid client parameter");
		return;
	}

	pbap_pce = CONTAINER_OF(client, struct bt_pbap_pce, _client);

	if (rsp_code != BT_PBAP_RSP_CODE_OK) {
		pbap_pce_clear_pending_request(pbap_pce);
		if (pbap_pce->cb->disconnect) {
			pbap_pce->cb->disconnect(pbap_pce, rsp_code);
		}
		return;
	} else {
		if (pbap_pce->_goep._goep_v2) {
			err = bt_goep_transport_l2cap_disconnect(&(pbap_pce->_goep));
		} else {
			err = bt_goep_transport_rfcomm_disconnect(&(pbap_pce->_goep));
		}
		if (err) {
			LOG_WRN("Fail to disconnect pbap conn (err %d)", err);
		}
		return;
	}
	return;
}

static void pbap_pce_get_cb(struct bt_obex_client *client, uint8_t rsp_code, struct net_buf *buf)
{
	struct bt_pbap_pce *pbap_pce;
	uint8_t srm = 0x00;

	pbap_pce = CONTAINER_OF(client, struct bt_pbap_pce, _client);
	if (pbap_pce == NULL) {
		LOG_WRN("No available pbap_pce");
		return;
	}

	if (pbap_pce->_goep._goep_v2) {
		bt_obex_get_header_srm(buf, &srm);
		if (srm != 0x01) {
			LOG_WRN("Fail to get srm header");
		}
	}
	pbap_pce->_rsp_cb(pbap_pce, rsp_code, buf);

	if (rsp_code != BT_PBAP_RSP_CODE_CONTINUE) {
		pbap_pce_clear_pending_request(pbap_pce);
		atomic_set(&pbap_pce->_state, BT_PBAP_IDEL);
	}

	return;
}

static void pbap_pce_setpath(struct bt_obex_client *client, uint8_t rsp_code, struct net_buf *buf)
{

	struct bt_pbap_pce *pbap_pce;

	if (client == NULL) {
		LOG_ERR("Invalid client parameter");
		return;
	}

	pbap_pce = CONTAINER_OF(client, struct bt_pbap_pce, _client);

	if (pbap_pce->cb->set_path) {
		pbap_pce->cb->set_path(pbap_pce, rsp_code);
	}

	atomic_set(&pbap_pce->_state, BT_PBAP_IDEL);
}

struct bt_obex_client_ops pbap_pce_ops = {
	.connect = pbap_pce_connect,
	.disconnect = pbap_pce_disconnect,
	.get = pbap_pce_get_cb,
	.setpath = pbap_pce_setpath,
};

static int bt_pbap_pce_connect(struct bt_conn *conn, uint8_t channel, uint16_t psm,
			       struct bt_pbap_pce *pbap_pce, struct bt_pbap_pce_cb *cb)
{
	int err;

	if (cb == NULL) {
		LOG_ERR("Invalid callback parameter");
		return -EINVAL;
	}

	err = bt_sdp_register_service(&pbap_pce_rec);
	if (err != 0) {
		LOG_WRN("Fail to register SDP service");
	}

	if (conn == NULL) {
		LOG_WRN("Invalid connection");
		return -ENOTCONN;
	}

	if (pbap_pce == NULL) {
		LOG_WRN("No available pbap_pce");
		return -EINVAL;
	}

	if (pbap_pce->pwd != NULL && strlen(pbap_pce->pwd) > PBAP_PWD_MAX_LENGTH) {
		LOG_ERR("Password length exceeds maximum");
		return -EINVAL;
	}

	pbap_pce->_goep.transport_ops = &pbap_goep_transport_ops;
	pbap_pce->_client.ops = &pbap_pce_ops;
	pbap_pce->_client.obex = &pbap_pce->_goep.obex;

	if (channel != 0) {
		err = bt_goep_transport_rfcomm_connect(conn, &pbap_pce->_goep, channel);
		pbap_pce->_goep._goep_v2 = false;
	} else {
		err = bt_goep_transport_l2cap_connect(conn, &pbap_pce->_goep, psm);
		pbap_pce->_goep._goep_v2 = true;
	}

	if (err != 0) {
		LOG_ERR("Fail to connect (err %d)", err);
		return err;
	} else {
                pbap_pce->cb = cb;
		LOG_INF("PBAP connection pending");
	}

	atomic_set(&pbap_pce->_state, BT_PBAP_CONNECTING);

	return 0;
}

int bt_pbap_pce_rfcomm_connect(struct bt_conn *conn, uint8_t channel, struct bt_pbap_pce *pbap_pce,struct bt_pbap_pce_cb *cb)
{
	return bt_pbap_pce_connect(conn, channel, 0, pbap_pce, cb);
}

int bt_pbap_pce_l2cap_connect(struct bt_conn *conn, uint16_t psm, struct bt_pbap_pce *pbap_pce, struct bt_pbap_pce_cb *cb)
{
	return bt_pbap_pce_connect(conn, 0, psm, pbap_pce, cb);
}

int bt_pbap_pce_disconnect(struct bt_pbap_pce *pbap_pce, bool enforce)
{
	int err;

	if (pbap_pce == NULL) {
		LOG_WRN("No available pbap_pce");
		return -EINVAL;
	};

	if (enforce) {
		if (pbap_pce->_goep._goep_v2) {
			err = bt_goep_transport_l2cap_disconnect(&pbap_pce->_goep);
		} else {
			err = bt_goep_transport_rfcomm_disconnect(&pbap_pce->_goep);
		}
		if (err) {
			LOG_WRN("Fail to disconnect pbap conn (err %d)", err);
		}
	} else {
		err = bt_obex_disconnect(&pbap_pce->_client, NULL);
		if (err) {
			LOG_WRN("Fail to send disconn req %d", err);
		}
	}

	if (!err) {
		atomic_set(&pbap_pce->_state, BT_PBAP_DISCONNECTING);
	}
	return err;
}

static int pbap_check_conn_id(uint32_t id, struct net_buf *buf)
{
	uint32_t conn_id;
	int err;

	err = bt_obex_get_header_conn_id(buf, &conn_id);
	if (err != 0) {
		LOG_ERR("Failed to get conn id %d", err);
		return err;
	}

	if (conn_id != id) {
		LOG_ERR("Conn id is mismatched %u != %u", conn_id, id);
		return -EINVAL;
	}

	return 0;
}

static int pbap_check_srm(struct net_buf *buf)
{
	uint8_t srm;
	int err;

	err = bt_obex_get_header_srm(buf, &srm);
	if (err != 0) {
		LOG_ERR("Failed to get header SRM %d", err);
		return err;
	}

	if (0x01 != srm) {
		LOG_ERR("SRM  mismatched %u != 0x01", srm);
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
		LOG_ERR("Failed to get header SRMP %d", err);
		return err;
	}

	if (0x01 != srmp) {
		LOG_ERR("SRMP mismatched %u != 0x01", srmp);
		return -EINVAL;
	}

	return 0;
}

#define PBAP_REQUIRED_HDR(_count, _hdrs)                                                           \
	{                                                                                          \
		.count = (_count), .hdrs = (const uint8_t *)(_hdrs),                               \
	}

#define _PBAP_REQUIRED_HDR_LIST(...)                                                               \
	PBAP_REQUIRED_HDR(sizeof((uint8_t[]){__VA_ARGS__}), ((uint8_t[]){__VA_ARGS__}))

#define PBAP_REQUIRED_HDR_LIST(...) _PBAP_REQUIRED_HDR_LIST(__VA_ARGS__)

struct pbap_required_hdr {
	uint8_t count;
	const uint8_t *hdrs;
};

typedef void (*bt_pbap_pce_cb_t)(struct bt_pbap_pce *pbap_pce, uint8_t rsp_code,
				 struct net_buf *buf);

struct pbap_pull_function {
	const char *type;
	struct pbap_required_hdr req_hdr;
	// bt_bip_server_cb_t (*get_server_cb)(struct bt_bip_server *server);
	bt_pbap_pce_cb_t (*get_client_cb)(struct bt_pbap_pce *pbap_pce);
};

#define PULL_PHONEBOOK_REQUIRED_HDR                                                                \
	BT_OBEX_HEADER_ID_NAME, BT_OBEX_HEADER_ID_TYPE, BT_OBEX_HEADER_ID_CONN_ID

#define PULL_VCARDLISTING_REQUIRED_HDR                                                             \
	BT_OBEX_HEADER_ID_NAME, BT_OBEX_HEADER_ID_TYPE, BT_OBEX_HEADER_ID_CONN_ID

#define PULL_VCARDENTRY_REQUIRED_HDR                                                               \
	BT_OBEX_HEADER_ID_NAME, BT_OBEX_HEADER_ID_TYPE, BT_OBEX_HEADER_ID_CONN_ID

static bt_pbap_pce_cb_t pbap_pce_pull_phonebook_cb(struct bt_pbap_pce *pbap_pce)
{
	return pbap_pce->cb->pull_phonebook;
}

static bt_pbap_pce_cb_t pbap_pce_pull_vcardlisting_cb(struct bt_pbap_pce *pbap_pce)
{
	return pbap_pce->cb->pull_vcardlisting;
}

static bt_pbap_pce_cb_t pbap_pce_pull_vcardentry_cb(struct bt_pbap_pce *pbap_pce)
{
	return pbap_pce->cb->pull_vcardentry;
}

static struct pbap_pull_function pbap_pull_functions[] = {
	{BT_PBAP_PULL_PHONEBOOK_TYPE, PBAP_REQUIRED_HDR_LIST(PULL_PHONEBOOK_REQUIRED_HDR),
	 pbap_pce_pull_phonebook_cb},
	{BT_PBAP_PULL_VCARD_LISTING_TYPE, PBAP_REQUIRED_HDR_LIST(PULL_VCARDLISTING_REQUIRED_HDR),
	 pbap_pce_pull_vcardlisting_cb},
	{BT_PBAP_PULL_VCARD_ENTRY_TYPE, PBAP_REQUIRED_HDR_LIST(PULL_VCARDENTRY_REQUIRED_HDR),
	 pbap_pce_pull_vcardentry_cb},
};

static bool has_required_hdrs(struct net_buf *buf, const struct pbap_required_hdr *hdr)
{
	for (uint8_t index = 0; index < hdr->count; index++) {
		if (!bt_obex_has_header(buf, hdr->hdrs[index])) {
			return false;
		}
	}
	return true;
};

static int pbap_pce_get_req_cb(struct bt_pbap_pce *pbap_pce, const char *type, struct net_buf *buf,
			       bt_pbap_pce_cb_t *cb, const char **req_type)
{
	int err;
	uint16_t len;
	const uint8_t *type_data;

	if (pbap_pce == NULL) {
		LOG_ERR("Invalid pbap_pce handle");
		return -EINVAL;
	}

	err = bt_obex_get_header_type(buf, &len, &type_data);
	if (err != 0) {
		LOG_WRN("Failed to get type header %d", err);
		return -EINVAL;
	}

	if (len <= strlen(type_data)) {
		LOG_WRN("Invalid type string len %u <= %u", len, strlen(type_data));
		return -EINVAL;
	}

	*cb = NULL;
	ARRAY_FOR_EACH(pbap_pull_functions, i) {
		err = strcmp(pbap_pull_functions[i].type, type);
		if (err != 0) {
			continue;
		}
		err = strcmp(pbap_pull_functions[i].type, type_data);
		if (err != 0) {
			continue;
		}

		if (!has_required_hdrs(buf, &pbap_pull_functions[i].req_hdr)) {
			continue;
		}

		/* Application parameter tag id is not checked. */

		if (pbap_pull_functions[i].get_client_cb == NULL) {
			continue;
		}

		*cb = pbap_pull_functions[i].get_client_cb(pbap_pce);
		if (*cb == NULL) {
			continue;
		}

		if (req_type != NULL) {
			*req_type = pbap_pull_functions[i].type;
		}
		break;
	}

	if (*cb == NULL) {
		LOG_WRN("Unsupported request");
		return -EINVAL;
	}

	return 0;
}

static int pbap_pce_get(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, const char *type)
{
	int err = 0;
	bt_pbap_pce_cb_t cb;
	bt_pbap_pce_cb_t old_cb;
	const char *req_type;
	const char *old_req_type;

	if (pbap_pce == NULL || buf == NULL) {
		return -EINVAL;
	}

	if (atomic_get(&pbap_pce->_state) != BT_PBAP_IDEL) {
		LOG_ERR("Invalid state %u", (uint8_t)atomic_get(&pbap_pce->_state));
		return -EINVAL;
	}

	old_cb = pbap_pce->_rsp_cb;
	old_req_type = pbap_pce->_req_type;

	if (pbap_pce->_rsp_cb == NULL || bt_obex_has_header(buf, BT_OBEX_HEADER_ID_TYPE)) {
		err = pbap_pce_get_req_cb(pbap_pce, type, buf, &cb, &req_type);
		if (err != 0) {
			LOG_ERR("Invalid request %d", err);
			return err;
		}

		if (pbap_pce->_rsp_cb != NULL && cb != pbap_pce->_rsp_cb) {
			LOG_ERR("Previous operation is not completed");
			return -EINVAL;
		}

		pbap_pce->_rsp_cb = cb;
		pbap_pce->_req_type = req_type;
	}

	if (pbap_pce->_req_type != NULL && strcmp(pbap_pce->_req_type, type) != 0) {
		LOG_ERR("Invalid request type %s != %s", pbap_pce->_req_type, type);
		err = -EINVAL;
		goto failed;
	}
	if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_CONN_ID)) {
		err = pbap_check_conn_id(pbap_pce->_conn_id, buf);
		if (err != 0) {
			goto failed;
		}
	}

	if (pbap_pce->_goep._goep_v2) {
		if (!bt_obex_has_header(buf, BT_OBEX_HEADER_ID_SRM)) {
			LOG_ERR("No SRM header");
			err = -EINVAL;
			goto failed;
		} else if (pbap_check_srm(buf) != 0) {
			goto failed;
		}
		pbap_pce->_srmp = false;
		if (bt_obex_has_header(buf, BT_OBEX_HEADER_ID_SRM_PARAM)) {
			if (pbap_check_srmp(buf) == 0) {
				pbap_pce->_srmp = true;
			} else {
				goto failed;
			}
		}
	}

	err = bt_obex_get(&pbap_pce->_client, true, buf);

failed:
	if (err != 0) {
		pbap_pce->_rsp_cb = old_cb;
		pbap_pce->_req_type = old_req_type;
		LOG_ERR("Failed to send get req %d", err);
	}

	return err;
}

int bt_pbap_pce_pull_phonebook(struct bt_pbap_pce *pbap_pce, struct net_buf *buf)
{
	return pbap_pce_get(pbap_pce, buf, BT_PBAP_PULL_PHONEBOOK_TYPE);
}

int bt_pbap_pce_pull_vcardlisting(struct bt_pbap_pce *pbap_pce, struct net_buf *buf)
{
	return pbap_pce_get(pbap_pce, buf, BT_PBAP_PULL_VCARD_LISTING_TYPE);
}

int bt_pbap_pce_pull_vcardentry(struct bt_pbap_pce *pbap_pce, struct net_buf *buf)
{
	return pbap_pce_get(pbap_pce, buf, BT_PBAP_PULL_VCARD_ENTRY_TYPE);
}

int bt_pbap_pce_set_path(struct bt_pbap_pce *pbap_pce, uint8_t flags, struct net_buf *buf)
{
        int err;

        if (flags != BT_PBAP_SET_PATH_FLAGS_UP && flags != BT_PBAP_SET_PATH_FLAGS_DOWN_OR_ROOT) {
                LOG_ERR("Invalid flags %u", flags);
                return -EINVAL;
        }

        if (!bt_obex_has_header(buf, BT_OBEX_HEADER_ID_CONN_ID)) {
                LOG_ERR("No connection ID header");
                return -EINVAL;
        } else if (pbap_check_conn_id(pbap_pce->_conn_id, buf) != 0) {
                LOG_ERR("Failed to check connection ID");
                return -EINVAL;
        }

        err = bt_obex_setpath(&pbap_pce->_client, flags, buf);
	if (err != 0) {
		LOG_WRN("Fail to add header srm id %d", err);
	}

        return err;
}


struct net_buf *bt_pbap_pce_create_pdu(struct bt_pbap_pce *pbap_pce, struct net_buf_pool *pool)
{
	if (pool == NULL) {
		return bt_goep_create_pdu(&pbap_pce->_goep, &bt_pbap_pool);
	}
	return bt_goep_create_pdu(&(pbap_pce->_goep), pool);
}

static int bt_pbap_generate_auth_challenge(const uint8_t *pwd, uint8_t *auth_chal_req)
{
	int64_t timestamp = k_uptime_get();
	uint8_t hash_input[PBAP_PWD_MAX_LENGTH + 1U + sizeof(timestamp)] = {0};
	size_t len;
	uint16_t pwd_len;
	int32_t status = PSA_SUCCESS;

	if (pwd == NULL) {
		LOG_WRN("no available password");
		return -EINVAL;
	}

	if (auth_chal_req == NULL) {
		LOG_WRN("no available auth_chal_req");
		return -EINVAL;
	}
	pwd_len = strlen(pwd);
	if (pwd_len == 0 || pwd_len > PBAP_PWD_MAX_LENGTH) {
		LOG_ERR("Password is invalid");
		return -EINVAL;
	}

	memcpy(hash_input, &timestamp, sizeof(timestamp));
	hash_input[sizeof(timestamp)] = ':';
	memcpy(hash_input + sizeof(timestamp) + 1U, pwd, strlen(pwd));
	status = psa_hash_compute(PSA_ALG_MD5, (const unsigned char *)hash_input,
				  sizeof(timestamp) + 1U + pwd_len, auth_chal_req, 16U, &len);
	if (status != PSA_SUCCESS) {
		LOG_WRN("Genarate auth challenage failed %d", status);
		return status;
	}
	return 0;
}

static int bt_pbap_generate_auth_response(const uint8_t *pwd, uint8_t *auth_chal_req,
					  uint8_t *auth_chal_rsp)
{
	uint8_t hash_input[PBAP_PWD_MAX_LENGTH + BT_OBEX_CHALLENGE_TAG_NONCE_LEN + 1U] = {0};
	size_t len;
	uint16_t pwd_len;
	int32_t status = PSA_SUCCESS;

	if (pwd == NULL) {
		LOG_WRN("no available password");
		return -EINVAL;
	}

	if (auth_chal_req == NULL) {
		LOG_WRN("no available auth_chal_req");
		return -EINVAL;
	}

	if (auth_chal_rsp == NULL) {
		LOG_WRN("no available auth_chal_rsp");
		return -EINVAL;
	}

	pwd_len = strlen(pwd);
	if (pwd_len == 0 || pwd_len > PBAP_PWD_MAX_LENGTH) {
		LOG_ERR("Password is invalid");
		return -EINVAL;
	}

	memcpy(hash_input, auth_chal_req, BT_OBEX_CHALLENGE_TAG_NONCE_LEN);
	hash_input[BT_OBEX_CHALLENGE_TAG_NONCE_LEN] = ':';
	memcpy(hash_input + BT_OBEX_CHALLENGE_TAG_NONCE_LEN + 1U, pwd, pwd_len);

	status = psa_hash_compute(PSA_ALG_MD5, (const unsigned char *)hash_input,
				  BT_OBEX_CHALLENGE_TAG_NONCE_LEN + 1U + pwd_len, auth_chal_rsp,
				  BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN, &len);
	if (status != PSA_SUCCESS) {
		LOG_WRN("Generate auth response failed %d", status);
		return status;
	}
	return 0;
}

static int bt_pbap_verify_auth(uint8_t *auth_chal_req, uint8_t *auth_chal_rsp, const uint8_t *pwd)
{
	uint8_t result[BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN] = {0};
	int32_t status = PSA_SUCCESS;

	status = bt_pbap_generate_auth_response(pwd, auth_chal_req, (uint8_t *)&result);
	if (status == PSA_SUCCESS) {
		return memcmp(result, (const uint8_t *)auth_chal_rsp,
			      BT_OBEX_RESPONSE_TAG_REQ_DIGEST_LEN);
	}

	return status;
}

#define BT_PBAP_PSE_SUPPORTED_FEATURES 0x000003FF
#define BT_PBAP_PSE_SUPPORTED_REPOSITORIES  0x0F
#define BT_RFCOMM_CHAN_PBAP_PSE 9
#define BT_BR_PSM_PBAP_PSE 0x1003
#define BT_SDP_PROTP_OBEX  0x0008

static struct bt_sdp_attribute pbap_pse_attrs[] = {
    BT_SDP_NEW_SERVICE,
    /* ServiceClassIDList */
    BT_SDP_LIST(
        BT_SDP_ATTR_SVCLASS_ID_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
        BT_SDP_DATA_ELEM_LIST(
        {
            BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
            BT_SDP_ARRAY_16(BT_SDP_PBAP_PSE_SVCLASS)
        },
        )
    ),
    /* ProtocolDescriptorList */
    BT_SDP_LIST(
        BT_SDP_ATTR_PROTO_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 17), 
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
                BT_SDP_ARRAY_16(BT_SDP_PROTO_RFCOMM), 
            },
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UINT8), 
                BT_SDP_ARRAY_8(BT_RFCOMM_CHAN_PBAP_PSE) 
            },
            )
        },
        {
            BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
            BT_SDP_DATA_ELEM_LIST(
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
                BT_SDP_ARRAY_16(BT_SDP_PROTP_OBEX),
            },
            )
        },
        )
    ),
    /* BluetoothProfileDescriptorList */
    BT_SDP_LIST(
        BT_SDP_ATTR_PROFILE_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
        BT_SDP_DATA_ELEM_LIST(
        {
            BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
            BT_SDP_DATA_ELEM_LIST(
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UUID16), 
                BT_SDP_ARRAY_16(BT_SDP_PBAP_SVCLASS) 
            },
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UINT16), 
                BT_SDP_ARRAY_16(0x0102U) 
            },
            )
        },
        )
    ),
    BT_SDP_SERVICE_NAME("Phonebook Access PSE"),
     /* GoepL2CapPsm */
   BT_SDP_ATTR_GOEP_L2CAP_PSM,
   {
       BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
       BT_SDP_ARRAY_16(BT_BR_PSM_PBAP_PSE)
   },

    /* SupportedRepositories */
    BT_SDP_ATTR_SUPPORTED_REPOSITORIES,
    {
        BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
        BT_SDP_ARRAY_8(BT_PBAP_PSE_SUPPORTED_REPOSITORIES)
    },

    /* PBAP_PSE SupportedFeatures */
    BT_SDP_ATTR_PBAP_SUPPORTED_FEATURES,
    {
        BT_SDP_TYPE_SIZE(BT_SDP_UINT32),
        BT_SDP_ARRAY_32(BT_PBAP_PSE_SUPPORTED_FEATURES)
    },
};

static struct bt_sdp_record pbap_pse_rec = BT_SDP_RECORD(pbap_pse_attrs);

static void pse_server_connect(struct bt_obex_server *server, uint8_t version, uint16_t mopl,
				struct net_buf *buf)
{
        int rsp_code = BT_PBAP_RSP_CODE_SUCCESS;
        int err;
        struct net_buf *tx_buf;

        struct bt_pbap_pse *pbap_pse = CONTAINER_OF(server, struct bt_pbap_pse, _server);
	tx_buf = bt_goep_create_pdu(&pbap_pse->_goep, &bt_pbap_pool);
	if (tx_buf == NULL) {
		LOG_ERR("Fail to allocate tx buffer");
		return;
	}

        err = bt_obex_add_header_who(tx_buf, sizeof(pbap_target_id), pbap_target_id);
        if (err != 0) {
                LOG_ERR("Fail to add header who %d", err);
                goto failed;
        }

        err = bt_obex_add_header_conn_id(tx_buf, pbap_pse->_conn_id);
        if (err != 0) {
                LOG_ERR("Fail to add header conn id %d", err);
                goto failed;
        }


	rsp_code = pbap_pse->cb->connect(pbap_pse, version, mopl, buf);

        err = bt_obex_connect_rsp(server, rsp_code, pbap_pse->mopl, tx_buf);
        if (err != 0) {
                LOG_ERR("Failed to send connect response %d", err);
                goto failed;
        }

        return;

failed:
        net_buf_unref(tx_buf);
        return;
}

static void pse_server_disconnect(struct bt_obex_server *server, struct net_buf *buf)
{
        return;
}

struct bt_obex_server_ops pbap_pse_server_ops = {
	.connect = pse_server_connect,
	.disconnect = pse_server_disconnect,
	// .put = goep_server_put,
	// .get = goep_server_get,
	// .abort = goep_server_abort,
	// .setpath = goep_server_setpath,
	// .action = goep_server_action,
};


static void pse_transport_connected(struct bt_conn *conn, struct bt_goep *goep)
{
	LOG_INF("GOEP %p transport connected on %p", goep, conn);
}

static void pse_transport_disconnected(struct bt_goep *goep)
{
	LOG_INF("GOEP %p transport disconnected", goep);
}

struct bt_goep_transport_ops pse_transport_ops = {
	.connected = pse_transport_connected,
	.disconnected = pse_transport_disconnected,
};

#define PBAP_RFDCOMM_SERVER(server) CONTAINER_OF(server, struct bt_pbap_pse, transport_server.rfcomm_server)
static int pbap_pse_rfcomm_accept(struct bt_conn *conn, struct bt_goep_transport_rfcomm_server *server,
			 struct bt_goep **goep)
{
        struct bt_pbap_pse *pbap_pse;

        pbap_pse = PBAP_RFDCOMM_SERVER(server);
        pbap_pse->_goep.transport_ops = &pse_transport_ops;
	*goep = &pbap_pse->_goep;
	return 0;
}


int bt_pbap_pse_rfcomm_register(struct bt_pbap_pse *pbap_pse, struct bt_pbap_pse_cb *cb)
{
        int err;

        if (pbap_pse == NULL || cb == NULL) {
                LOG_ERR("Invalid parameters");
                return -EINVAL;
        }


        pbap_pse->transport_server.rfcomm_server.accept = &pbap_pse_rfcomm_accept;
        pbap_pse->transport_server.rfcomm_server.rfcomm.channel = BT_RFCOMM_CHAN_PBAP_PSE;
        pbap_pse->_server.ops = &pbap_pse_server_ops;
        pbap_pse->_server.obex = &pbap_pse->_goep.obex;
        pbap_pse->cb = cb;


	err = bt_sdp_register_service(&pbap_pse_rec);
	if (err != 0) {
		LOG_WRN("Fail to register SDP service");
	}

	err = bt_obex_server_register(&pbap_pse->_server, NULL);
	if (err != 0) {
		LOG_ERR("Fail to register obex server %d", err);
                return err;
	}

	err = bt_goep_transport_rfcomm_server_register(&pbap_pse->transport_server.rfcomm_server);
	if (err != 0) {
		LOG_ERR("Fail to register RFCOMM server (error %d)", err);
		pbap_pse->transport_server.rfcomm_server.rfcomm.channel = 0;
		return -ENOEXEC;
	}
}