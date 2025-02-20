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

struct bt_pbap_pce_cb *bt_pce;

struct bt_pbap_goep{
    struct bt_goep _goep;
    struct bt_pbap_pce *_pbap;
    uint32_t conn_id;
};
static int bt_pbap_generate_auth_challenage(uint8_t *pwd, uint8_t *auth_chal_req);
static int bt_pbap_generate_auth_response(uint8_t *pwd, uint8_t *auth_chal_req, uint8_t *auth_chal_rsp);
static int bt_pbap_verify_auth(uint8_t *chal, uint8_t *rsp, uint8_t *pwd);

#define PBAP_APPL_PARAM_COUNT_MAX   10U
struct bt_pbap_appl_param appl_param[PBAP_APPL_PARAM_COUNT_MAX];

static struct bt_pbap_goep pbap_goep[CONFIG_BT_MAX_CONN];

NET_BUF_POOL_FIXED_DEFINE(bt_pbap_pce_pool, CONFIG_BT_MAX_CONN,
			  BT_RFCOMM_BUF_SIZE(CONFIG_BT_GOEP_RFCOMM_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

const uint8_t pbap_target_id[] = {0x79U, 0x61U, 0x35U, 0xf0U, 0xf0U, 0xc5U, 0x11U, 0xd8U,
                                0x09U, 0x66U, 0x08U, 0x00U, 0x20U, 0x0cU, 0x9aU, 0x66U};

const uint8_t phonebook_type[] = "x-bt/phonebook";
const uint8_t vcardlisting_type[] = "x-bt/vcard-listing";
const uint8_t vcardentry_type[] = "x-bt/vcard";

static struct bt_sdp_attribute pbap_pce_attrs[] = {
    BT_SDP_NEW_SERVICE,
        BT_SDP_LIST(
        BT_SDP_ATTR_SVCLASS_ID_LIST,
        /* ServiceClassIDList */
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3), //35 03
        BT_SDP_DATA_ELEM_LIST(
        {
            BT_SDP_TYPE_SIZE(BT_SDP_UUID16), //19
            BT_SDP_ARRAY_16(BT_SDP_PBAP_PCE_SVCLASS) //11 2E
        },
        )
    ),
    BT_SDP_LIST(
        BT_SDP_ATTR_PROFILE_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8), //35 08
        BT_SDP_DATA_ELEM_LIST(
        {
            BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6), //35 06
            BT_SDP_DATA_ELEM_LIST(
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UUID16), //19
                BT_SDP_ARRAY_16(BT_SDP_PBAP_SVCLASS) //11 30
            },
            {
                BT_SDP_TYPE_SIZE(BT_SDP_UINT16), //09
                BT_SDP_ARRAY_16(0x0102U) //01 02
            },
            )
        },
        )
    ),
    BT_SDP_SERVICE_NAME("Phonebook Access PCE"),
};

static struct bt_sdp_record pbap_pce_rec = BT_SDP_RECORD(pbap_pce_attrs);

static struct bt_pbap_goep *bt_goep_alloc(struct bt_conn *conn, struct bt_pbap_pce *pbap_pce)
{
    for (uint8_t index = 0; index < ARRAY_SIZE(pbap_goep); ++index){
        if (!pbap_goep[index]._goep._acl && !pbap_goep[index]._pbap){
            pbap_goep[index]._goep._acl = conn;
            pbap_goep[index]._pbap = pbap_pce;
            pbap_pce->goep = &pbap_goep[index]._goep;
            return &pbap_goep[index];
        }
    }
    return NULL;
}

static void bt_pbap_goep_release(struct bt_pbap_goep *pbap_pce_goep)
{
    pbap_pce_goep->_goep._acl = NULL;
    pbap_pce_goep->_pbap = NULL;
    return;
}

static struct bt_pbap_goep *bt_pbap_pce_lookup_by_conn_goep(struct bt_conn *conn, struct bt_goep *goep)
{
    for (uint8_t index = 0; index < ARRAY_SIZE(pbap_goep); ++index){
        if (&pbap_goep[index]._goep == goep && pbap_goep[index]._goep._acl == conn){
            return &pbap_goep[index];
        }
    }
    return NULL;
}

static struct bt_pbap_goep *bt_pbap_pce_lookup_obex(struct bt_obex *obex)
{
    for (uint8_t index = 0; index < ARRAY_SIZE(pbap_goep); ++index){
        if (&(pbap_goep[index]._goep.obex) == obex){
            return &pbap_goep[index];
        }
    }
    return NULL;
}
static uint16_t pbap_ascii_to_unicode(uint8_t *des, const uint8_t *src)
{
    uint32_t i = 0;

    if ((src == NULL) || (des == NULL))
    {
        return -EINVAL;
    }
    while (src[i] != 0x00U)
    {
        des[(i << 1U)]      = 0x00U;
        des[(i << 1U) + 1U] = src[i];
        i++;
    }

    des[(i << 1U)]      = 0x00U;
    des[(i << 1U) + 1U] = 0x00U; /* terminate with 0x00, 0x00 */
    return (i + 1) * 2;
}

static void bt_pbap_pce_init()
{
    bt_sdp_register_service(&pbap_pce_rec);
}

int bt_pbap_pce_register(struct bt_pbap_pce_cb *cb)
{
    if (!cb){
        return -EINVAL;
    }
    if (bt_pce){
		return -EALREADY;
    }

    bt_pce = cb;

    bt_pbap_pce_init();

    return 0;
}

static int pbap_organize_appl_param(struct bt_pbap_appl_param *appl_par, uint8_t tag_id, uint8_t length, uint8_t *data, uint16_t *total_length)
{
    uint16_t appl_param_length = *total_length;
    for (uint8_t i = 0; i < PBAP_APPL_PARAM_COUNT_MAX; i++){
        if (appl_par[i].id == 0){
            appl_par[i].id = tag_id;
            appl_par[i].length = length;
            memcpy(appl_par[i].value.data, data, length);
            appl_param_length += sizeof(uint8_t) + sizeof(uint8_t) + length;
            *total_length = appl_param_length;
            return 0;
        }
    }
    return -EAGAIN;
}

static void string_param(uint8_t *data, uint8_t *arry){
    struct bt_pbap_TLV auth[] = BT_PBAP_AUTH_CHAL(data);
    memset(arry, 0 ,24);
    uint8_t index = 0;
    for(uint8_t i = 0; i<3 ;i++){
        memcpy(&arry[index], &(auth[i].tag), 1);
        index += 1;
        memcpy(&arry[index], &(auth[i].length), 1); 
        index += 1;
        if (i > 0){
            memcpy(&arry[index], auth[i].data, 1);
            index += 1; 
        }
        else{
            memcpy(&arry[index], auth[i].data, strlen(auth[i].data));
            index += strlen(auth[i].data);
        }
    }
}


static void pbap_goep_transport_connected(struct bt_conn *conn, struct bt_goep *goep)
{
	LOG_INF("GOEP %p transport connected on %p", goep, conn);
    int err;
    uint16_t appl_param_len = 0;
    struct net_buf *buf;
    struct bt_pbap_goep *_pbap_goep;
    _pbap_goep = bt_pbap_pce_lookup_by_conn_goep(conn, goep);
    if (!_pbap_goep) {
        LOG_WRN("Invalid pbap_pce");
        return; 
    }

    buf = bt_goep_create_pdu(&_pbap_goep->_goep, &bt_pbap_pce_pool);
    if (!buf) {
        LOG_WRN("Fail to allocate tx buffer");
        return ;
    }

    err = bt_obex_add_header_target(buf, (uint16_t)sizeof(pbap_target_id), pbap_target_id);
    if (err){
        LOG_WRN("Fail to add header target");
        net_buf_unref(buf);
        return;
    }

    if (_pbap_goep->_pbap->pwd){
        bt_pbap_generate_auth_challenage(_pbap_goep->_pbap->pwd, _pbap_goep->_pbap->auth_chal); 
        uint8_t arry[16 + 2 + 3 +3] = {0};
        string_param(_pbap_goep->_pbap->auth_chal, arry); 
        err = bt_obex_add_header_auth_challenge(buf, sizeof(arry), arry);
        if (err){
            LOG_WRN("Fail to add auth_challenge");
            net_buf_unref(buf);
            return;
        }
        _pbap_goep->_pbap->local_auth = true;
    }

    if (_pbap_goep->_pbap->peer_feature){
        uint32_t value =  sys_get_be32((uint8_t *)&_pbap_goep->_pbap->peer_feature);
        err = pbap_organize_appl_param(appl_param, BT_PBAP_APPL_PARAM_TAG_ID_SUPPORTED_FEATURES, 4, (uint8_t *)&value, &appl_param_len);
        if (err){
            if (err){
                LOG_WRN("Fail to add appl_param supported feature %d", err);
                net_buf_unref(buf);
                return;
            }
        }
    }

    if (appl_param_len){
        err = bt_obex_add_header_app_param(buf, appl_param_len, (uint8_t *)appl_param);
        if (err){
            LOG_WRN("Fail to add appl_param %d", err);
            net_buf_unref(buf);
            return;
        }
    }

    err = bt_obex_connect(&_pbap_goep->_goep.obex, _pbap_goep->_pbap->mpl, buf);
    if (err < 0){
        net_buf_unref(buf);
        bt_pbap_goep_release(_pbap_goep);
        LOG_WRN("Fail to send conn req %d", err);
    }
    return;
}

static void pbap_goep_transport_disconnected(struct bt_goep *goep)
{
	LOG_INF("GOEP %p transport disconnected", goep);
    bt_pbap_goep_release(bt_pbap_pce_lookup_by_conn_goep(goep->_acl, goep));
}

static struct bt_goep_transport_ops pbap_goep_transport_ops = {
	.connected = pbap_goep_transport_connected,
	.disconnected = pbap_goep_transport_disconnected,
};

static bool bt_obex_find_tlv_param_cb(struct bt_pbap_TLV *hdr, void *user_data)
{
	struct bt_pbap_TLV *value;

	value = (struct bt_pbap_TLV *)user_data;

	if (hdr->tag == value->tag) {
		value->data = hdr->data;
		value->length = hdr->length;
		return false;
	}
	return true;
}
static int bt_pbap_get_head_param(uint8_t *buf, uint16_t length,
    bool (*func)(struct bt_obex_hdr *hdr, void *user_data), void *user_data)
{
    uint16_t len = 0;
    uint16_t total_len  = length;
    uint8_t header_id;
	uint8_t header_value_len;
    struct bt_pbap_TLV bt_param;
    if (!buf || !func){
        LOG_WRN("Invalid parameter");
		return -EINVAL;
    }
	while (len < total_len) {
		header_id = buf[len];
		len++;
        header_value_len = buf[len];
        len++;
		if ((len + header_value_len) > total_len) {
			return -EINVAL;
		}

		bt_param.tag = header_id;
		bt_param.data = &buf[len];
		bt_param.length = header_value_len;
		len += header_value_len;

		if (!func(&bt_param, user_data)) {
			return 0;
		}
	}
	return 0;
}


static void goep_client_connect(struct bt_obex *obex, uint8_t rsp_code, uint8_t version,
				uint16_t mopl, struct net_buf *buf)
{
	LOG_INF("OBEX %p conn rsq, rsp_code %s, version %02x, mopl %04x", obex,
		    bt_obex_rsp_code_to_str(rsp_code), version, mopl);

    int err;
    uint16_t length = 0;
    uint8_t *auth;
    struct bt_pbap_TLV bt_auth_param;
    struct bt_pbap_goep *_pbap_goep =  bt_pbap_pce_lookup_obex(obex);
    struct net_buf *tx_buf;

    if (!_pbap_goep){
        LOG_WRN("No available pbap_pce");
        return; 
    }
    err = bt_obex_get_header_conn_id(buf, & _pbap_goep->conn_id);
    if (err){
        LOG_WRN("No available connection id");
    }

    if (rsp_code == BT_PBAP_RSP_CODE_UNAUTH){
        tx_buf = bt_goep_create_pdu(&_pbap_goep->_goep, &bt_pbap_pce_pool);
        if (!tx_buf) {
            LOG_WRN("Fail to allocate tx buffer");
            goto failed ;
        }

        err = bt_obex_get_header_auth_challenge(buf, &length, &auth);
        if (err){
            LOG_WRN("No available auth_response");
            net_buf_unref(tx_buf);
            goto failed;
        }
        _pbap_goep->_pbap->peer_auth = true;

        bt_auth_param.tag = 0x00;
        bt_pbap_get_head_param(auth, length, bt_obex_find_tlv_param_cb, &bt_auth_param);
        uint8_t result[16] = {0};
        uint8_t arry[16 + 2 + 3 +3] = {0};

        // To do
        // when server auth and client do not provide pwd firstiy, callback connected or get_auth_info to accept pwd from application ?
        // if (!_pbap_goep->_pbap->pwd){
        //     bt_pce->connect();?
        //     bt_pce->get_auth_info();?
        // }

        bt_pbap_generate_auth_response(_pbap_goep->_pbap->pwd, bt_auth_param.data, result);

        string_param(result, arry);
        err = bt_obex_add_header_auth_rsp(tx_buf, sizeof(arry), arry);
        if (err){
            LOG_WRN("Fail to add auth_challenge");
            net_buf_unref(tx_buf);
            goto failed;
        }
        
        if (_pbap_goep->_pbap->local_auth){
            string_param(_pbap_goep->_pbap->auth_chal, arry);
            err = bt_obex_add_header_auth_challenge(tx_buf, sizeof(arry), arry);
            if (err){
                LOG_WRN("Fail to add auth_challenge");
                net_buf_unref(tx_buf);
                goto failed;
            }
        }

        err = bt_obex_connect(&_pbap_goep->_goep.obex, _pbap_goep->_pbap->mpl, tx_buf);
        if (err < 0){
            net_buf_unref(tx_buf);
            LOG_WRN("Fail to send conn req %d", err);
            goto failed;
        }
    }

    if (_pbap_goep->_pbap->local_auth && rsp_code == BT_PBAP_RSP_CODE_OK){
        err = bt_obex_get_header_auth_rsp(buf, &length, &auth);
        if (err){
            LOG_WRN("No available auth_response");
        }
        bt_auth_param.tag = 0x00;
        bt_pbap_get_head_param(auth, length, bt_obex_find_tlv_param_cb, &bt_auth_param);
        err = bt_pbap_verify_auth(_pbap_goep->_pbap->auth_chal, bt_auth_param.data, _pbap_goep->_pbap->pwd);
        if (!err){
            LOG_INF("auth success");
        }else{
            LOG_WRN("auth fail");
            err = bt_pbap_pce_disconnect(_pbap_goep->_pbap);
            if (err){
                LOG_WRN("Fail to send disconnect command");
            }
            return;
        }
    }


    if (bt_pce && bt_pce->connect && rsp_code == BT_PBAP_RSP_CODE_OK)
    {
        bt_pce->connect(_pbap_goep->_pbap, mopl);
    }
    if (rsp_code == BT_PBAP_RSP_CODE_OK){
        atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_CONNECTED);
        atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_IDEL);
    }
    return;

failed :
    net_buf_unref(buf);
    if (_pbap_goep->_goep._goep_v2){
        err = bt_goep_transport_l2cap_disconnect(&(_pbap_goep->_goep));
    }
    else{
        err = bt_goep_transport_rfcomm_disconnect(&(_pbap_goep->_goep));
    }
    if (err){
        LOG_WRN("Fail to disconnect pbap conn (err %d)", err);

    }
    return;

}

static void goep_client_disconnect(struct bt_obex *obex, uint8_t rsp_code, struct net_buf *buf)
{
	LOG_INF("OBEX %p disconn rsq, rsp_code %s", obex,
		    bt_obex_rsp_code_to_str(rsp_code));
    int err;
    struct bt_pbap_goep *_pbap_goep =  bt_pbap_pce_lookup_obex(obex);

    if (!_pbap_goep){
        LOG_WRN("No available pbap_pce");
        return; 
    }

    if (_pbap_goep->_goep._goep_v2){
        err = bt_goep_transport_l2cap_disconnect(&(_pbap_goep->_goep));
    }
    else{
        err = bt_goep_transport_rfcomm_disconnect(&(_pbap_goep->_goep));
    }
    if (err){
        LOG_WRN("Fail to disconnect pbap conn (err %d)", err);
        return;
    }
    if (bt_pce && bt_pce->disconnect)
    {
        bt_pce->disconnect(_pbap_goep->_pbap, rsp_code);
    }
    atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_DISCONNECTED);
    return ;

}

static void goep_client_get(struct bt_obex *obex, uint8_t rsp_code, struct net_buf *buf)
{
	LOG_INF("OBEX %p get rsq, rsp_code %s, data len %d", obex,
		    bt_obex_rsp_code_to_str(rsp_code), buf->len);
    struct bt_pbap_goep *_pbap_goep =  bt_pbap_pce_lookup_obex(obex);
    struct net_buf *tx_buf;
    int err = 0;

    if (!_pbap_goep){
        LOG_WRN("No available pbap_pce");
        return; 
    }

    switch (atomic_get(&_pbap_goep->_pbap->_state))
    {
        case BT_PBAP_PULL_PHONEBOOK:

            if (bt_pce && bt_pce->pull_phonebook)
            {
                bt_pce->pull_phonebook(_pbap_goep->_pbap, rsp_code, buf);
            }else{
                net_buf_unref(buf);
            }
            break;
        case BT_PBAP_PULL_VCARDLISTING:
            if (bt_pce && bt_pce->pull_vcardlisting)
            {
                bt_pce->pull_vcardlisting(_pbap_goep->_pbap, rsp_code, buf);
            }else{
                net_buf_unref(buf);
            }
            break;
        case BT_PBAP_PULL_VCARDENTRY:
            if (bt_pce && bt_pce->pull_vcardentry)
            {
                bt_pce->pull_vcardentry(_pbap_goep->_pbap, rsp_code, buf);
            }else{
                net_buf_unref(buf);
            }
            break;
        default:
            break;
    }

    if (!_pbap_goep->_goep._goep_v2 && rsp_code == BT_PBAP_RSP_CODE_CONTINUE){
        tx_buf = bt_goep_create_pdu(&(_pbap_goep->_goep), &bt_pbap_pce_pool);
        if (!tx_buf) {
            LOG_WRN("Fail to allocate tx buffer");
            atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_IDEL);
            return;
        }
        switch (atomic_get(&_pbap_goep->_pbap->_state))
        {
            case BT_PBAP_PULL_PHONEBOOK:
                err = bt_pbap_pce_pull_phonebook_create_cmd(_pbap_goep->_pbap, tx_buf, NULL, false);
                break;

            case BT_PBAP_PULL_VCARDLISTING:
                err = bt_pbap_pce_pull_vcardlisting_create_cmd(_pbap_goep->_pbap, tx_buf, NULL, false);
                break;
            
            case BT_PBAP_PULL_VCARDENTRY:
                err = bt_pbap_pce_pull_vcardentry_create_cmd(_pbap_goep->_pbap, tx_buf, NULL, false);
                break;
        }
        if (err){
            net_buf_unref(tx_buf);
            atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_IDEL);
            LOG_WRN("Fail create pull cmd  %d", err);
            return;
        }

        err = bt_pbap_pce_send_cmd(_pbap_goep->_pbap, tx_buf);
        if (err){
            net_buf_unref(tx_buf);
            atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_IDEL);
            LOG_WRN("Fail to send command %d",err);
        }
        return;
    }

    if (rsp_code != BT_PBAP_RSP_CODE_CONTINUE){
        atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_IDEL);
    }
}

void goep_client_setpath(struct bt_obex *obex, uint8_t rsp_code, struct net_buf *buf)
{
	LOG_INF("OBEX %p set path rsp_code %s", obex,
        bt_obex_rsp_code_to_str(rsp_code));

    struct bt_pbap_goep *_pbap_goep =  bt_pbap_pce_lookup_obex(obex);

    if (!_pbap_goep){
        LOG_WRN("No available pbap_pce");
        return; 
    }

    if (bt_pce && bt_pce->set_path)
    {
        bt_pce->set_path(_pbap_goep->_pbap, rsp_code);
    }
    atomic_set(&_pbap_goep->_pbap->_state, BT_PBAP_IDEL);
}


struct bt_obex_client_ops pbap_goep_client_ops = {
	.connect = goep_client_connect,
	.disconnect = goep_client_disconnect,
	.get = goep_client_get,
	.setpath = goep_client_setpath,
};

int bt_pbap_pce_rfcomm_connect(struct bt_conn *conn, uint8_t channel, struct bt_pbap_pce *pbap_pce)
{
    int err;
    struct bt_pbap_goep *_pbap_goep;

    if (!conn) {
		LOG_WRN("Invalid connection");
		return -ENOTCONN;
	}

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    if(!bt_pce){
        LOG_WRN("No available bt_pce ");
        return -EINVAL; 
    }

    pbap_pce->acl = conn;

    _pbap_goep = bt_goep_alloc(conn, pbap_pce);
    if (!_pbap_goep)
    {
        LOG_WRN("No available _goep");
        return -EINVAL; 
    }

    _pbap_goep->_goep.transport_ops = &pbap_goep_transport_ops;
    _pbap_goep->_goep.obex.client_ops = &pbap_goep_client_ops;

	err = bt_goep_transport_rfcomm_connect(conn, &_pbap_goep->_goep, channel);
	if (err) {
        LOG_WRN("Fail to connect to channel %d (err %d)", channel, err);
		bt_pbap_goep_release(pbap_goep);
	} else {
		LOG_INF("PBAP RFCOMM connection pending");
	}
    atomic_set(&pbap_pce->_state, BT_PBAP_CONNECTING);
	return err;
}

int bt_pbap_pce_l2cap_connect(struct bt_conn *conn, uint16_t psm, struct bt_pbap_pce *pbap_pce)
{
    int err;
    struct bt_pbap_goep *_pbap_goep;

    if (!conn) {
		LOG_WRN("Invalid connection");
		return -ENOTCONN;
	}

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    if(!bt_pce){
        LOG_WRN("No available bt_pce ");
        return -EINVAL; 
    }

    pbap_pce->acl = conn;

    _pbap_goep = bt_goep_alloc(conn, pbap_pce);
    if (!_pbap_goep)
    {
        LOG_WRN("No available _goep");
        return -EINVAL; 
    }

    _pbap_goep->_goep.transport_ops = &pbap_goep_transport_ops;
    _pbap_goep->_goep.obex.client_ops = &pbap_goep_client_ops;

	err = bt_goep_transport_l2cap_connect(conn, &_pbap_goep->_goep, psm);
	if (err) {
        LOG_WRN("Fail to connect to psm %d (err %d)", psm, err);
		bt_pbap_goep_release(pbap_goep);
	} else {
		LOG_INF("PBAP RFCOMM connection pending");
	}
    atomic_set(&pbap_pce->_state, BT_PBAP_CONNECTING);
	return err;
}

int bt_pbap_pce_disconnect(struct bt_pbap_pce *pbap_pce)
{
    int err;

    if (!pbap_pce) {
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    err = bt_obex_disconnect(&(pbap_pce->goep->obex), NULL);
	if (err) {
		LOG_WRN("Fail to send disconn req %d", err);
	}
	return err;
}

int bt_pbap_pce_pull_phonebook_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait)
{
    int err;
    uint16_t length;
    uint8_t unicode_trans[50] = {0};
    struct bt_pbap_goep *_pbap_goep;
    _pbap_goep = bt_pbap_pce_lookup_by_conn_goep(pbap_pce->acl, pbap_pce->goep);

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    err = bt_obex_add_header_conn_id(buf, _pbap_goep->conn_id);
    if (err){
        LOG_WRN("Fail to add header connectiond id %d", err);
        return err;
    }

    if (pbap_pce->goep->_goep_v2){
        err = bt_obex_add_header_srm(buf, 0x01);
        if (err){
            LOG_WRN("Fail to add header srm id %d", err);
            return err;
        }else if (wait){
            err =  bt_obex_add_header_srm_param(buf, 0x01);
            if (err){
                LOG_WRN("Fail to add header srm param id %d", err);
                return err; 
            }
        }
    }

    if (atomic_get(&pbap_pce->_state) == BT_PBAP_IDEL){
        err = bt_obex_add_header_type(buf, (uint16_t)strlen(phonebook_type), phonebook_type);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }
    
        memset(unicode_trans, 0, sizeof(unicode_trans));
        if (!name){
            LOG_WRN("No available name");
            return -EINVAL;
        }
        length = pbap_ascii_to_unicode((uint8_t *)unicode_trans, (uint8_t *)name);
        err = bt_obex_add_header_name(buf, length, unicode_trans);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }
    }

    atomic_set(&pbap_pce->_state, BT_PBAP_PULL_PHONEBOOK);

    return err;
}

int bt_pbap_pce_set_path(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name)
{
    int err;
    uint16_t length;
    uint8_t Flags;
    char *path_name = NULL;
    struct bt_pbap_goep *_pbap_goep;
    uint8_t unicode_trans[50] = {0};

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    if (atomic_get(&pbap_pce->_state) != BT_PBAP_IDEL){
        LOG_WRN("Operation inprogress");
		return -EINPROGRESS;
    }

    _pbap_goep = bt_pbap_pce_lookup_by_conn_goep(pbap_pce->acl, pbap_pce->goep);
    if (!_pbap_goep)
    {
        LOG_WRN("No available _pbap_goep");
        return -EINVAL; 
    }

    length = (uint16_t)strlen(name);
    if (length == 1U && strcmp(name, "/") == 0)
    {
        Flags = 0x2U;
        path_name = NULL;
    }
    else if (length == 2U && strcmp(name, "..") == 0)
    {
        Flags = 0x3U;
        path_name = NULL;
    }
    else
    {
        if (name != NULL && name[0] == '.' && name[1] == '/')
        {
            Flags = 0x2U;
            path_name = name + 2U;
        }
        else{
            LOG_WRN("No available name");
            return -EINVAL; 
        }
    }

    err = bt_obex_add_header_conn_id(buf, _pbap_goep->conn_id);
    if (err){
        LOG_WRN("Fail to add header connectiond id %d", err);
        return err;
    }

    if(path_name){
        length = pbap_ascii_to_unicode((uint8_t *)unicode_trans, (uint8_t *)path_name);
        err = bt_obex_add_header_name(buf, length, unicode_trans);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }
    }

    err = bt_obex_setpath(&(pbap_pce->goep->obex), Flags, buf);
    if(err){
        LOG_WRN("Fail to add header srm id %d", err);
        return err;
    }

    atomic_set(&pbap_pce->_state, B_PBAP_SET_PATH);

    return err;
}

int bt_pbap_pce_pull_vcardlisting_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait)
{
    int err;
    uint16_t length;
    uint8_t unicode_trans[50] = {0};
    struct bt_pbap_goep *_pbap_goep;
    _pbap_goep = bt_pbap_pce_lookup_by_conn_goep(pbap_pce->acl, pbap_pce->goep);

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    err = bt_obex_add_header_conn_id(buf, _pbap_goep->conn_id);
    if (err){
        LOG_WRN("Fail to add header connectiond id %d", err);
        return err;
    }

    if (pbap_pce->goep->_goep_v2){
        err = bt_obex_add_header_srm(buf, 0x01);
        if (err){
            LOG_WRN("Fail to add header srm id %d", err);
            return err;
        }else if (wait){
            err =  bt_obex_add_header_srm_param(buf, 0x01);
            if (err){
                LOG_WRN("Fail to add header srm param id %d", err);
                return err; 
            }
        }
    }

    if (atomic_get(&pbap_pce->_state) == BT_PBAP_IDEL){
        err = bt_obex_add_header_type(buf, (uint16_t)strlen(vcardlisting_type), vcardlisting_type);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }

        memset(unicode_trans, 0, sizeof(unicode_trans));
        length = 0;
        if (name){
            length = pbap_ascii_to_unicode((uint8_t *)unicode_trans, (uint8_t *)name);
        }

        err = bt_obex_add_header_name(buf, length, unicode_trans);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }
    }
    
    atomic_set(&pbap_pce->_state, BT_PBAP_PULL_VCARDLISTING);

    return err;
}

int bt_pbap_pce_pull_vcardentry_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait)
{
    int err;
    uint16_t length;
    uint8_t unicode_trans[50] = {0};
    struct bt_pbap_goep *_pbap_goep;
    _pbap_goep = bt_pbap_pce_lookup_by_conn_goep(pbap_pce->acl, pbap_pce->goep);

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    err = bt_obex_add_header_conn_id(buf, _pbap_goep->conn_id);
    if (err){
        LOG_WRN("Fail to add header connectiond id %d", err);
        return err;
    }

    if (pbap_pce->goep->_goep_v2){
        err = bt_obex_add_header_srm(buf, 0x01);
        if (err){
            LOG_WRN("Fail to add header srm id %d", err);
            return err;
        }else if (wait){
            err =  bt_obex_add_header_srm_param(buf, 0x01);
            if (err){
                LOG_WRN("Fail to add header srm param id %d", err);
                return err; 
            }
        }
    }

    if (atomic_get(&pbap_pce->_state) == BT_PBAP_IDEL){
        err = bt_obex_add_header_type(buf, (uint16_t)strlen(vcardentry_type), vcardentry_type);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }

        memset(unicode_trans, 0, sizeof(unicode_trans));
        if (!name){
            LOG_WRN("No available name");
            return -EINVAL;
        }
        length = pbap_ascii_to_unicode((uint8_t *)unicode_trans, (uint8_t *)name);
        err = bt_obex_add_header_name(buf, length, unicode_trans);
        if (err){
            LOG_WRN("Fail to add header name %d", err);
            return err;
        }
    }

    atomic_set(&pbap_pce->_state, BT_PBAP_PULL_VCARDENTRY);

    return err;
}

int bt_pbap_pce_abort(struct bt_pbap_pce *pbap_pce)
{
    int err;
    struct net_buf* buf;

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    buf = bt_goep_create_pdu(pbap_pce->goep, &bt_pbap_pce_pool);
    if (!buf) {
        LOG_WRN("Fail to allocate GOEP buffer");
        return -ENOMEM;
    }

    err = bt_obex_abort(&(pbap_pce->goep->obex), buf);
    if (err){
        LOG_WRN("Fail to send abort req %d", err);
    }
    net_buf_unref(buf);
    return err;
}

int bt_pbap_pce_send_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf)
{
    int err;

    if (!pbap_pce){
        LOG_WRN("No available pbap_pce");
        return -EINVAL; 
    }

    if (!buf) {
        LOG_WRN("No available buffer");
        return -ENOMEM;
    }

    err = bt_obex_get(&(pbap_pce->goep->obex), true, buf);
    if (err) {
        atomic_set(&pbap_pce->_state, BT_PBAP_IDEL);
        LOG_WRN("Fail to send get req %d", err);
    }
    return err;
}


int bt_pbap_pce_get_body(struct net_buf *buf, uint16_t *len, uint8_t **body)
{
    int err;
    err = bt_obex_get_header_body(buf, len, body);
    if (err){
        LOG_WRN("Fail get header body %d", err);
    }
    return err;  
}

int bt_pbap_pce_get_end_body(struct net_buf *buf, uint16_t *len, uint8_t **body){
    int err;
    err = bt_obex_get_header_end_body(buf, len, body);
    if (err){
        LOG_WRN("Fail get header end body %d", err);
    }
    return err;  
}

struct net_buf *bt_pbap_create_pdu(struct bt_pbap_pce *pbap_pce, struct net_buf_pool *pool)
{
    return bt_goep_create_pdu(pbap_pce->goep, pool);
}





/* MD5 */

/* The four core functions - F1 is optimized somewhat */
/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))
/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f,w,x,y,z,in,s)  (w += f(x,y,z) + in, w = (w<<s | w>>(32-s)) + x)

/*
* Shuffle the bytes into little-endian order within words, as per the
* MD5 spec. Note: this code works regardless of the byte order.
*/
void byteSwap(uint32_t *buf, unsigned words)
{
    uint8_t *p = (uint8_t *)buf;
    do {
        *buf++ = (uint32_t)((unsigned)p[3] << 8 | p[2]) << 16 |
                 ((unsigned)p[1] << 8 | p[0]);
        p += 4;
    } while (--words);
}



void xMD5Transform(uint32_t buf[4], uint32_t const in[16])
{
    register uint32_t a, b, c, d;
    a = buf[0];
    b = buf[1];
    c = buf[2];
    d = buf[3];
    MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
    MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
    MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
    MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
    MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
    MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
    MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
    MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
    MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
    MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
    MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
    MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
    MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
    MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
    MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
    MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);
    MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
    MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
    MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
    MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
    MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
    MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
    MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
    MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
    MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
    MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
    MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
    MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
    MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
    MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
    MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
    MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);
    MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
    MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
    MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
    MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
    MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
    MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
    MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
    MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
    MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
    MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
    MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
    MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
    MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
    MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
    MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
    MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);
    MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
    MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
    MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
    MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
    MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
    MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
    MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
    MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
    MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
    MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
    MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
    MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
    MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
    MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
    MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
    MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}

/*
* Start MD5 accumulation. Set bit count to 0 and buffer to mysterious
* initialization constants.
*/
void xMD5Init(struct xmd5context *ctx)
{
    ctx->buf[0] = 0x67452301;
    ctx->buf[1] = 0xefcdab89;
    ctx->buf[2] = 0x98badcfe;
    ctx->buf[3] = 0x10325476;
    ctx->bytes[0] = 0;
    ctx->bytes[1] = 0;
}

/*
* Update context to reflect the concatenation of another buffer full
* of bytes.
*/
void xMD5Update(struct xmd5context *ctx, uint8_t const *buf, int len)
{
    uint32_t t;
    /* Update byte count */
    t = ctx->bytes[0];
    ctx->bytes[0] += len;
    if (ctx->bytes[0] < t){
        ctx->bytes[1]++;
    }
    /* Carry from low to high */
    t = 64U - (t & 0x3fU); /* Space avail in ctx->in (at least 1) */

    if ((uint32_t)t > len) {
        memcpy((uint8_t *)ctx->in + 64U - (unsigned)t, (uint8_t *)buf, len);
        return;
    }
    /* First chunk is an odd size */
    memcpy((uint8_t *)ctx->in + 64U - (unsigned)t, (uint8_t *)buf, (unsigned)t);
    byteSwap(ctx->in, 16);
    xMD5Transform(ctx->buf, ctx->in);
    buf += (unsigned)t;
    len -= (unsigned)t;
    /* Process data in 64-byte chunks */
    while (len >= 64U) {
        memcpy(ctx->in, buf, 64U);
        byteSwap(ctx->in, 16U);
        xMD5Transform(ctx->buf, ctx->in);
        buf += 64U;
        len -= 64U;
    }
    /* Handle any remaining bytes of data. */ 
    memcpy(ctx->in, (uint8_t *)buf, len);
}

/*
* Final wrapup - pad to 64-byte boundary with the bit pattern
* 1 0* (64-bit count of bits processed, MSB-first)
*/
void xMD5Final(uint8_t digest[16], struct xmd5context *ctx)
{
    int count = (int)(ctx->bytes[0] & 0x3fU); /* Bytes in ctx->in */
    uint8_t *p = (uint8_t *)ctx->in + count;
    /* First unused byte */
    /* Set the first char of padding to 0x80. There is always room.*/
    *p++ = 0x80;
    /* Bytes of padding needed to make 56 bytes (-8..55) */
    count = 56 - 1 - count;
    if (count < 0) {
        /* Padding forces an extra block */
        memset(p, 0u, count+8);
        byteSwap(ctx->in, 16);
        xMD5Transform(ctx->buf, ctx->in);
        p = (uint8_t *)ctx->in;
        count = 56;
    }
    memset(p, 0U, count+8);
    byteSwap(ctx->in, 14);
    /* Append length in bits and transform */
    ctx->in[14] = ctx->bytes[0] << 3;
    ctx->in[15] = ctx->bytes[1] << 3 | ctx->bytes[0] >> 29;
    xMD5Transform(ctx->buf, ctx->in);
    byteSwap(ctx->buf, 4);
    memcpy(digest, ctx->buf, 16);
    memset(ctx, 0U, sizeof(ctx));
}

void MD5(void *dest, void *orig, int len)
{
    struct xmd5context context;
    xMD5Init(&context);
    xMD5Update(&context, orig, len);
    xMD5Final(dest, &context);
}

#define PBAP_PWD_MAX_LENGTH    50
static int bt_pbap_generate_auth_challenage(uint8_t *pwd, uint8_t *auth_chal_req)
{
    uint8_t *key = "Randomize me";
    uint8_t h[50 + 1 + 1] = {0};
    if (!pwd){
        LOG_WRN("no available password");
        return -EINVAL;
    }

    if (!auth_chal_req){
        LOG_WRN("no available auth_chal_req");
        return -EINVAL;
    }
    uint16_t pwd_len = strlen(pwd);
    uint16_t key_len = strlen(key);
    memcpy(h, key, key_len);
    h[key_len] = ':';
    memcpy(h + key_len + 1, pwd, pwd_len);
    MD5(auth_chal_req, h, strlen(h));
    return 0;
}

static int bt_pbap_generate_auth_response(uint8_t *pwd, uint8_t *auth_chal_req, uint8_t *auth_chal_rsp)
{
    uint8_t h[50 + 1 + 1] = {0};
    if (!pwd){
        LOG_WRN("no available password");
        return -EINVAL;
    }

    if (!auth_chal_req){
        LOG_WRN("no available auth_chal_req");
        return -EINVAL;
    }

    if (!auth_chal_rsp){
        LOG_WRN("no available auth_chal_rsp");
        return -EINVAL;
    }

    memcpy(h, auth_chal_req, 16);
    h[16] = ':';
    memcpy(h + 17, pwd, strlen(pwd));
    MD5(auth_chal_rsp, h, strlen(h));
}

static int bt_pbap_verify_auth(uint8_t *chal, uint8_t *rsp, uint8_t *pwd)
{
    uint8_t result[16] = {0};
    bt_pbap_generate_auth_response(pwd, chal, result);

    return memcmp(result, rsp, 16);

}