/* btp_hfp.h - Bluetooth HFP tester headers */

/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
/* HFP commands */
#define BTP_HFP_READ_SUPPORTED_COMMANDS	0x01

struct btp_hfp_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_HFP_AG_CONNECT                 0x02
struct btp_hfp_ag_connect_cmd {
    bt_addr_le_t address;
    uint8_t channel;
} __packed;

struct btp_hfp_ag_connect_rp {
    uint8_t connection_id;
} __packed;
