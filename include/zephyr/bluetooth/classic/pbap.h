/* pbap.h - Phone Book Access Profile handling */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <zephyr/bluetooth/classic/obex.h>
#include <zephyr/bluetooth/classic/goep.h>

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_PBAP_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_PBAP_H_

/**
 * @brief Bluetooth APIs
 * @defgroup bluetooth Bluetooth APIs
 * @ingroup connectivity
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Phone Book Access Profile(PBAP)
 * @defgroup bt_pbap Phone Book Access Profile (PBAP)
 * @since 1.0
 * @version 1.0.0
 * @ingroup bluetooth
 * @{
 */

/** @brief PBAP response Code. */
enum __packed bt_pbap_rsp_code {
    /* Continue */
    BT_PBAP_RSP_CODE_CONTINUE = 0x90,
    /* OK */
    BT_PBAP_RSP_CODE_OK = 0xa0,
    /* Success */
    BT_PBAP_RSP_CODE_SUCCESS = 0xa0,
    /* Bad Request - server couldn’t understand request */
    BT_PBAP_RSP_CODE_BAD_REQ = 0xc0,
	/* Unauthorized */
	BT_PBAP_RSP_CODE_UNAUTH = 0xc1,
	/* Forbidden - operation is understood but refused */
	BT_PBAP_RSP_CODE_FORBIDDEN = 0xc3,
	/* Not Found */
	BT_PBAP_RSP_CODE_NOT_FOUND = 0xc4,
	/* Not Acceptable */
	BT_PBAP_RSP_CODE_NOT_ACCEPT = 0xc6,
	/* Precondition Failed */
	BT_PBAP_RSP_CODE_PRECON_FAIL = 0xcc,
    /* Not Implemented */
	BT_PBAP_RSP_CODE_NOT_IMPL = 0xd1,
	/* Service Unavailable */
	BT_PBAP_RSP_CODE_UNAVAIL = 0xd3,
};

/** @brief The tag id used in PBAP application parameters. */
enum __packed bt_pbap_appl_param_tag_id{
    /* Order */
    BT_PBAP_APPL_PARAM_TAG_ID_ORDER = 0x01,
    /* SearchValue */
    BT_PBAP_APPL_PARAM_TAG_ID_SEARCH_VALUE = 0x02,
    /* SearchProperty */
    BT_PBAP_APPL_PARAM_TAG_ID_SEARCH_PROPERTY = 0x03,
    /* MaxListCount */
    BT_PBAP_APPL_PARAM_TAG_ID_MAX_LIST_COUNT = 0x04,
    /* ListStartOffset */
    BT_PBAP_APPL_PARAM_TAG_ID_LIST_START_OFF_SET = 0x05,
    /* PropertySelector */
    BT_PBAP_APPL_PARAM_TAG_ID_PROPERTY_SELECTOR = 0x06,
    /* Format */
    BT_PBAP_APPL_PARAM_TAG_ID_FORMAT = 0x07,
    /* PhonebookSize */
    BT_PBAP_APPL_PARAM_TAG_ID_PHONEBOOK_SIZE = 0x08,
    /* NewMissedCalls */
    BT_PBAP_APPL_PARAM_TAG_ID_NEW_MISSED_CALLS = 0x09,
    /* PrimaryFolderVersion */
    BT_PBAP_APPL_PARAM_TAG_ID_PRIMARY_FOLDER_VERSION = 0x0A,
    /* SecondaryFolderVersion */
    BT_PBAP_APPL_PARAM_TAG_ID_SECONDARY_FOLDER_VERSION = 0x0B,
    /* vCardSelector */
    BT_PBAP_APPL_PARAM_TAG_ID_VCARD_SELECTOR = 0x0C,
    /* DatabaseIdentifier */
    BT_PBAP_APPL_PARAM_TAG_ID_DATABASE_IDENTIFIER = 0x0D,
    /* vCardSelectorOperator */
    BT_PBAP_APPL_PARAM_TAG_ID_VCARD_SELECTOR_OPERATOR = 0x0E,
    /* ResetNewMissedCalls */
    BT_PBAP_APPL_PARAM_TAG_ID_RESET_NEW_MISSED_CALLS = 0x0F,
    /* PbapSupportedFeatures */
    BT_PBAP_APPL_PARAM_TAG_ID_SUPPORTED_FEATURES = 0x10,
};


struct bt_pbap_appl_param{
    uint8_t id;
    uint8_t length;
    union {
        uint8_t data[0];
        uint8_t data_1b;
        uint16_t data_2b;
        uint32_t data_4b;
        uint64_t data_8b;
    }value;
}__packed;

/** @brief PBAP client PCE struct */
struct bt_pbap_pce{
    /** @brief connection handle */
    struct bt_conn *acl;
    /** @brief password for authication. When connecting, the application must provide this parameter
    *  when the component wants to authenticate with the server. 
    */
    char  *pwd;
    /** @brief peer device support features. After performing SDP to the server,
    *  the PCE can obtain the features supported by the PSE.
    */
    uint32_t peer_feature;
    /** @brief local device support feature. If PSE provide PCE, application should provide PCE features. */
    uint32_t lcl_feature;
    /** @brief max package length. When performing a connect operation, the application must provide this parameter.*/
    uint16_t mpl;
    /** @internal Save the current stats, @brief bt_pbap_state */
    atomic_t _state;
    /** @internal goep handle, @brief bt_goep */
    struct bt_goep *goep;
    /** @internal save authicathon_challenge nonce */
    uint8_t auth_chal[16];
    /** @internal Flag for local device if authicate actively. */
    bool local_auth;
    /** @internal Flag for peer device if authicate actively. */
    bool peer_auth;
};
/** @brief PBAP client PCE operations structure.
 * 
 * The object has to stay valid and constant for the lifetime of the PBAP client.
 */
struct bt_pbap_pce_cb{
    /** @brief PBAP PCE connect response callback
     * 
     * if this callback is provided it will be called when the PBAP connect response
     * is received.
     * 
     * @param pbap  The PBAP PCE object
     * @param mpl   The max package length of buf that application can use
     */

    void (*connect)(struct bt_pbap_pce *pbap, uint16_t mpl);

    /** @brief PBAP PCE disconnect response callback
     * 
     * if this callback is provided it will be called when the PBAP disconnect response
     * is received.
     * 
     * @param pbap The PBAP PCE object
     * @param rsp_code  Response code @ref bt_pbap_rsp_code
     */
    void (*disconnect)(struct bt_pbap_pce *pbap, uint8_t rsp_code);

    /** @brief PBAP PCE pull phonebook response callback
     * 
     * if this callback is provided it will be called when the PCE pull phonebook
     * response is received.
     * 
     * @param pbap The PBAP PCE object
     * @param rsp_code Response code @ref bt_pbap_rsp_code
     * @param buf Optional response headers
     */
    void (*pull_phonebook)(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf);

    /** @brief PBAP PCE pull vcardlisting response callback
     * 
     * if this callback is provided it will be called when the PCE pull vcardlisting
     * response is received.
     * 
     * @param pbap The PBAP PCE object
     * @param rsp_code Response code @ref bt_pbap_rsp_code
     * @param buf Optional response headers
     */
    void (*pull_vcardlisting)(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf);

    /** @brief PBAP PCE pull vcardentey response callback
     * 
     * if this callback is provided it will be called when the PCE pull vcardentey
     * response is received.
     * 
     * @param pbap The PBAP object
     * @param rsp_code Response code @ref bt_pbap_rsp_code
     * @param buf Optional response headers
     */
    void (*pull_vcardentry)(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf);

    /** @brief PBAP PCE set path response callback
     * 
     * if this callback is provided it will be called when the PCE set path
     * response is received.
     * 
     * @param pbap The PBAP object
     * @param rsp_code Response code @ref bt_pbap_rsp_code
     */
    void (*set_path)(struct bt_pbap_pce *pbap, uint8_t rsp_code);

    /** @brief PBAP PCE abort response callback
     * 
     * if this callback is provided it will be called when the PCE abort
     * response is received.
     * 
     * @param pbap The PBAP object
     * @param rsp_code Response code @ref bt_pbap_rsp_code
     */
    void (*abort)(struct bt_pbap_pce *pbap, uint8_t rsp_code);
};

enum __packed bt_pbap_state {
	/** PBAP disconnected */
	BT_PBAP_DISCONNECTED,
	/** PBAP in connecting state */
	BT_PBAP_CONNECTING,
	/** PBAP ready for upper layer traffic on it */
	BT_PBAP_CONNECTED,
	/** PBAP in disconnecting state */
	BT_PBAP_DISCONNECTING,
    /** PBAP in PULL Phonebook Function state  */
    BT_PBAP_PULL_PHONEBOOK,
    /** PBAP in Set Path Function state */
    B_PBAP_SET_PATH,
    /** PBAP in PULL Vcardlisting Function state  */
    BT_PBAP_PULL_VCARDLISTING,
    /** PBAP in PULL Vcardentry Function state  */
    BT_PBAP_PULL_VCARDENTRY,
    /** PBAP in idel state */
    BT_PBAP_IDEL,
};

/** @brief PBAP client PCE register.
 * 
 *  Register PCE application callback @brief bt_pbap_pce_cb.
 *  All other operations need to be performed after this function.
 * 
 *  @param cb PBAP client callback @ref bt_pbap_pce_cb.
 * 
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_pbap_pce_register(struct bt_pbap_pce_cb *cb);

/** @brief Allocate the buffer from given pool after reserving head room for PBAP client PCE.
 *
 *  For PBAP connection over RFCOMM, the reserved head room includes OBEX, RFCOMM, L2CAP and ACL
 *  headers.
 *  For PBAP connection over L2CAP, the reserved head room includes OBEX, L2CAP and ACL headers.
 *
 *  @param pbap_pce PBAP PCE object
 *  @param pool Which pool to take the buffer from
 *
 *  @return New buffer.
 */
struct net_buf *bt_pbap_create_pdu(struct bt_pbap_pce *pbap_pce, struct net_buf_pool *pool);

/** @brief PBAP client PCE connect PBAP server PSE over RFCOMM.
 *
 *  PBAP connect over RFCOMM, once the connection is completed, the callback
 *  @ref bt_pbap_pce::connected is called. If the connection is rejected,
 *  @ref bt_pbap_pce::disconnected callback is called instead.
 * 

 *  The Acl connection handle @brief bt_conn is passed as first parameter.
 *  The RFCOMM channel is passed as second paramter. The RFCOMM channel 
 *  of the PBAP server PSE can be obtained through SDP operation.
 * 
 *  The PBAP PCE object is passed (over an address of it) as third parameter, application should
 *  create PBAP PCE object @ref bt_pbap_pce. Then pass to this API the location (address).
 *  
 *  @param conn Acl connection handle
 *  @param channel RFCOMM channel
 *  @param pbap_pce PBAP PCE object @brief bt_pbap_pce
 *
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_pbap_pce_rfcomm_connect(struct bt_conn *conn, uint8_t channel, struct bt_pbap_pce *pbap_pce);

/** @brief PBAP client PCE connect PBAP server PSE over L2CAP.
 *
 *  PBAP connect over L2CAP, once the connection is completed, the callback
 *  @ref bt_pbap_pce::connected is called. If the connection is rejected,
 *  @ref bt_pbap_pce::disconnected callback is called instead.
 * 

 *  The Acl connection handle @brief bt_conn is passed as first parameter.
 *  The L2cap PSM is passed as second paramter. The L2cap PSM  
 *  of the PBAP server PSE can be obtained through SDP operation.
 * 
 *  The PBAP PCE object is passed (over an address of it) as third parameter, application should
 *  create PBAP PCE object @ref bt_pbap_pce. Then pass to this API the location (address).
 *  
 *  @param conn Acl connection handle
 *  @param psm  L2cap PSM
 *  @param pbap_pce PBAP PCE object @brief bt_pbap_pce
 *
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_pbap_pce_l2cap_connect(struct bt_conn *conn, uint16_t psm, struct bt_pbap_pce *pbap_pce);

/** @brief Disconnect PBAP connection from PBAP client PCE. */
/**
 *  Disconnect PBAP connection. 
 * 
 *  @param pbap_pce PBAP PCE object @brief bt_pbap_pce
 * 
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_pbap_pce_disconnect(struct bt_pbap_pce *pbap_pce);

/** @brief Disconnect PBAP connection from PBAP client PCE. */
/**
 *  Disconnect PBAP connection. 
 * 
 *  @param pbap_pce PBAP PCE object @brief bt_pbap_pce
 * 
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_pbap_pce_pull_phonebook_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);



int bt_pbap_pce_pull_vcardlisting_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);

int bt_pbap_pce_pull_vcardentry_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);

int bt_pbap_pce_send_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf);

int bt_pbap_pce_set_path(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name);


int bt_pbap_pce_abort(struct bt_pbap_pce *pbap_pce);

int bt_pbap_pce_get_body(struct net_buf *buf, uint16_t *len, uint8_t **body);

int bt_pbap_pce_get_end_body(struct net_buf *buf, uint16_t *len, uint8_t **body);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif
/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_PBAP_H_ */