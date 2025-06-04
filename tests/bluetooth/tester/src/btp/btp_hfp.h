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
