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

#define BTP_HFP_ENABLE_SLC  0x02
struct btp_hfp_enable_slc_cmd {
	bt_addr_le_t address;
	uint8_t channel;
	uint8_t is_ag;
	uint8_t flags;
} __packed;

struct btp_hfp_enable_slc_rp {
	uint8_t connection_id;
} __packed;

#define BTP_HFP_DISABLE_SLC  0x03
struct btp_hfp_disable_slc_cmd {
	uint8_t flags;
} __packed;

struct btp_hfp_disable_slc_rp {
	
} __packed;

#define BTP_HFP_SIGNAL_STRENGTH_SEND  0x04
struct btp_hfp_signal_strength_send_cmd {
	uint8_t strength;
	uint8_t flags;
} __packed;

struct btp_hfp_signal_strength_send_rp {
	
} __packed;

#define BTP_HFP_CONTROL  0x05
struct btp_hfp_control_cmd {
	uint8_t control_type;
	uint8_t value;
	uint8_t flags;
} __packed;

struct btp_hfp_control_rp {
	
} __packed;

enum btp_hfp_control_type
{
	HFP_ENABLE_INBAND_RING        = 0x01,
	HFP_DISABLE_CALL              = 0x02,
	HFP_MUTE_INBAND_RING          = 0x03,
	HFP_AG_ANSWER_CALL            = 0x04,
	HFP_REJECT_CALL               = 0x05,
	HFP_OUT_CALL                  = 0x06,
	HFP_CLS_MEM_CALL_LIST         = 0x07,
	HFP_OUT_MEM_CALL              = 0x08,
	HFP_OUT_MEM_OUTOFRANGE_CALL   = 0x9,
	HFP_OUT_LAST_CALL             = 0xa,
	HFP_TWC_CALL                  = 0xb,
	HFP_END_SECOND_CALL           = 0xc,
	HFP_DISABLE_ACTIVE_CALL       = 0xd,
	HFP_HELD_ACTIVE_CALL          = 0xe,
	HFP_EC_NR_DISABLE             = 0xf,
	HFP_ENABLE_BINP               = 0x10,
	HFP_ENABLE_SUB_NUMBER         = 0x11,
	HFP_ENABLE_VR                 = 0x12,
	HFP_DISABLE_VR                = 0x13,
	HFP_QUERY_LIST_CALL           = 0x14,
	HFP_REJECT_HELD_CALL          = 0x15,
	HFP_ACCEPT_HELD_CALL          = 0x16,
	HFP_ACCEPT_INCOMING_HELD_CALL = 0x17,
	HFP_SEND_IIA                  = 0x18,
	HFP_SEND_BCC                  = 0x19,
	HFP_DIS_CTR_CHN               = 0x1a,
	HFP_IMPAIR_SIGNAL             = 0x1b,
	HFP_JOIN_CONVERSATION_CALL    = 0x1c,
	HFP_EXPLICIT_TRANSFER_CALL    = 0x1d,
	HFP_ENABLE_CLIP               = 0x1e,
	HFP_DISABLE_IN_BAND           = 0x1f,
	HFP_DISCOVER_START            = 0x20,
	HFP_INTG_HELD_CALL            = 0x21,
	HFP_SEND_BCC_WBS              = 0x22,
};

#define BTP_HFP_SIGNAL_STRENGTH_VERIFY  0x06
struct btp_hfp_signal_strength_verify_cmd {
	uint8_t strength;
	uint8_t flags;
} __packed;

struct btp_hfp_signal_strength_verify_rp {
	
} __packed;

#define BTP_HFP_AG_ENABLE_CALL  0x07
struct btp_hfp_ag_enable_call_cmd {
	uint8_t flags;
} __packed;

struct btp_hfp_ag_enable_call_rp {
	
} __packed;

#define BTP_HFP_AG_DISCOVERABLE  0x08
struct btp_hfp_ag_discoverable_cmd {
	uint8_t flags;
} __packed;

struct btp_hfp_ag_discoverable_rp {
	
} __packed;

#define BTP_HFP_HF_DISCOVERABLE  0x09
struct btp_hfp_hf_discoverable_cmd {
	uint8_t flags;
} __packed;

struct btp_hfp_hf_discoverable_rp {
	
} __packed;

#define BTP_HFP_ENABLE_AUDIO  0x10
struct btp_hfp_enable_audio_cmd {
	uint8_t flags;
} __packed;

struct btp_hfp_enable_audio_rp {
	
} __packed;

#define BTP_HFP_DISABLE_AUDIO  0x11
struct btp_hfp_disable_audio_cmd {
	uint8_t flags;
} __packed;

struct btp_hfp_disable_audio_rp {
	
} __packed;

#define BTP_HFP_EV_SCO_CONNECTED  0x81

struct btp_hfp_sco_connected_ev {
	
} __packed;

#define BTP_HFP_EV_SCO_DISCONNECTED  0x82

struct btp_hfp_sco_disconnected_ev {
	
} __packed;
