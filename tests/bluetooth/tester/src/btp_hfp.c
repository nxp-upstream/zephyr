/* btp_hfp.c - Bluetooth HFP Tester */

/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "btp/btp.h"

#define LOG_MODULE_NAME bttester_hfp
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

static volatile uint8_t hf_check_signal_strength;
static uint8_t hfp_in_calling_status = 0xff;
uint8_t call_active = 0;
static bool audio_conn_created;
/* Store HFP connection for later use */
struct bt_conn *hf_conn;

struct bt_hfp_hf *hfp_hf;
struct bt_conn *hf_sco_conn;
static struct bt_hfp_hf_call *hfp_hf_call[CONFIG_BT_HFP_HF_MAX_CALLS];

struct bt_hfp_ag *hfp_ag;
struct bt_hfp_ag *hfp_ag_ongoing;
struct bt_conn *hfp_ag_sco_conn;
static struct bt_hfp_ag_call *hfp_ag_call[CONFIG_BT_HFP_AG_MAX_CALLS];

static uint32_t conn_count = 0;
// static struct bt_hfp_ag_ongoing_call ag_ongoing_call_info[CONFIG_BT_HFP_AG_MAX_CALLS];

static size_t ag_ongoing_calls;

static bool has_ongoing_calls;


static void ag_add_a_call(struct bt_hfp_ag_call *call)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (!hfp_ag_call[i]) {
			hfp_ag_call[i] = call;
			return;
		}
	}
}

static void ag_remove_a_call(struct bt_hfp_ag_call *call)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (call == hfp_ag_call[i]) {
			hfp_ag_call[i] = NULL;
			return;
		}
	}
}

static void ag_remove_calls(void)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (hfp_ag_call[i] != NULL) {
			hfp_ag_call[i] = NULL;
		}
	}
}

static void ag_connected(struct bt_conn *conn, struct bt_hfp_ag *ag)
{
	hfp_ag = ag;
	LOG_DBG("AG connected");
}

static void ag_disconnected(struct bt_hfp_ag *ag)
{
	ag_remove_calls();
	LOG_DBG("AG disconnected");
}

static void ag_sco_connected(struct bt_hfp_ag *ag, struct bt_conn *sco_conn)
{
	struct btp_hfp_sco_connected_ev ev;

	if (hfp_ag_sco_conn != NULL) {
		LOG_ERR("AG SCO conn %p exists", hfp_ag_sco_conn);
		return;
	}

	audio_conn_created = true;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_SCO_CONNECTED, &ev, sizeof(ev));

	hfp_ag_sco_conn = bt_conn_ref(sco_conn);
}

static void ag_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
	struct btp_hfp_sco_disconnected_ev ev;

	if (hfp_ag_sco_conn == sco_conn) {
		bt_conn_unref(hfp_ag_sco_conn);
		hfp_ag_sco_conn = NULL;
		audio_conn_created = false;
		tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_SCO_DISCONNECTED, &ev, sizeof(ev));
	}
}

static int ag_get_ongoing_call(struct bt_hfp_ag *ag)
{
	if (!has_ongoing_calls) {
		return -EINVAL;
	}

	hfp_ag_ongoing = ag;
	LOG_DBG("Please set ongoing calls");
	return 0;
}

static int ag_memory_dial(struct bt_hfp_ag *ag, const char *location, char **number)
{
	static char *phone = "123456789";

	if (strcmp(location, "0")) {
		return -ENOTSUP;
	}

	LOG_DBG("AG memory dial");

	*number = phone;

	return 0;
}

static int ag_number_call(struct bt_hfp_ag *ag, const char *number)
{
	static char *phone = "123456789";

	LOG_DBG("AG number call");

	if (strcmp(number, phone)) {
		return -ENOTSUP;
	}

	return 0;
}

static void ag_outgoing(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
	LOG_DBG("AG outgoing call %p, number %s", call, number);
	ag_add_a_call(call);
}

static void ag_incoming(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
	LOG_DBG("AG incoming call %p, number %s", call, number);
	ag_add_a_call(call);
}

static void ag_incoming_held(struct bt_hfp_ag_call *call)
{
	LOG_DBG("AG incoming call %p is held", call);
}

static void ag_ringing(struct bt_hfp_ag_call *call, bool in_band)
{
	LOG_DBG("AG call %p start ringing mode %d", call, in_band);
}

static void ag_accept(struct bt_hfp_ag_call *call)
{
	LOG_DBG("AG call %p accept", call);
}

static void ag_held(struct bt_hfp_ag_call *call)
{
	LOG_DBG("AG call %p held", call);
}

static void ag_retrieve(struct bt_hfp_ag_call *call)
{
	LOG_DBG("AG call %p retrieved", call);
}

static void ag_reject(struct bt_hfp_ag_call *call)
{
	LOG_DBG("AG call %p reject", call);
	ag_remove_a_call(call);
}

static void ag_terminate(struct bt_hfp_ag_call *call)
{
	LOG_DBG("AG call %p terminate", call);
	ag_remove_a_call(call);
}

static void ag_codec(struct bt_hfp_ag *ag, uint32_t ids)
{
	LOG_DBG("AG received codec id bit map %x", ids);
}

void ag_vgm(struct bt_hfp_ag *ag, uint8_t gain)
{
	LOG_DBG("AG received vgm %d", gain);
}

void ag_vgs(struct bt_hfp_ag *ag, uint8_t gain)
{
	LOG_DBG("AG received vgs %d", gain);
}

void ag_codec_negotiate(struct bt_hfp_ag *ag, int err)
{
	LOG_DBG("AG codec negotiation result %d", err);
}

void ag_audio_connect_req(struct bt_hfp_ag *ag)
{
	LOG_DBG("Receive audio connect request. "
		"Input `hfp ag audio_connect` to start audio connect");
}

void ag_ecnr_turn_off(struct bt_hfp_ag *ag)
{
	LOG_DBG("encr is disabled");
}

#if defined(CONFIG_BT_HFP_AG_3WAY_CALL)
void ag_explicit_call_transfer(struct bt_hfp_ag *ag)
{
	LOG_DBG("explicit call transfer");
}
#endif /* CONFIG_BT_HFP_AG_3WAY_CALL */

#if defined(CONFIG_BT_HFP_AG_VOICE_RECG)
void ag_voice_recognition(struct bt_hfp_ag *ag, bool activate)
{
	LOG_DBG("AG Voice recognition %s", activate ? "activate" : "deactivate");
}

#if defined(CONFIG_BT_HFP_AG_ENH_VOICE_RECG)
void ag_ready_to_accept_audio(struct bt_hfp_ag *ag)
{
	LOG_DBG("hf is ready to accept audio");
}
#endif /* CONFIG_BT_HFP_AG_ENH_VOICE_RECG */
#endif /* CONFIG_BT_HFP_AG_VOICE_RECG */

#if defined(CONFIG_BT_HFP_AG_VOICE_TAG)
int ag_request_phone_number(struct bt_hfp_ag *ag, char **number)
{
	static bool valid_number;

	if (valid_number && number) {
		valid_number = false;
		*number = "123456789";
		return 0;
	}

	valid_number = true;
	return -EINVAL;
}
#endif /* CONFIG_BT_HFP_AG_VOICE_TAG */

void ag_transmit_dtmf_code(struct bt_hfp_ag *ag, char code)
{
	LOG_DBG("DTMF code is %c", code);
}

struct {
	char *number;
	uint8_t type;
	uint8_t service;
} ag_subscriber_number_info[] = {
	{
		.number = "12345678",
		.type = 128,
		.service = 4,
	},
	{
		.number = "87654321",
		.type = 128,
		.service = 4,
	},
};

static bool subscriber;

int ag_subscriber_number(struct bt_hfp_ag *ag, bt_hfp_ag_query_subscriber_func_t func)
{
	int err;

	if (subscriber && func) {
		for (size_t index = 0; index < ARRAY_SIZE(ag_subscriber_number_info); index++) {
			err = func(ag, ag_subscriber_number_info[index].number,
				   ag_subscriber_number_info[index].type,
				   ag_subscriber_number_info[index].service);
			if (err < 0) {
				break;
			}
		}
	}
	return 0;
}

void ag_hf_indicator_value(struct bt_hfp_ag *ag, enum hfp_ag_hf_indicators indicator,
			   uint32_t value)
{
	LOG_DBG("indicator %d value %d", indicator, value);
}

static struct bt_hfp_ag_cb ag_cb = {
	.connected = ag_connected,
	.disconnected = ag_disconnected,
	.sco_connected = ag_sco_connected,
	.sco_disconnected = ag_sco_disconnected,
// 	.get_ongoing_call = ag_get_ongoing_call,
// 	.memory_dial = ag_memory_dial,
// 	.number_call = ag_number_call,
// 	.outgoing = ag_outgoing,
// 	.incoming = ag_incoming,
// 	.incoming_held = ag_incoming_held,
// 	.ringing = ag_ringing,
	.accept = ag_accept,
// 	.held = ag_held,
// 	.retrieve = ag_retrieve,
// 	.reject = ag_reject,
// 	.terminate = ag_terminate,
// 	.codec = ag_codec,
// 	.codec_negotiate = ag_codec_negotiate,
// 	.audio_connect_req = ag_audio_connect_req,
// 	.vgm = ag_vgm,
// 	.vgs = ag_vgs,
// #if defined(CONFIG_BT_HFP_AG_ECNR)
// 	.ecnr_turn_off = ag_ecnr_turn_off,
// #endif /* CONFIG_BT_HFP_AG_ECNR */
// #if defined(CONFIG_BT_HFP_AG_3WAY_CALL)
// 	.explicit_call_transfer = ag_explicit_call_transfer,
// #endif /* CONFIG_BT_HFP_AG_3WAY_CALL */
// #if defined(CONFIG_BT_HFP_AG_VOICE_RECG)
// 	.voice_recognition = ag_voice_recognition,
// #if defined(CONFIG_BT_HFP_AG_ENH_VOICE_RECG)
// 	.ready_to_accept_audio = ag_ready_to_accept_audio,
// #endif /* CONFIG_BT_HFP_AG_ENH_VOICE_RECG */
// #endif /* CONFIG_BT_HFP_AG_VOICE_RECG */
// #if defined(CONFIG_BT_HFP_AG_VOICE_TAG)
// 	.request_phone_number = ag_request_phone_number,
// #endif /* CONFIG_BT_HFP_AG_VOICE_TAG */
// 	.transmit_dtmf_code = ag_transmit_dtmf_code,
// 	.subscriber_number = ag_subscriber_number,
// 	.hf_indicator_value = ag_hf_indicator_value,
};

/* HFP HF callbacks */
static void hf_remove_calls(void)
{
	ARRAY_FOR_EACH(hfp_hf_call, i) {
		if (hfp_hf_call[i] != NULL) {
			hfp_hf_call[i] = NULL;
		}
	}
}

static void hf_connected(struct bt_conn *conn, struct bt_hfp_hf *hf)
{
	hf_conn = conn;
	hfp_hf = hf;
	conn_count++;
	LOG_DBG("HF connected");
}

static void hf_disconnected(struct bt_hfp_hf *hf)
{
	hf_conn = NULL;
	hfp_hf = NULL;
	hf_remove_calls();
	LOG_DBG("HF disconnected");
}

static void hf_sco_connected(struct bt_hfp_hf *hf, struct bt_conn *sco_conn)
{
	LOG_DBG("HF SCO connected %p", sco_conn);

	if (hf_sco_conn != NULL) {
		LOG_ERR("HF SCO conn %p exists", hf_sco_conn);
		return;
	}

	hf_sco_conn = bt_conn_ref(sco_conn);
}

static void hf_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
	LOG_DBG("HF SCO disconnected %p (reason %u)", sco_conn, reason);

	if (hf_sco_conn == sco_conn) {
		bt_conn_unref(hf_sco_conn);
		hf_sco_conn = NULL;
	} else {
		LOG_ERR("Unknown SCO disconnected (%p != %p)", hf_sco_conn, sco_conn);
	}
}

static void hf_signal(struct bt_hfp_hf *hf, uint32_t value)
{
	hf_check_signal_strength = value;
}

static void hf_retrieve(struct bt_hfp_hf_call *call)
{
	LOG_DBG("hf call %p retrieve", call);
}

/* Minimal set of callbacks needed for connection */
static struct bt_hfp_hf_cb hf_cb = {
	.connected = hf_connected,
	.disconnected = hf_disconnected,
	.sco_connected = hf_sco_connected,
	.sco_disconnected = hf_sco_disconnected,
	.signal = hf_signal,
	.retrieve = hf_retrieve,
};

static uint8_t read_supported_commands(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	struct btp_hfp_read_supported_commands_rp *rp = rsp;

	*rsp_len = tester_supported_commands(BTP_SERVICE_ID_HFP, rp->data);
	*rsp_len += sizeof(*rp);

	return BTP_STATUS_SUCCESS;
}

static uint8_t enable_slc(const void *cmd, uint16_t cmd_len,
			  void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_enable_slc_cmd *cp = cmd;
	struct btp_hfp_enable_slc_rp *rp = rsp;
	struct bt_conn *conn = NULL;;
	struct bt_hfp_ag *ag;
	struct bt_hfp_hf *hf;
	uint8_t channel = cp->channel;
	int err;

	/* Connect to HFP service */
	if (cp->is_ag == 1) {
		conn = bt_conn_lookup_addr_br(&cp->address.a);
		if (conn) {
			err = bt_hfp_ag_connect(conn, &ag, channel);
		} else {
			conn = bt_conn_create_br(&cp->address.a, BT_BR_CONN_PARAM_DEFAULT);
			if (conn == NULL) {
				err = BTP_STATUS_FAILED;
			}
		}
		
	} else {
		err = bt_hfp_hf_connect(conn, &hf, channel);
	}

	bt_conn_unref(conn);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	/* Set connection ID in response */
	rp->connection_id = 1;  /* Using fixed ID for simplicity */
	*rsp_len = sizeof(*rp);

	return BTP_STATUS_SUCCESS;
}


static uint8_t disable_slc(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_disable_slc_cmd *cp = cmd;
	struct btp_hfp_disable_slc_rp *rp = rsp;
	uint8_t count = 0;

	if (hfp_ag) {
		bt_hfp_ag_disconnect(hfp_ag);
	} else {
		while (conn_count == 0) {
			count++;
			OSA_TimeDelay(500);
			if (count > 100)
			break;
		}
		bt_hfp_hf_disconnect(hfp_hf);
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t signal_strength_send(const void *cmd, uint16_t cmd_len,
				    void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_signal_strength_send_cmd *cp = cmd;
	struct btp_hfp_signal_strength_send_rp *rp = rsp;
	uint8_t current_value;
	int err;

	bt_hfp_ag_get_indicator(hfp_ag, BT_HFP_AG_SIGNAL_IND, &current_value);
	if (cp->strength != current_value) {
		err = bt_hfp_ag_signal_strength(hfp_ag, cp->strength);
		if (err) {
			return BTP_STATUS_FAILED;
		}
	}

	hf_check_signal_strength = cp->strength;
	return BTP_STATUS_SUCCESS;
}

static uint8_t control(const void *cmd, uint16_t cmd_len,
		       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_control_cmd *cp = cmd;
	struct btp_hfp_control_rp *rp = rsp;
	uint8_t current_value;
	int err;

	if (!hfp_ag) {
		return BTP_STATUS_FAILED;
	}

	switch (cp->control_type) {
	case HFP_IMPAIR_SIGNAL:
		bt_hfp_ag_get_indicator(hfp_ag, BT_HFP_AG_SIGNAL_IND, &current_value);
		if ((current_value == 0) || (current_value > 6)) {
			current_value = 5;
		} else {
			current_value--;
		}
		err = bt_hfp_ag_set_indicator(hfp_ag, BT_HFP_AG_SIGNAL_IND, current_value);
		hf_check_signal_strength = current_value;
		break;
	default:
		return BTP_STATUS_FAILED;
	}
	
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t signal_strength_verify(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_signal_strength_verify_cmd *cp = cmd;
	struct btp_hfp_signal_strength_verify_rp *rp = rsp;

	if (hf_check_signal_strength == cp->strength) {
		return BTP_STATUS_SUCCESS;
	} else {
		return BTP_STATUS_FAILED;
	}
}

static uint8_t ag_enable_call(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_enable_call_cmd *cp = cmd;
	struct btp_hfp_ag_enable_call_rp *rp = rsp;

	bt_hfp_ag_remote_incoming(hfp_ag, "1234567");
	return BTP_STATUS_SUCCESS;
}

static uint8_t ag_discoverable(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_discoverable_cmd *cp = cmd;
	struct btp_hfp_ag_discoverable_rp *rp = rsp;

	bt_hfp_ag_register(&ag_cb);

	return BTP_STATUS_SUCCESS;
}

static uint8_t hf_discoverable(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_hf_discoverable_cmd *cp = cmd;
	struct btp_hfp_hf_discoverable_rp *rp = rsp;

	bt_hfp_hf_register(&hf_cb);

	return BTP_STATUS_SUCCESS;
}

static uint8_t enable_audio(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_enable_audio_cmd *cp = cmd;
	struct btp_hfp_enable_audio_rp *rp = rsp;
	int err;

	err = bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_CVSD);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t disable_audio(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_disable_audio_cmd *cp = cmd;
	struct btp_hfp_disable_audio_rp *rp = rsp;
	int err;

	err = bt_conn_disconnect(hfp_ag_sco_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static const struct btp_handler hfp_handlers[] = {
	{
		.opcode = BTP_HFP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = read_supported_commands
	},
	{
		.opcode = BTP_HFP_ENABLE_SLC,
		.expect_len = sizeof(struct btp_hfp_enable_slc_cmd),
		.func = enable_slc
	},
	{
		.opcode = BTP_HFP_DISABLE_SLC,
		.expect_len = sizeof(struct btp_hfp_disable_slc_cmd),
		.func = disable_slc
	},
	{
		.opcode = BTP_HFP_SIGNAL_STRENGTH_SEND,
		.expect_len = sizeof(struct btp_hfp_signal_strength_send_cmd),
		.func = signal_strength_send
	},
	{
		.opcode = BTP_HFP_CONTROL,
		.expect_len = sizeof(struct btp_hfp_control_cmd),
		.func = control
	},
	{
		.opcode = BTP_HFP_SIGNAL_STRENGTH_VERIFY,
		.expect_len = sizeof(struct btp_hfp_signal_strength_verify_cmd),
		.func = signal_strength_verify
	},
	{
		.opcode = BTP_HFP_AG_ENABLE_CALL,
		.expect_len = sizeof(struct btp_hfp_ag_enable_call_cmd),
		.func = ag_enable_call
	},
	{
		.opcode = BTP_HFP_AG_DISCOVERABLE,
		.expect_len = sizeof(struct btp_hfp_ag_discoverable_cmd),
		.func = ag_discoverable
	},
	{
		.opcode = BTP_HFP_HF_DISCOVERABLE,
		.expect_len = sizeof(struct btp_hfp_hf_discoverable_cmd),
		.func = hf_discoverable
	},
	{
		.opcode = BTP_HFP_ENABLE_AUDIO,
		.expect_len = sizeof(struct btp_hfp_enable_audio_cmd),
		.func = enable_audio
	},
	{
		.opcode = BTP_HFP_DISABLE_AUDIO,
		.expect_len = sizeof(struct btp_hfp_disable_audio_cmd),
		.func = disable_audio
	},
};

uint8_t tester_init_hfp(void)
{
	int err;

	err = bt_hfp_ag_register(&ag_cb);
	if (err) {
		LOG_ERR("Callback register failed: %d", err);
	}

	err = bt_hfp_hf_register(&hf_cb);
	if (err) {
		LOG_ERR("Callback register failed: %d", err);
	}
	tester_register_command_handlers(BTP_SERVICE_ID_HFP, hfp_handlers,
					 ARRAY_SIZE(hfp_handlers));

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_hfp(void)
{
	return BTP_STATUS_SUCCESS;
}
