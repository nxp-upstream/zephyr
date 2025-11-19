/* btp_hfp.h - Bluetooth HFP tester headers */

/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* HFP commands */
#define BTP_HFP_READ_SUPPORTED_COMMANDS  0x01

struct btp_hfp_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_HFP_AG_ENABLE_SLC  0x02
struct btp_hfp_ag_enable_slc_cmd {
	bt_addr_le_t address;
	uint8_t channel;
} __packed;

struct btp_hfp_ag_enable_slc_rp {
	uint8_t connection_id;
} __packed;

#define BTP_HFP_HF_ENABLE_SLC  0x03
struct btp_hfp_hf_enable_slc_cmd {
	bt_addr_le_t address;
	uint8_t channel;
} __packed;

struct btp_hfp_hf_enable_slc_rp {
	uint8_t connection_id;	
} __packed;

#define BTP_HFP_AG_DISABLE_SLC  0x04

#define BTP_HFP_HF_DISABLE_SLC  0x05

#define BTP_HFP_AG_SIGNAL_STRENGTH_SEND  0x06
struct btp_hfp_ag_signal_strength_send_cmd {
	uint8_t strength;
} __packed;

#define BTP_HFP_SIGNAL_STRENGTH_VERIFY  0x07
struct btp_hfp_signal_strength_verify_cmd {
	uint8_t strength;
} __packed;

#define BTP_HFP_AG_ENABLE_CALL  0x08

#define BTP_HFP_AG_DISCOVERABLE  0x09

#define BTP_HFP_HF_DISCOVERABLE  0x0A

#define BTP_HFP_VERIFY_NETWORK_OPERATOR  0x0B
struct btp_hfp_verify_network_operator_cmd {
	uint8_t len;
	char op[16];
} __packed;

#define BTP_HFP_AG_DISABLE_CALL_EXTERNAL  0x0C

#define BTP_HFP_HF_ANSWER_CALL  0x0D
struct btp_hfp_hf_answer_call_rp {

} __packed;


#define BTP_HFP_VERIFY  0x0E
enum btp_hfp_verify_type
{
	HFP_VERIFY_INBAND_RING = 0x01,
	HFP_VERIFY_INBAND_RING_MUTING = 0x02,
	HFP_VERIFY_IUT_ALERTING = 0x03,
	HFP_VERIFY_IUT_NOT_ALERTING = 0x04,
	HFP_VERIFY_EC_NR_DISABLED = 0x05,
};
struct btp_hfp_verify_cmd {
	uint8_t verify_type;
} __packed;

struct btp_hfp_verify_rp {

} __packed;


#define BTP_HFP_VERIFY_VOICE_TAG  0x0F
struct btp_hfp_verify_voice_tag_cmd {
	uint8_t voice_tag;
} __packed;

struct btp_hfp_verify_voice_tag_rp {

} __packed;

#define BTP_HFP_AG_SPEAKER_VOLUME_SEND  0x10
struct btp_hfp_ag_speaker_volume_send_cmd {
	uint8_t volume;
} __packed;

struct btp_hfp_ag_speaker_volume_send_rp {
	
} __packed;

#define BTP_HFP_AG_MIC_VOLUME_SEND  0x11
struct btp_hfp_ag_mic_volume_send_cmd {
	uint8_t volume;
} __packed;

struct btp_hfp_ag_mic_volume_send_rp {
	
} __packed;

#define BTP_HFP_HF_SPEAKER_VOLUME_SEND  0x12
struct btp_hfp_hf_speaker_volume_send_cmd {
	uint8_t volume;
} __packed;

struct btp_hfp_hf_speaker_volume_send_rp {
	
} __packed;

#define BTP_HFP_HF_MIC_VOLUME_SEND  0x13
struct btp_hfp_hf_mic_volume_send_cmd {
	uint8_t volume;
} __packed;

struct btp_hfp_hf_mic_volume_send_rp {
	
} __packed;

#define BTP_HFP_AG_ENABLE_AUDIO  0x14

struct btp_hfp_enable_audio_rp {

} __packed;

#define BTP_HFP_HF_ENABLE_AUDIO  0x15

struct btp_hfp_hf_enable_audio_rp {
	
} __packed;

#define BTP_HFP_DISABLE_AUDIO  0x16
struct btp_hfp_disable_audio_cmd {
} __packed;

struct btp_hfp_disable_audio_rp {

} __packed;

#define BTP_HFP_AG_ENABLE_NETWORK  0x17

#define BTP_HFP_AG_DISABLE_NETWORK  0x18

struct btp_hfp_ag_disable_network_rp {
	
} __packed;

#define BTP_HFP_AG_MAKE_ROAM_ACTIVE  0x19

struct btp_hfp_ag_make_roam_active_rp {
	
} __packed;

#define BTP_HFP_AG_MAKE_ROAM_INACTIVE  0x1A

struct btp_hfp_ag_make_roam_inactive_rp {
	
} __packed;

#define BTP_HFP_AG_MAKE_BATTERY_NOT_FULL_CHARGED  0x1B

struct btp_hfp_ag_make_battery_not_full_charged_rp {
	
} __packed;


#define BTP_HFP_AG_MAKE_BATTERY_FULL_CHARGED  0x1C

struct btp_hfp_ag_make_battery_full_charged_rp {
	
} __packed;


#define BTP_HFP_VERIFY_BATTERY_CHARGED  0x1D

struct btp_hfp_verify_battery_charged_rp {
	
} __packed;


#define BTP_HFP_VERIFY_BATTERY_DISCHARGED  0x1E

struct btp_hfp_verify_battery_discharged_rp {
	
} __packed;

#define BTP_HFP_SPEAKER_VOLUME_VERIFY  0x1F
struct btp_hfp_speaker_volume_verify_cmd {
	uint8_t volume;
} __packed;

struct btp_hfp_speaker_volume_verify_rp {
	
} __packed;

#define BTP_HFP_MIC_VOLUME_VERIFY  0x20
struct btp_hfp_mic_volume_verify_cmd {
	uint8_t volume;
} __packed;

struct btp_hfp_mic_volume_verify_rp {
	
} __packed;

#define BTP_HFP_AG_REGISTER  0x21

struct btp_hfp_ag_register_rp {

} __packed;


#define BTP_HFP_HF_REGISTER  0x22

struct btp_hfp_hf_register_rp {

} __packed;


#define BTP_HFP_VERIFY_ROAM_ACTIVE  0x23

struct btp_hfp_verify_roam_active_rp {

} __packed;


#define BTP_HFP_HF_QUERY_NETWORK_OPERATOR  0x24

struct btp_hfp_hf_query_network_operator_rp {
	
} __packed;


#define BTP_HFP_AG_VRE_TEXT  0x25
struct btp_hfp_ag_vre_text_cmd {
	uint8_t status;
	uint16_t id;
	uint8_t type;
	uint8_t operation;
	uint32_t delay;
} __packed;

struct btp_hfp_ag_vre_text_rp {

} __packed;

#define BTP_HFP_HF_DTMF_CODE_SEND  0x26
struct btp_hfp_hf_dtmf_code_send_cmd {
	char dtmf_code;
} __packed;

struct btp_hfp_hf_dtmf_code_send_rp {
	
} __packed;

#define BTP_HFP_VERIFY_ROAM_INACTIVE  0x27

struct btp_hfp_verify_roam_inactive_rp {

} __packed;

#define BTP_HFP_HF_PRIVATE_CONSULTATION_MODE  0x28
struct btp_hfp_hf_private_consultation_mode_cmd {
	uint8_t index;
} __packed;

struct btp_hfp_hf_private_consultation_mode_rp {
	
} __packed;

#define BTP_HFP_HF_RELEASE_SPECIFIED_CALL  0x29
struct btp_hfp_hf_release_specified_call_cmd {
	uint8_t index;
} __packed;

struct btp_hfp_hf_release_specified_call_rp {
	
} __packed;


#define BTP_HFP_AG_SET_ONGOING_CALLS  0x2A
struct btp_hfp_ag_set_ongoing_calls_cmd {
	uint8_t type;
	uint8_t status;
	uint8_t dir;
	uint8_t all;
	uint8_t number_len;
	char number[];
} __packed;

struct btp_hfp_ag_set_ongoing_calls_rp {

} __packed;

#define BTP_HFP_AG_HOLD_INCOMING  0x2B
struct btp_hfp_ag_hold_incoming_cmd {
} __packed;

struct btp_hfp_ag_hold_incoming_rp {

} __packed;

#define BTP_HFP_AG_LAST_DIALED_NUMBER  0x2C
struct btp_hfp_ag_last_dialed_number_cmd {
	uint8_t type;
	uint8_t number_len;
	char number[];
} __packed;

#define BTP_HFP_AG_ANSWER_CALL  0x2D
struct btp_hfp_ag_answer_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_answer_call_rp {

} __packed;

#define BTP_HFP_AG_REJECT_CALL  0x2E
struct btp_hfp_ag_reject_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_reject_call_rp {

} __packed;

#define BTP_HFP_HF_REJECT_CALL  0x2F
struct btp_hfp_hf_reject_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_hf_reject_call_rp {

} __packed;

#define BTP_HFP_AG_END_CALL  0x30
struct btp_hfp_ag_end_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_end_call_rp {

} __packed;

#define BTP_HFP_HF_END_CALL  0x31
struct btp_hfp_hf_end_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_hf_end_call_rp {

} __packed;

#define BTP_HFP_AG_DISABLE_INBAND  0x32

struct btp_hfp_ag_disable_inband_rp {

} __packed;

#define BTP_HFP_AG_ENABLE_INBAND  0x33

struct btp_hfp_ag_enable_inband_rp {

} __packed;

#define BTP_HFP_AG_TWC_CALL  0x34

struct btp_hfp_ag_twc_call_rp {

} __packed;

#define BTP_HFP_AG_ENABLE_VR  0x35

struct btp_hfp_ag_enable_vr_rp {

} __packed;

#define BTP_HFP_HF_ENABLE_VR  0x36

struct btp_hfp_hf_enable_vr_rp {

} __packed;

#define BTP_HFP_AG_SEND_BCC  0x37

struct btp_hfp_ag_send_bcc_rp {

} __packed;

#define BTP_HFP_HF_SEND_BCC  0x38
struct btp_hfp_hf_send_bcc_rp {

} __packed;

#define BTP_HFP_AG_SEND_BCC_MSBC  0x39

struct btp_hfp_ag_send_bcc_msbc_rp {

} __packed;

#define BTP_HFP_HF_SEND_BCC_MSBC  0x3A

struct btp_hfp_hf_send_bcc_msbc_rp {

} __packed;

#define BTP_HFP_AG_SEND_BCC_SWB  0x3B

struct btp_hfp_ag_send_bcc_swb_rp {

} __packed;

#define BTP_HFP_HF_SEND_BCC_SWB  0x3C

struct btp_hfp_hf_send_bcc_swb_rp {

} __packed;

#define BTP_HFP_CLS_MEM_CALL_LIST  0x3D

struct btp_hfp_cls_mem_call_list_rp {

} __packed;

#define BTP_HFP_HF_ACCEPT_HELD_CALL  0x3E
struct btp_hfp_hf_accept_held_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_hf_accept_held_call_rp {

} __packed;

#define BTP_HFP_HF_HELD_ACTIVE_CALL  0x3F

struct btp_hfp_hf_held_active_call_rp {

} __packed;

#define BTP_HFP_AG_ACCEPT_INCOMING_HELD_CALL  0x40
struct btp_hfp_ag_accept_incoming_held_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_accept_incoming_held_call_rp {

} __packed;

#define BTP_HFP_HF_ACCEPT_INCOMING_HELD_CALL  0x41
struct btp_hfp_hf_accept_incoming_held_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_hf_accept_incoming_held_call_rp {

} __packed;

#define BTP_HFP_AG_REJECT_HELD_CALL  0x42
struct btp_hfp_ag_reject_held_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_reject_held_call_rp {

} __packed;

#define BTP_HFP_HF_REJECT_HELD_CALL  0x43
struct btp_hfp_hf_reject_held_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_hf_reject_held_call_rp {

} __packed;

#define BTP_HFP_AG_OUT_CALL  0x44

struct btp_hfp_ag_out_call_rp {

} __packed;

#define BTP_HFP_HF_OUT_CALL  0x45

struct btp_hfp_hf_out_call_rp {

} __packed;

#define BTP_HFP_HF_ENABLE_CLIP  0x46

struct btp_hfp_hf_enable_clip_rp {

} __packed;

#define BTP_HFP_HF_SEND_IIA  0x47
struct btp_hfp_hf_send_iia_rp {

} __packed;

#define BTP_HFP_HF_ENABLE_SUB_NUMBER  0x48

struct btp_hfp_hf_enable_sub_number_rp {

} __packed;

#define BTP_HFP_HF_OUT_MEM_CALL  0x49

struct btp_hfp_hf_out_mem_call_rp {

} __packed;

#define BTP_HFP_HF_OUT_MEM_OUTOFRANGE_CALL  0x4A

struct btp_hfp_hf_out_mem_outofrange_call_rp {

} __packed;

#define BTP_HFP_HF_EC_NR_DISABLE  0x4B

struct btp_hfp_hf_ec_nr_disable_rp {

} __packed;

#define BTP_HFP_AG_DIASBLE_VR  0x4C

struct btp_hfp_ag_diasble_vr_rp {

} __packed;

#define BTP_HFP_HF_DISABLE_VR  0x4D

struct btp_hfp_hf_disable_vr_rp {

} __packed;

#define BTP_HFP_HF_ENABLE_BINP  0x4E

struct btp_hfp_hf_enable_binp_rp {

} __packed;

#define BTP_HFP_AG_JOIN_CONVERSATION_CALL  0x4F

struct btp_hfp_ag_join_conversation_call_rp {

} __packed;

#define BTP_HFP_HF_JOIN_CONVERSATION_CALL  0x50

struct btp_hfp_hf_join_conversation_call_rp {

} __packed;

#define BTP_HFP_HF_EXPLICIT_TRANSFER_CALL  0x51

struct btp_hfp_hf_explicit_transfer_call_rp {

} __packed;

#define BTP_HFP_HF_OUT_LAST_CALL  0x52

struct btp_hfp_hf_out_last_call_rp {

} __packed;

#define BTP_HFP_HF_DISABLE_ACTIVE_CALL  0x53

struct btp_hfp_hf_disable_active_call_rp {

} __packed;

#define BTP_HFP_HF_END_SECOND_CALL  0x54
struct btp_hfp_hf_end_second_call_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_hf_end_second_call_rp {

} __packed;

#define BTP_HFP_MUTE_INBAND_RING  0x55

struct btp_hfp_mute_inband_ring_rp {

} __packed;

#define BTP_HFP_AG_REMOTE_REJECT  0x56
struct btp_hfp_ag_remote_reject_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_remote_reject_rp {

} __packed;

#define BTP_HFP_AG_REMOTE_RING  0x57
struct btp_hfp_ag_remote_ring_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_remote_ring_rp {

} __packed;

#define BTP_HFP_AG_HOLD  0x58
struct btp_hfp_ag_hold_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_hold_rp {

} __packed;

#define BTP_HFP_AG_RETRIEVE  0x59
struct btp_hfp_ag_retrieve_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_retrieve_rp {

} __packed;

#define BTP_HFP_AG_VER_STATE  0x5A
struct btp_hfp_ag_ver_state_cmd {
	uint8_t value;
} __packed;

struct btp_hfp_ag_ver_state_rp {

} __packed;

#define BTP_HFP_HF_INDICATOR_VALUE  0x5B
struct btp_hfp_hf_indicator_value_cmd {
	uint8_t flag;
	uint8_t value;
} __packed;

struct btp_hfp_hf_indicator_value_rp {

} __packed;

#define BTP_HFP_HF_READY_ACCEPT_AUDIO  0x5C

struct btp_hfp_hf_ready_accept_audio_rp {

} __packed;

#define BTP_HFP_HF_IMPAIR_SIGNAL  0x5D

struct btp_hfp_hf_impair_signal_rp {

} __packed;

#define BTP_HFP_AG_SET_LAST_NUM  0x5E
struct btp_hfp_ag_set_last_num_rp {

} __packed;

/* BTP HEAD FILE */

#define BTP_HFP_EV_SCO_CONNECTED  0x81

struct btp_hfp_sco_connected_ev {

} __packed;

#define BTP_HFP_EV_SCO_DISCONNECTED  0x82

struct btp_hfp_sco_disconnected_ev {

} __packed;

#define BTP_HFP_EV_NEW_CALL  0x83

#define BTP_HFP_CALL_DIR_OUTGOING 0x00
#define BTP_HFP_CALL_DIR_INCOMING 0x01
struct btp_hfp_new_call_ev {
	uint8_t index;
	uint8_t type;
	uint8_t dir;
	uint8_t number_len;
	uint8_t number[];
} __packed;

#define BTP_HFP_EV_CALL_STATUS  0x84

#define BTP_HFP_CALL_STATUS_ACTIVE        0x00
#define BTP_HFP_CALL_STATUS_HELD          0x01
#define BTP_HFP_CALL_STATUS_DIALING       0x02
#define BTP_HFP_CALL_STATUS_ALERTING      0x03
#define BTP_HFP_CALL_STATUS_INCOMING      0x04
#define BTP_HFP_CALL_STATUS_WAITING       0x05
#define BTP_HFP_CALL_STATUS_INCOMING_HELD 0x06
#define BTP_HFP_CALL_STATUS_REJECTED      0x07
#define BTP_HFP_CALL_STATUS_TERMINATED    0x08
struct btp_hfp_call_status_ev {
	uint8_t index;
	uint8_t status;
} __packed;
