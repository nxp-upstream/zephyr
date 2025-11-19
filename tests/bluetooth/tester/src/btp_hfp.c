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

static volatile uint8_t hf_check_signal_strength = 5;
static uint8_t hfp_in_calling_status = 0xff;
uint8_t call_active = 0;
static bool audio_conn_created;
static volatile bool battery_charged_state;
#define MAX_COPS_NAME_SIZE (16)
static char cops_name[MAX_COPS_NAME_SIZE];
static char voice_tag[MAX_COPS_NAME_SIZE] = "+8613812345678";
static uint8_t s_hfp_in_calling_status = 0xff;
static uint8_t wait_call = 0;
static uint8_t call_held = 0;
static bool clear_mem_call_list = false;
static bool ec_nr_disabled = 1;
static bool inband_ring_tone_set;
static bool mute_inband_ringtone;
static volatile uint8_t hf_check_mic_volume;
static volatile uint8_t hf_check_speaker_volume;
static uint8_t codecs_negotiate_done = 0;
struct bt_conn *default_conn;
static volatile bool hf_accept_call;
static bool ring_alert = false;
static volatile bool roam_active_state;
static uint8_t signal_value;
static bool hf_auto_select_codec;
static uint32_t supported_codec_ids;
/* Store HFP connection for later use */
struct bt_hfp_hf *hfp_hf;
struct bt_conn *hf_sco_conn;
static struct bt_hfp_hf_call *hfp_hf_call[CONFIG_BT_HFP_HF_MAX_CALLS];
static uint8_t hfp_ag_call_dir[CONFIG_BT_HFP_HF_MAX_CALLS];

struct bt_hfp_ag *hfp_ag;
struct bt_hfp_ag *hfp_ag_ongoing;
struct bt_conn *hfp_ag_sco_conn;
static struct bt_hfp_ag_call *hfp_ag_call[CONFIG_BT_HFP_AG_MAX_CALLS];

static uint32_t conn_count = 0;
static struct bt_hfp_ag_ongoing_call ag_ongoing_call_info[CONFIG_BT_HFP_AG_MAX_CALLS];

static size_t ag_ongoing_calls;

static bool has_ongoing_calls;

static struct bt_hfp_ag_ongoing_call ag_ongoing_call_info_pre;
static void on_going_timer_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(on_going_timer, on_going_timer_handler);

static void ag_add_a_call(struct bt_hfp_ag_call *call)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (!hfp_ag_call[i]) {
			hfp_ag_call[i] = call;
			return;
		}
	}
}

static size_t ag_get_call_index(struct bt_hfp_ag_call *call)
{
	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (hfp_ag_call[i] == call) {
			return i;
		}
	}
}

static size_t ag_get_call_count(void)
{
	size_t count = 0;

	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (hfp_ag_call[i] != NULL) {
			count++;
		}
	}

	return count;
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
		return;
	}

	audio_conn_created = true;
	hfp_ag_sco_conn = bt_conn_ref(sco_conn);
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_SCO_CONNECTED, &ev, sizeof(ev));
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

	has_ongoing_calls = false;
	hfp_ag_ongoing = ag;
	(void)k_work_reschedule(&on_going_timer, K_MSEC(10));
	return 0;
}

static int ag_memory_dial(struct bt_hfp_ag *ag, const char *location, char **number)
{
	static char *phone = "1234567";

	if (clear_mem_call_list) {
		return -ENOTSUP;
	}

	*number = phone;

	return 0;
}

static int ag_number_call(struct bt_hfp_ag *ag, const char *number)
{
	static char *phone = "1234567";
	int len = strlen(number);

	if (number[len-1] == ';') {
		len -= 1;
	}

	if (strncmp(number, phone, len)) {
		return -ENOTSUP;
	}

	return 0;
}

static char last_number[CONFIG_BT_HFP_AG_PHONE_NUMBER_MAX_LEN + 1];

static int ag_redial(struct bt_hfp_ag *ag, char number[CONFIG_BT_HFP_AG_PHONE_NUMBER_MAX_LEN + 1])
{
	if (strlen(last_number) == 0) {
		return -EINVAL;
	}

	strncpy(number, last_number, CONFIG_BT_HFP_AG_PHONE_NUMBER_MAX_LEN);

	return 0;
}

#define MAX_CALL_NUMBER_SIZE 0x41

static uint8_t call_status_buf[sizeof(struct btp_hfp_new_call_ev) + MAX_CALL_NUMBER_SIZE];

static void ag_outgoing(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
	struct btp_hfp_new_call_ev *ev;

	LOG_DBG("AG outgoing call %p, number %s", call, number);
	ag_add_a_call(call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	hfp_ag_call_dir[ag_get_call_index(call)] = BTP_HFP_CALL_DIR_OUTGOING;

	ev = (void *)&call_status_buf[0];
	ev->index = (uint8_t)ag_get_call_index(call);
	ev->dir = BTP_HFP_CALL_DIR_OUTGOING;
	memset(ev->number, 0, MAX_CALL_NUMBER_SIZE);
	strncpy(ev->number, number, MAX_CALL_NUMBER_SIZE - 1);
	ev->type = 0;
	ev->number_len = strlen(ev->number);

	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_NEW_CALL, call_status_buf,
		     sizeof(*ev) + ev->number_len);
}

static void ag_incoming(struct bt_hfp_ag *ag, struct bt_hfp_ag_call *call, const char *number)
{
	struct btp_hfp_new_call_ev *ev;

	LOG_DBG("AG incoming call %p, number %s", call, number);
	ag_add_a_call(call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	hfp_ag_call_dir[ag_get_call_index(call)] = BTP_HFP_CALL_DIR_INCOMING;

	ev = (void *)&call_status_buf[0];
	ev->index = (uint8_t)ag_get_call_index(call);
	ev->dir = BTP_HFP_CALL_DIR_INCOMING;
	memset(ev->number, 0, MAX_CALL_NUMBER_SIZE);
	strncpy(ev->number, number, MAX_CALL_NUMBER_SIZE - 1);
	ev->type = 0;
	ev->number_len = strlen(ev->number);

	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_NEW_CALL, call_status_buf,
		     sizeof(*ev) + ev->number_len);
}

static void ag_incoming_held(struct bt_hfp_ag_call *call)
{
	struct btp_hfp_call_status_ev ev;

	LOG_DBG("AG incoming call %p is held", call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = BTP_HFP_CALL_STATUS_INCOMING_HELD;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_ringing(struct bt_hfp_ag_call *call, bool in_band)
{
	struct btp_hfp_call_status_ev ev;

	LOG_DBG("AG call %p start ringing mode %d", call, in_band);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = hfp_ag_call_dir[ev.index] == BTP_HFP_CALL_DIR_INCOMING ?
					BTP_HFP_CALL_STATUS_WAITING : BTP_HFP_CALL_STATUS_ALERTING;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_accept(struct bt_hfp_ag_call *call)
{
	struct btp_hfp_call_status_ev ev;
	LOG_DBG("AG call %p accept", call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = BTP_HFP_CALL_STATUS_ACTIVE;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_held(struct bt_hfp_ag_call *call)
{
	struct btp_hfp_call_status_ev ev;
	LOG_DBG("AG call %p held", call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = BTP_HFP_CALL_STATUS_HELD;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_retrieve(struct bt_hfp_ag_call *call)
{
	struct btp_hfp_call_status_ev ev;
	LOG_DBG("AG call %p retrieved", call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = BTP_HFP_CALL_STATUS_ACTIVE;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_reject(struct bt_hfp_ag_call *call)
{
	struct btp_hfp_call_status_ev ev;
	LOG_DBG("AG call %p reject", call);
	ag_remove_a_call(call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = BTP_HFP_CALL_STATUS_REJECTED;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_terminate(struct bt_hfp_ag_call *call)
{
	struct btp_hfp_call_status_ev ev;
	LOG_DBG("AG call %p terminate", call);
	ag_remove_a_call(call);

	if (ag_get_call_index(call) >= CONFIG_BT_HFP_AG_MAX_CALLS) {
		LOG_ERR("Call index out of range");
		return;
	}

	ev.index = (uint8_t)ag_get_call_index(call);
	ev.status = BTP_HFP_CALL_STATUS_TERMINATED;
	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CALL_STATUS, &ev, sizeof(ev));
}

static void ag_codec(struct bt_hfp_ag *ag, uint32_t ids)
{
	supported_codec_ids = ids;
}

void ag_vgm(struct bt_hfp_ag *ag, uint8_t gain)
{
	hf_check_mic_volume = gain;
}

void ag_vgs(struct bt_hfp_ag *ag, uint8_t gain)
{
	hf_check_speaker_volume = gain;
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
#if 0
	uint8_t state = 0;
	state |= BIT(0);
	bt_hfp_ag_vre_state(hfp_ag, state);
#endif
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
	*number = voice_tag;

	return 0;
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
	.get_ongoing_call = ag_get_ongoing_call,
	.memory_dial = ag_memory_dial,
	.number_call = ag_number_call,
	.redial = ag_redial,
	.outgoing = ag_outgoing,
	.incoming = ag_incoming,
	.incoming_held = ag_incoming_held,
	.ringing = ag_ringing,
	.accept = ag_accept,
	.held = ag_held,
	.retrieve = ag_retrieve,
	.reject = ag_reject,
	.terminate = ag_terminate,
	.codec = ag_codec,
	.codec_negotiate = ag_codec_negotiate,
	.audio_connect_req = ag_audio_connect_req,
	.vgm = ag_vgm,
	.vgs = ag_vgs,
// #if defined(CONFIG_BT_HFP_AG_ECNR)
// 	.ecnr_turn_off = ag_ecnr_turn_off,
// #endif /* CONFIG_BT_HFP_AG_ECNR */
#if defined(CONFIG_BT_HFP_AG_3WAY_CALL)
	.explicit_call_transfer = ag_explicit_call_transfer,
#endif /* CONFIG_BT_HFP_AG_3WAY_CALL */
#if defined(CONFIG_BT_HFP_AG_VOICE_RECG)
	.voice_recognition = ag_voice_recognition,
#if defined(CONFIG_BT_HFP_AG_ENH_VOICE_RECG)
	.ready_to_accept_audio = ag_ready_to_accept_audio,
#endif /* CONFIG_BT_HFP_AG_ENH_VOICE_RECG */
#endif /* CONFIG_BT_HFP_AG_VOICE_RECG */
#if defined(CONFIG_BT_HFP_AG_VOICE_TAG)
 	.request_phone_number = ag_request_phone_number,
#endif /* CONFIG_BT_HFP_AG_VOICE_TAG */
	.transmit_dtmf_code = ag_transmit_dtmf_code,
	.subscriber_number = ag_subscriber_number,
// 	.hf_indicator_value = ag_hf_indicator_value,
};

/* HFP HF callbacks */

static void hf_add_a_call(struct bt_hfp_hf_call *call)
{
	ARRAY_FOR_EACH(hfp_hf_call, i) {
		if (!hfp_hf_call[i]) {
			hfp_hf_call[i] = call;
			return;
		}
	}
}

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
	default_conn = conn;
	hfp_hf = hf;
	conn_count++;
	LOG_DBG("HF connected");
}

static void hf_disconnected(struct bt_hfp_hf *hf)
{
	default_conn = NULL;
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

static void hf_battery(struct bt_hfp_hf *hf, uint32_t value)
{
	if (value == 5) {
		battery_charged_state = true;
	} else {
		battery_charged_state = false;
	}
}

static void hf_ring_indication(struct bt_conn *conn)
{
	ring_alert = true;
}

void hf_remote_ringing(struct bt_hfp_hf_call *call)
{
	hf_add_a_call(call);
}

void hf_outgoing(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
	hf_add_a_call(call);
	bt_hfp_hf_accept(call);
}

static void hf_incoming(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
	hf_add_a_call(call);
}

static void hf_accept(struct bt_hfp_hf_call *call)
{
	ring_alert = false;
}

static void hf_roam(struct bt_conn *conn, uint32_t value)
{
	roam_active_state = value ? true : false;
}

void hf_subscriber_number(struct bt_hfp_hf *hf, const char *number, uint8_t type, uint8_t service)
{
}

#if defined(CONFIG_BT_HFP_HF_ECNR)
static void hf_ecnr_turn_off(struct bt_hfp_hf *hf, int err)
{
}
#endif /* CONFIG_BT_HFP_HF_ECNR */

#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
static void hf_codec_negotiate(struct bt_hfp_hf *hf, uint8_t id)
{
	bt_hfp_hf_select_codec(hfp_hf, id);
}
#endif /* CONFIG_BT_HFP_HF_CODEC_NEG */

#if defined(CONFIG_BT_HFP_HF_VOLUME)
static void hf_vgm(struct bt_hfp_hf *hf, uint8_t gain)
{
	hf_check_mic_volume = gain;
}

static void hf_vgs(struct bt_hfp_hf *hf, uint8_t gain)
{
	hf_check_speaker_volume = gain;
}
#endif /* CONFIG_BT_HFP_HF_VOLUME */

static void hf_operator(struct bt_hfp_hf *hf, uint8_t mode, uint8_t format, char *operator)
{
	size_t len = strlen(operator);
	size_t copy_len = (len < MAX_COPS_NAME_SIZE - 1) ? len : MAX_COPS_NAME_SIZE - 1;

	memcpy(cops_name, operator, copy_len);
	cops_name[copy_len] = '\0';
}

static void hf_inband_ring(struct bt_hfp_hf *hf, bool inband)
{
	inband_ring_tone_set = inband;
}

static void hf_query_call(struct bt_hfp_hf *hf, struct bt_hfp_hf_current_call *call)
{
	LOG_DBG("hf query call %p", call);
}

/* Minimal set of callbacks needed for connection */
static struct bt_hfp_hf_cb hf_cb = {
	.connected = hf_connected,
	.disconnected = hf_disconnected,
	.sco_connected = hf_sco_connected,
	.sco_disconnected = hf_sco_disconnected,
	.signal = hf_signal,
	.retrieve = hf_retrieve,
	.battery = hf_battery,
	.ring_indication = hf_ring_indication,
	.remote_ringing = hf_remote_ringing,
	.incoming = hf_incoming,
	.outgoing = hf_outgoing,
	.accept = hf_accept,
	.roam = hf_roam,
	.subscriber_number = hf_subscriber_number,
#if defined(CONFIG_BT_HFP_HF_ECNR)
	.ecnr_turn_off = hf_ecnr_turn_off,
#endif /* CONFIG_BT_HFP_HF_ECNR */
#if defined(CONFIG_BT_HFP_HF_CODEC_NEG)
	.codec_negotiate = hf_codec_negotiate,
#endif /* CONFIG_BT_HFP_HF_CODEC_NEG */
#if defined(CONFIG_BT_HFP_HF_VOLUME)
	.vgm = hf_vgm,
	.vgs = hf_vgs,
#endif /* CONFIG_BT_HFP_HF_VOLUME */
	.operator = hf_operator,
	.query_call = hf_query_call,
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
	struct bt_conn *conn;
	struct bt_conn_info info;
	struct bt_hfp_ag *ag;
	struct bt_hfp_hf *hf;
	uint8_t channel = cp->channel;
	int err;

	/* Connect to HFP service */
	if (cp->is_ag == 1) {
		if (default_conn == NULL) {
			bt_hfp_ag_register(&ag_cb);
			conn = bt_conn_create_br(&cp->address.a, BT_BR_CONN_PARAM_DEFAULT);
			if (conn == NULL) {
				return BTP_STATUS_FAILED;
			}
			bt_conn_unref(conn);

			default_conn = conn;
		}
		if (default_conn) {
			bt_conn_get_info(default_conn, &info);
			if (info.state == BT_CONN_STATE_CONNECTED) {
				bt_hfp_ag_connect(default_conn, &ag, channel);
				return BTP_STATUS_SUCCESS;
			}
			else {
				default_conn = NULL;
			}
		}
	} else {
		if (default_conn == NULL) {
			conn = bt_conn_lookup_addr_br(&cp->address.a);
			if (conn != NULL)
			{
				bt_conn_unref(conn);
			}

			conn = bt_conn_create_br(&cp->address.a, BT_BR_CONN_PARAM_DEFAULT);
			if (conn == NULL)
			{
				return BTP_STATUS_FAILED;
			}
			bt_conn_unref(conn);
		}
		default_conn = conn;
		if (default_conn) {
			bt_conn_get_info(default_conn, &info);
			if (info.state == BT_CONN_STATE_CONNECTED) {
				err = bt_hfp_hf_connect(default_conn, &hf, channel);
				return BTP_STATUS_SUCCESS;
			} else {
				default_conn = NULL;
			}
		}
	}
	/* Set connection ID in response */
	rp->connection_id = 1;
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

	bt_hfp_ag_signal_strength(hfp_ag, cp->strength);

	hf_check_signal_strength = cp->strength;

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

static uint8_t control(const void *cmd, uint16_t cmd_len,
		       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_control_cmd *cp = cmd;
	int err = 0;

	switch (cp->control_type) {
	case HFP_IMPAIR_SIGNAL:
		if (hf_check_signal_strength > 0) {
			hf_check_signal_strength--;
		}
		bt_hfp_ag_signal_strength(hfp_ag, hf_check_signal_strength);
		break;
	case HFP_AG_ANSWER_CALL:
		if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
			err = bt_hfp_ag_remote_accept(hfp_ag_call[cp->value]);
			s_hfp_in_calling_status = 3;
		} else {
			err = -EINVAL;
		}
		break;
	case HFP_REJECT_CALL:
		if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
			err = bt_hfp_ag_reject(hfp_ag_call[cp->value]);
		} else {
			// err = bt_hfp_hf_reject(hfp_hf_call[0]);
			err = bt_hfp_hf_terminate(hfp_hf_call[0]);
		}
		break;
	case HFP_END_CALL:
		if (hfp_ag) {
			err = bt_hfp_ag_terminate(hfp_ag_call[0]);
		} else {
			err = bt_hfp_hf_terminate(hfp_hf_call[0]);
		}
		break;
	case HFP_DISABLE_IN_BAND:
		err = bt_hfp_ag_inband_ringtone(hfp_ag, false);
		break;
	case HFP_ENABLE_INBAND_RING:
		err = bt_hfp_ag_inband_ringtone(hfp_ag, true);
		break;
	case HFP_TWC_CALL:
		if (hfp_ag) {
			err = bt_hfp_ag_remote_incoming(hfp_ag, "7654321");
		}
		break;
	case HFP_ENABLE_VR:
		if (hfp_ag) {
			err = bt_hfp_ag_voice_recognition(hfp_ag, true);
		} else {
			err = bt_hfp_hf_voice_recognition(hfp_hf, true);
		}
		break;
	case HFP_SEND_BCC:
		if (hfp_ag) {
			bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_CVSD);
			s_hfp_in_calling_status = 3;
		} else {
			bt_hfp_hf_audio_connect(hfp_hf);
		}
		break;
	case HFP_SEND_BCC_MSBC:
		if (hfp_ag) {
			bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_MSBC);
		} else {
			bt_hfp_hf_audio_connect(hfp_hf);
		}
		break;
	case HFP_SEND_BCC_SWB:
		if (hfp_ag) {
			bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_LC3_SWB);
		} else {
			bt_hfp_hf_audio_connect(hfp_hf);
		}
		break;
	case HFP_CLS_MEM_CALL_LIST:
		clear_mem_call_list = true;
		break;
	case HFP_ACCEPT_HELD_CALL:
		if (hfp_hf_call[0]) {
			err = bt_hfp_hf_hold_incoming(hfp_hf_call[0]);
		} else {
			err = -1;
		}
		break;
	case HFP_HELD_ACTIVE_CALL:
		err = bt_hfp_hf_hold_active_accept_other(hfp_hf);
		break;
	case HFP_ACCEPT_INCOMING_HELD_CALL:
		if (hfp_hf_call[0]) {
			err = bt_hfp_hf_accept(hfp_hf_call[0]);
		} else {
			if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
				err = bt_hfp_ag_accept(hfp_ag_call[cp->value]);
			} else {
				err = -EINVAL;
			}
		}
		break;
	case HFP_REJECT_HELD_CALL:
		if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
			err = bt_hfp_ag_reject(hfp_ag_call[cp->value]);
		} else if ((hfp_hf != NULL) && (cp->value < ARRAY_SIZE(hfp_hf_call))) {
			if (cp->value == 0) {
				err = bt_hfp_hf_reject(hfp_hf_call[0]);
			} else {
				err = bt_hfp_hf_set_udub(hfp_hf);
			}
		} else {
			err = -EINVAL;
		}
		break;
	case HFP_OUT_CALL:
		if(hfp_ag) {
			bt_hfp_ag_outgoing(hfp_ag, "7654321");
		} else {
			bt_hfp_hf_number_call(hfp_hf, "7654321");
		}
		break;
	case HFP_ENABLE_CLIP:
		err = bt_hfp_hf_cli(hfp_hf, true);
		break;
	case HFP_QUERY_LIST_CALL:
		err = bt_hfp_hf_query_list_of_current_calls(hfp_hf);
		break;
	case HFP_SEND_IIA:
		err = bt_hfp_hf_indicator_status(hfp_hf, 5);
		break;
	case HFP_ENABLE_SUB_NUMBER:
		err = bt_hfp_hf_query_subscriber(hfp_hf);
		break;
	case HFP_OUT_MEM_CALL:
		err = bt_hfp_hf_memory_dial(hfp_hf, "1");
		break;
	case HFP_OUT_MEM_OUTOFRANGE_CALL:
		err = bt_hfp_hf_memory_dial(hfp_hf, "2");
		break;
	case HFP_EC_NR_DISABLE:
		err = bt_hfp_hf_turn_off_ecnr(hfp_hf);
		break;
	case HFP_DISABLE_VR:
		if (hfp_ag) {
			err = bt_hfp_ag_voice_recognition(hfp_ag, false);
		} else {
			err = bt_hfp_hf_voice_recognition(hfp_hf, false);
		}
		break;
	case HFP_ENABLE_BINP:
		err = bt_hfp_hf_request_phone_number(hfp_hf);
		break;
	case HFP_JOIN_CONVERSATION_CALL:
		if (hfp_ag) {
			err = bt_hfp_ag_explicit_call_transfer(hfp_ag);
		} else {
			err = bt_hfp_hf_join_conversation(hfp_hf);
		}
		break;
	case HFP_EXPLICIT_TRANSFER_CALL:
		err = bt_hfp_hf_explicit_call_transfer(hfp_hf);
		break;
	case HFP_OUT_LAST_CALL:
		err = bt_hfp_hf_redial(hfp_hf);
		break;
	case HFP_DISABLE_ACTIVE_CALL:
		err = bt_hfp_hf_release_active_accept_other(hfp_hf);
		break;
	case HFP_END_SECOND_CALL:
		err = bt_hfp_hf_terminate(hfp_hf_call[1]);
		break;
	case HFP_MUTE_INBAND_RING:
		mute_inband_ringtone = true;
		break;
	case HFP_REMOTE_REJECT:
		if (hfp_ag) {
			err = bt_hfp_ag_remote_reject(hfp_ag_call[0]);
		} else {
			err = -1;
		}
		break;
	case HFP_REMOTE_RING:
		if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
			err = bt_hfp_ag_remote_ringing(hfp_ag_call[cp->value]);
		} else {
			err = -EINVAL;
		}
		break;
	case HFP_AG_HOLD:
		if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
			err = bt_hfp_ag_hold(hfp_ag_call[cp->value]);
		} else {
			err = -EINVAL;
		}
		break;
	case HFP_AG_RETRIEVE:
		if ((hfp_ag != NULL) && (cp->value < ARRAY_SIZE(hfp_ag_call))) {
			err = bt_hfp_ag_retrieve(hfp_ag_call[cp->value]);
		} else {
			err = -EINVAL;
		}
		break;
	case HFP_AG_VRE_STATE:
		if ((hfp_ag != NULL) && (cp->value < 8)) {
			err = bt_hfp_ag_vre_state(hfp_ag, BIT(cp->value));
		} else {
			err = -EINVAL;
		}
		break;
	case HFP_HF_INDICATOR_VALUE:
		if (cp->flags == 1) {
			(void)bt_hfp_hf_enhanced_safety(hfp_hf, cp->value);
		} else {
			(void)bt_hfp_hf_battery(hfp_hf, cp->value);
		}
		break;
	case HFP_HF_READY_ACCEPT_AUDIO:
		err = bt_hfp_hf_ready_to_accept_audio(hfp_hf);
		break;
	case HFP_AG_SET_LAST_NUM:
		if (hfp_ag) {
			err = 0;
			memcpy(last_number, "12345678", sizeof("12345678"));
		} else {
			err = -1;
		}
		break;
	default:
		err = -1;
	}

	if (err < 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static void on_going_timer_handler(struct k_work *work)
{
	int err;

	if (hfp_ag_ongoing == NULL) {
		return;
	}

	err = bt_hfp_ag_ongoing_calls(hfp_ag_ongoing, ag_ongoing_call_info, ag_ongoing_calls);
	if(err) {
		LOG_DBG("AG ongoing calls set fail!");
	}
	hfp_ag_ongoing = NULL;
}

static uint8_t ag_enable_call(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_enable_call_cmd *cp = cmd;
	struct btp_hfp_ag_enable_call_rp *rp = rsp;
	int err = 0;

	char *number = "1234567";

	/* The number should be set by upper */
	if (ag_get_call_count() != 0) {
		number = "7654321";
	}
	err = bt_hfp_ag_remote_incoming(hfp_ag, number);
	if (err < 0) {
		return BTP_STATUS_FAILED;
	}

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

static uint8_t verify_network_operator(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_verify_network_operator_cmd *cp = cmd;
	struct btp_hfp_verify_network_operator_rp *rp = rsp;

	if (strncmp(cp->op, cops_name, MAX_COPS_NAME_SIZE) == 0) {
		return BTP_STATUS_SUCCESS;
	}

	return BTP_STATUS_FAILED;
}

static uint8_t ag_disable_call_external(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_disable_call_external_cmd *cp = cmd;
	struct btp_hfp_ag_disable_call_external_rp *rp = rsp;
	int err;

	ARRAY_FOR_EACH(hfp_ag_call, i) {
		if (hfp_ag_call[i] != NULL) {
			err = bt_hfp_ag_remote_terminate(hfp_ag_call[i]);
			/* Ignore the error code */
			if (err != 0) {
				LOG_ERR("Failed to terminate the call %d", i);
			}
		}
	}
	return BTP_STATUS_SUCCESS;
}

static uint8_t hf_answer_call(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_hf_answer_call_cmd *cp = cmd;
	struct btp_hfp_hf_answer_call_rp *rp = rsp;

	hf_accept_call = true;
	bt_hfp_hf_accept(hfp_hf_call[0]);

	return BTP_STATUS_SUCCESS;
}

static uint8_t verify(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_verify_cmd *cp = cmd;
	struct btp_hfp_verify_rp *rp = rsp;

	switch (cp->verify_type) {
	case HFP_VERIFY_EC_NR_DISABLED:
		if (!ec_nr_disabled) {
			return BTP_STATUS_FAILED;
		}
		break;
	case HFP_VERIFY_INBAND_RING:
		if (inband_ring_tone_set && audio_conn_created) {
			return BTP_STATUS_SUCCESS;
		}
		else {
			uint16_t delay = 12;

			while (delay--) {
				OSA_TimeDelay(500);
				if (inband_ring_tone_set && audio_conn_created) {
					return BTP_STATUS_SUCCESS;
				}
			}
		}
		break;
	case HFP_VERIFY_IUT_ALERTING:
		if (!ring_alert) {
			return BTP_STATUS_FAILED;
		}
		break;
	case HFP_VERIFY_IUT_NOT_ALERTING:
		if (ring_alert) {
			return BTP_STATUS_FAILED;
		}
		break;
	case HFP_VERIFY_INBAND_RING_MUTING:
		if (inband_ring_tone_set && (!mute_inband_ringtone)) {
			return BTP_STATUS_FAILED;
		}
		break;
	default:
		break;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t verify_voice_tag(const void *cmd, uint16_t cmd_len,
			        void *rsp, uint16_t *rsp_len)
{
	const char *cp = (const char *)cmd;
	struct btp_hfp_verify_voice_tag_rp *rp = rsp;

	if (strncmp(cp, voice_tag, MAX_COPS_NAME_SIZE) == 0) {
		return BTP_STATUS_SUCCESS;
	}

	return BTP_STATUS_FAILED;

}

static uint8_t speaker_mic_volume_send(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_speaker_mic_volume_send_cmd *cp = cmd;
	struct btp_hfp_speaker_mic_volume_send_rp *rp = rsp;
	int err;
	if (cp->speaker_mic == 0x0) {
		if (hfp_ag != NULL) {
			err = bt_hfp_ag_vgs(hfp_ag, cp->speaker_mic_volume);
		}
		else {
			err = bt_hfp_hf_vgs(hfp_hf, cp->speaker_mic_volume);
		}
		hf_check_speaker_volume = cp->speaker_mic_volume;
	} else if (cp->speaker_mic == 0x1) {
		if (hfp_ag != NULL) {
			err = bt_hfp_ag_vgm(hfp_ag, cp->speaker_mic_volume);
		}
		else {
			err = bt_hfp_hf_vgm(hfp_hf, cp->speaker_mic_volume);
		}

		hf_check_mic_volume = cp->speaker_mic_volume;
	} else {
		return BTP_STATUS_UNKNOWN_CMD;
	}

	if (err) {
		return BTP_STATUS_FAILED;
	}
	return BTP_STATUS_SUCCESS;
}

static uint8_t enable_audio(const void *cmd, uint16_t cmd_len,
			    void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_enable_audio_cmd *cp = cmd;
	struct btp_hfp_enable_audio_rp *rp = rsp;
	int err;

	if (hfp_ag){
		if (supported_codec_ids & BIT(BT_HFP_AG_CODEC_CVSD)) {
			err = bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_CVSD);
		} else if (supported_codec_ids & BIT(BT_HFP_AG_CODEC_MSBC)) {
			err = bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_MSBC);
		} else if (supported_codec_ids & BIT(BT_HFP_AG_CODEC_LC3_SWB)) {
			err = bt_hfp_ag_audio_connect(hfp_ag, BT_HFP_AG_CODEC_LC3_SWB);
		} else {
			err = -1;
		}
	} else {
		err = bt_hfp_hf_audio_connect(hfp_hf);
	}

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
	// int err;

	bt_conn_disconnect(hfp_ag_sco_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	// if (err) {
	// 	return BTP_STATUS_FAILED;
	// }

	return BTP_STATUS_SUCCESS;
}

static uint8_t enable_network(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_enable_network_cmd *cp = cmd;
	struct btp_hfp_enable_network_rp *rp = rsp;

	bt_hfp_ag_service_availability(hfp_ag, true);

	return BTP_STATUS_SUCCESS;
}

static uint8_t disable_network(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_disable_network_cmd *cp = cmd;
	struct btp_hfp_disable_network_rp *rp = rsp;

	bt_hfp_ag_service_availability(hfp_ag, false);

	return BTP_STATUS_SUCCESS;
}

static uint8_t make_roam_active(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_make_roam_active_cmd *cp = cmd;
	struct btp_hfp_make_roam_active_rp *rp = rsp;

	bt_hfp_ag_roaming_status(hfp_ag, 1);
	return BTP_STATUS_SUCCESS;
}

static uint8_t make_roam_inactive(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_make_roam_inactive_cmd *cp = cmd;
	struct btp_hfp_make_roam_inactive_rp *rp = rsp;

	bt_hfp_ag_roaming_status(hfp_ag, 0);
	return BTP_STATUS_SUCCESS;
}

static uint8_t make_battery_not_full_charged(const void *cmd, uint16_t cmd_len,
					     void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_make_battery_not_full_charged_cmd *cp = cmd;
	struct btp_hfp_make_battery_not_full_charged_rp *rp = rsp;
	int err;

	if (hfp_ag != NULL){
		err = bt_hfp_ag_battery_level(hfp_ag, 3);
		if (err) {
			return BTP_STATUS_FAILED;
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t make_battery_full_charged(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_make_battery_full_charged_cmd *cp = cmd;
	struct btp_hfp_make_battery_full_charged_rp *rp = rsp;
	int err;

	if (hfp_ag != NULL){
		err = bt_hfp_ag_battery_level(hfp_ag, 5);
		if (err) {
			return BTP_STATUS_FAILED;
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t verify_battery_charged(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_verify_battery_charged_cmd *cp = cmd;
	struct btp_hfp_verify_battery_charged_rp *rp = rsp;

	if (battery_charged_state) {
		return BTP_STATUS_SUCCESS;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t verify_battery_discharged(const void *cmd, uint16_t cmd_len,
					 void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_verify_battery_discharged_cmd *cp = cmd;
	struct btp_hfp_verify_battery_discharged_rp *rp = rsp;

	if (!battery_charged_state) {
		return BTP_STATUS_SUCCESS;
	}

	return BTP_STATUS_FAILED;
}

static uint8_t speaker_mic_volume_verify(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_speaker_mic_volume_verify_cmd *cp = cmd;
	struct btp_hfp_speaker_mic_volume_verify_rp *rp = rsp;

	if (cp->speaker_mic == 0x1) {
		if (hf_check_mic_volume == cp->speaker_mic_volume) {
			return BTP_STATUS_SUCCESS;
		} else {
			return BTP_STATUS_FAILED;
		}
	} else if (cp->speaker_mic == 0x0) {
		if (hf_check_speaker_volume == cp->speaker_mic_volume) {
			return BTP_STATUS_SUCCESS;
		} else {
			return BTP_STATUS_FAILED;
		}
	} else {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t ag_register(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_register_cmd *cp = cmd;
	struct btp_hfp_ag_register_rp *rp = rsp;
	int err;

	err = bt_hfp_ag_register(&ag_cb);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hf_register(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_hf_register_cmd *cp = cmd;
	struct btp_hfp_hf_register_rp *rp = rsp;
	int err;

	err = bt_hfp_hf_register(&hf_cb);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t verify_roam_active(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_verify_roam_active_cmd *cp = cmd;
	struct btp_hfp_verify_roam_active_rp *rp = rsp;

	if (roam_active_state) {
		return BTP_STATUS_SUCCESS;
	}
	return BTP_STATUS_FAILED;
}

static uint8_t query_network_operator(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_query_network_operator_cmd *cp = cmd;
	struct btp_hfp_query_network_operator_rp *rp = rsp;
	int err;

	err = bt_hfp_hf_get_operator(hfp_hf);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

struct btp_ag_vre_text {
	struct k_work_delayable work;
	uint8_t status;
	uint16_t id;
	uint8_t type;
	uint8_t operation;
};

static void vre_text_work_handler(struct k_work *work)
{
	struct btp_ag_vre_text *vre_text;
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	int err;
	char id[sizeof(vre_text->id) * 2 + 1];

	if (hfp_ag == NULL) {
		return;
	}

	vre_text = CONTAINER_OF(dwork, struct btp_ag_vre_text, work);

	bin2hex((uint8_t *)&vre_text->id, sizeof(vre_text->id), id, sizeof(id));
	bt_hfp_ag_vre_textual_representation(hfp_ag, vre_text->status, id, vre_text->type,
					     vre_text->operation, "1");
}

static struct btp_ag_vre_text vre_text = {
	.work = Z_WORK_DELAYABLE_INITIALIZER(vre_text_work_handler),
};

static uint8_t ag_vre_text(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_vre_text_cmd *cp = cmd;
	struct btp_hfp_ag_vre_text_rp *rp = rsp;
	int err;
	char id[sizeof(cp->id) * 2 + 1];

	if (cp->delay != 0) {
		vre_text.operation = cp->operation;
		vre_text.type = cp->type;
		vre_text.id = cp->id;
		vre_text.status = cp->status;
		err = k_work_schedule(&vre_text.work, K_MSEC(cp->delay));
		if (err < 0) {
			return BTP_STATUS_FAILED;
		}
		return BTP_STATUS_SUCCESS;
	}

	bin2hex((uint8_t *)&cp->id, sizeof(cp->id), id, sizeof(id));
	err = bt_hfp_ag_vre_textual_representation(hfp_ag, cp->status, id, cp->type,
						   cp->operation, "1");
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t dtmf_code_send(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_dtmf_code_send_cmd *cp = cmd;
	struct btp_hfp_dtmf_code_send_rp *rp = rsp;
	int err;

	err = bt_hfp_hf_transmit_dtmf_code(hfp_hf_call[0], cp->dtmf_code);
	if (err != 0) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t verify_roam_inactive(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_verify_roam_inactive_cmd *cp = cmd;
	struct btp_hfp_verify_roam_inactive_rp *rp = rsp;

	if (!roam_active_state) {
		return BTP_STATUS_SUCCESS;
	}
	return BTP_STATUS_FAILED;
}

static uint8_t private_consultation_mode(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_private_consultation_mode_cmd *cp = cmd;
	struct btp_hfp_private_consultation_mode_rp *rp = rsp;
	int err;

	err = bt_hfp_hf_private_consultation_mode(hfp_hf_call[cp->index]);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t release_specified_call(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_release_specified_call_cmd *cp = cmd;
	struct btp_hfp_release_specified_call_rp *rp = rsp;
	int err;

	err = bt_hfp_hf_release_specified_call(hfp_hf_call[cp->index]);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t ag_set_ongoing_calls(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_set_ongoing_calls_cmd *cp = cmd;
	struct btp_hfp_set_ongoing_calls_rp *rp = rsp;
	size_t max_calls;

	max_calls =  MIN(CONFIG_BT_HFP_AG_MAX_CALLS, ARRAY_SIZE(ag_ongoing_call_info));
	if (ag_ongoing_calls >= max_calls) {
		return BTP_STATUS_FAILED;
	}

	memset(ag_ongoing_call_info[ag_ongoing_calls].number, 0,
		sizeof(ag_ongoing_call_info[ag_ongoing_calls].number));
	memcpy(ag_ongoing_call_info[ag_ongoing_calls].number, cp->number,
		MIN(cp->number_len, sizeof(ag_ongoing_call_info[ag_ongoing_calls].number) - 1));
	ag_ongoing_call_info[ag_ongoing_calls].type = (uint8_t)cp->type;
	ag_ongoing_call_info[ag_ongoing_calls].status = (enum bt_hfp_ag_call_status)cp->status;
	ag_ongoing_call_info[ag_ongoing_calls].dir = (enum bt_hfp_ag_call_dir)cp->dir;

	ag_ongoing_calls++;

	if (cp->all) {
		has_ongoing_calls = true;
	}

	if (ag_ongoing_calls >= max_calls) {
		has_ongoing_calls = true;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t ag_hold_incoming(const void *cmd, uint16_t cmd_len,
	void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_ag_hold_incoming_cmd *cp = cmd;
	struct btp_hfp_ag_hold_incoming_rp *rp = rsp;
	int err;

	err = bt_hfp_ag_hold_incoming(hfp_ag_call[0]);
	if (err) {
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t ag_last_dialed_number(const void *cmd, uint16_t cmd_len, void *rsp,
				     uint16_t *rsp_len)
{
	const struct btp_hfp_ag_last_dialed_number_cmd *cp = cmd;
	int err;

	if (hfp_ag == NULL) {
		return BTP_STATUS_FAILED;
	}

	memset(last_number, 0, sizeof(last_number));
	memcpy(last_number, cp->number, MIN(cp->number_len, sizeof(last_number) - 1));
	return BTP_STATUS_SUCCESS;
}

/* BTP API COMPLETION */

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
		.opcode = BTP_HFP_VERIFY_NETWORK_OPERATOR,
		.expect_len = sizeof(struct btp_hfp_verify_network_operator_cmd),
		.func = verify_network_operator
	},

	{
		.opcode = BTP_HFP_AG_DISABLE_CALL_EXTERNAL,
		.expect_len = sizeof(struct btp_hfp_ag_disable_call_external_cmd),
		.func = ag_disable_call_external
	},

	{
		.opcode = BTP_HFP_HF_ANSWER_CALL,
		.expect_len = sizeof(struct btp_hfp_hf_answer_call_cmd),
		.func = hf_answer_call
	},

	{
		.opcode = BTP_HFP_VERIFY,
		.expect_len = sizeof(struct btp_hfp_verify_cmd),
		.func = verify
	},

	{
		.opcode = BTP_HFP_VERIFY_VOICE_TAG,
		.expect_len = sizeof(struct btp_hfp_verify_voice_tag_cmd),
		.func = verify_voice_tag
	},

	{
		.opcode = BTP_HFP_SPEAKER_MIC_VOLUME_SEND,
		.expect_len = sizeof(struct btp_hfp_speaker_mic_volume_send_cmd),
		.func = speaker_mic_volume_send
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
	{
		.opcode = BTP_HFP_DISABLE_NETWORK,
		.expect_len = sizeof(struct btp_hfp_disable_network_cmd),
		.func = disable_network
	},
	{
		.opcode = BTP_HFP_ENABLE_NETWORK,
		.expect_len = sizeof(struct btp_hfp_enable_network_cmd),
		.func = enable_network
	},
	{
		.opcode = BTP_HFP_MAKE_ROAM_ACTIVE,
		.expect_len = sizeof(struct btp_hfp_make_roam_active_cmd),
		.func = make_roam_active
	},
	{
		.opcode = BTP_HFP_MAKE_ROAM_INACTIVE,
		.expect_len = sizeof(struct btp_hfp_make_roam_inactive_cmd),
		.func = make_roam_inactive
	},

	{
		.opcode = BTP_HFP_MAKE_BATTERY_NOT_FULL_CHARGED,
		.expect_len = sizeof(struct btp_hfp_make_battery_not_full_charged_cmd),
		.func = make_battery_not_full_charged
	},

	{
		.opcode = BTP_HFP_MAKE_BATTERY_FULL_CHARGED,
		.expect_len = sizeof(struct btp_hfp_make_battery_full_charged_cmd),
		.func = make_battery_full_charged
	},

	{
		.opcode = BTP_HFP_VERIFY_BATTERY_CHARGED,
		.expect_len = sizeof(struct btp_hfp_verify_battery_charged_cmd),
		.func = verify_battery_charged
	},

	{
		.opcode = BTP_HFP_VERIFY_BATTERY_DISCHARGED,
		.expect_len = sizeof(struct btp_hfp_verify_battery_discharged_cmd),
		.func = verify_battery_discharged
	},

	{
		.opcode = BTP_HFP_SPEAKER_MIC_VOLUME_VERIFY,
		.expect_len = sizeof(struct btp_hfp_speaker_mic_volume_verify_cmd),
		.func = speaker_mic_volume_verify
	},

	{
		.opcode = BTP_HFP_AG_REGISTER,
		.expect_len = sizeof(struct btp_hfp_ag_register_cmd),
		.func = ag_register
	},

	{
		.opcode = BTP_HFP_HF_REGISTER,
		.expect_len = sizeof(struct btp_hfp_hf_register_cmd),
		.func = hf_register
	},

	{
		.opcode = BTP_HFP_VERIFY_ROAM_ACTIVE,
		.expect_len = sizeof(struct btp_hfp_verify_roam_active_cmd),
		.func = verify_roam_active
	},

	{
		.opcode = BTP_HFP_QUERY_NETWORK_OPERATOR,
		.expect_len = sizeof(struct btp_hfp_query_network_operator_cmd),
		.func = query_network_operator
	},

	{
		.opcode = BTP_HFP_AG_VRE_TEXT,
		.expect_len = sizeof(struct btp_hfp_ag_vre_text_cmd),
		.func = ag_vre_text
	},

	{
		.opcode = BTP_HFP_DTMF_CODE_SEND,
		.expect_len = sizeof(struct btp_hfp_dtmf_code_send_cmd),
		.func = dtmf_code_send
	},

	{
		.opcode = BTP_HFP_VERIFY_ROAM_INACTIVE,
		.expect_len = sizeof(struct btp_hfp_verify_roam_inactive_cmd),
		.func = verify_roam_inactive
	},

	{
		.opcode = BTP_HFP_PRIVATE_CONSULTATION_MODE,
		.expect_len = sizeof(struct btp_hfp_private_consultation_mode_cmd),
		.func = private_consultation_mode
	},

	{
		.opcode = BTP_HFP_RELEASE_SPECIFIED_CALL,
		.expect_len = sizeof(struct btp_hfp_release_specified_call_cmd),
		.func = release_specified_call
	},

	{
		.opcode = BTP_HFP_SET_ONGOING_CALLS,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = ag_set_ongoing_calls
	},
	{
		.opcode = BTP_HFP_HOLD_INCOMING,
		.expect_len = sizeof(struct btp_hfp_ag_hold_incoming_cmd),
		.func = ag_hold_incoming
	},

	{
		.opcode = BTP_HFP_LAST_DIALED_NUMBER,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = ag_last_dialed_number
	},
/* BTP BONDING COMPLETION */
};

uint8_t tester_init_hfp(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_HFP, hfp_handlers,
					 ARRAY_SIZE(hfp_handlers));

	hf_accept_call = false;
	hf_check_signal_strength = 5;

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_hfp(void)
{
	return BTP_STATUS_SUCCESS;
}
