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

struct bt_pbap_pce{
    struct bt_conn *acl;
    char  *pwd;
    uint32_t peer_feature;
    uint32_t lcl_feature;
    uint16_t mpl;
    /** @internal Save the current stats, @brief bt_pbap_state */
    atomic_t _state;
    /** @internal */
    struct bt_goep *goep;
};
/** @brief PBAP client operations structure.
 * 
 * The object has to stay valid and constant for the lifetime of the PBAP client.
 */
struct bt_pbap_pce_cb{
    /** @brief PBAP connect response callback
     * 
     * if this callback is provided it will be called when the PBAP connect response
     * is received.
     * 
     * @param pbap  The PBAP object
     * @param mpl   The max package length of buf that application can use
     */

    void (*connect)(struct bt_pbap_pce *pbap, uint16_t mpl);

    /** @brief PBAP disconnect response callback
     * 
     * if this callback is provided it will be called when the PBAP disconnect response
     * is received.
     * 
     * @param pbap The PBAP object
     * @param rsp_code  Response code @ref bt_pbap_rsp_code
     */
    void (*disconnect)(struct bt_pbap_pce *pbap, uint8_t rsp_code);

    /** @brief PBAP pull phonebook response callback
     * 
     * if this callback is provided it will be called when the PBAP pull phonebook
     * response is received.
     * 
     * @param pbap The PBAP object
     * @param rsp_code Response code @ref bt_pbap_rsp_code
     * @param buf Optional response headers
     */
    void (*pull_phonebook)(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf);

    void (*pull_vcardlisting)(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf);

    void (*pull_vcardentry)(struct bt_pbap_pce *pbap, uint8_t rsp_code, struct net_buf *buf);


    void (*set_path)(struct bt_pbap_pce *pbap, uint8_t rsp_code);
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

/** @brief PBAP struct */

/** @brief PBAP client register.
 * 
 * @param cb PBAP client callback @ref bt_pbap_pce_cb.
 * 
 * @return 0 in case of success or negative value in case of error.
 */
int bt_pbap_pce_register(struct bt_pbap_pce_cb *cb);

struct net_buf *bt_pbap_create_pdu(struct bt_pbap_pce *pbap_pce, struct net_buf_pool *pool);
/**
 * 
 */
int bt_pbap_pce_rfcomm_connect(struct bt_conn *conn, uint8_t channel, struct bt_pbap_pce *pbap_pce);

int bt_pbap_pce_l2cap_connect(struct bt_conn *conn, uint16_t psm, struct bt_pbap_pce *pbap_pce);

int bt_pbap_pce_pull_phonebook(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);

int bt_pbap_pce_pull_phonebook_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);

int bt_pbap_pce_set_path(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name);

int bt_pbap_pce_pull_vcardlisting_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);

int bt_pbap_pce_pull_vcardentry_create_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf, char *name, bool wait);

int bt_pbap_pce_send_cmd(struct bt_pbap_pce *pbap_pce, struct net_buf *buf);

int bt_pbap_pce_disconnect(struct bt_pbap_pce *pbap_pce);

int bt_pbap_pce_abort(struct bt_pbap_pce *pbap_pce);

int bt_pbap_pce_get_body(struct net_buf *buf, uint16_t *len, uint8_t **body);

int bt_pbap_pce_get_end_body(struct net_buf *buf, uint16_t *len, uint8_t **body);
/**
 * @}
 */


 struct xmd5context
 {
     uint32_t buf[4];
     uint32_t bytes[2];
     uint32_t in[16];
 };

#ifdef __cplusplus
}
#endif
/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_PBAP_H_ */