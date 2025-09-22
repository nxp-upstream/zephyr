/** @file
 *  @brief Audio Video Remote Control Profile shell functions.
 */

/*
 * Copyright (c) 2024 Xiaomi InC.
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

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/classic/avrcp.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/l2cap.h>

#include <zephyr/shell/shell.h>

#include "host/shell/bt.h"
#include "common/bt_shell_private.h"

NET_BUF_POOL_DEFINE(avrcp_tx_pool, CONFIG_BT_MAX_CONN,
		    BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

NET_BUF_POOL_DEFINE(avrcp_big_tx_pool, CONFIG_BT_MAX_CONN,
		    1024, CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

#define FOLDER_NAME_HEX_BUF_LEN 80

struct bt_avrcp_ct *default_ct;
struct bt_avrcp_tg *default_tg;
static bool avrcp_ct_registered;
static bool avrcp_tg_registered;
static uint8_t local_tid;
static uint8_t tg_tid;
static uint8_t tg_cap_id;

struct bt_avrcp_media_attr_rsp {
	uint32_t attr_id;
	uint16_t charset_id;
	uint16_t attr_len;
	const uint8_t *attr_val;
} __packed;

static struct bt_avrcp_media_attr_rsp test_media_attrs[] = {
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_TITLE,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 11,
		.attr_val = (const uint8_t *)"Test Title",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_ARTIST,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 11,
		.attr_val = "Test Artist",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_ALBUM,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 10U,
		.attr_val = (const uint8_t *)"Test Album",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_TRACK_NUMBER,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 1,
		.attr_val = (const uint8_t *)"1",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_TOTAL_TRACKS,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 2U,
		.attr_val = (const uint8_t *)"10",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_GENRE,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 4U,
		.attr_val = (const uint8_t *)"Rock",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_PLAYING_TIME,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 6U,
		.attr_val = (const uint8_t *)"240000", /* 4 minutes in milliseconds */
	},
};

static struct bt_avrcp_media_attr_rsp large_media_attrs[] = {
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_TITLE,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 200U,
		.attr_val = (const uint8_t *)
		"This is a long title that is designed to test the fragmentation of the AVRCP.",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_ARTIST,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 250U,
		.attr_val = (const uint8_t *)
		"This is a very long artist name that is also designed to test fragmentation.",
	},
	{
		.attr_id = BT_AVRCP_MEDIA_ATTR_ALBUM,
		.charset_id = BT_AVRCP_CHARSET_UTF8,
		.attr_len = 100U,
		.attr_val = (const uint8_t *)
		"This is a long album name for testing fragmentation of AVRCP responses.",
	},
};

static uint8_t get_next_tid(void)
{
	uint8_t ret = local_tid;

	local_tid++;
	local_tid &= 0x0F;

	return ret;
}

static void avrcp_ct_connected(struct bt_conn *conn, struct bt_avrcp_ct *ct)
{
	bt_shell_print("AVRCP CT connected");
	default_ct = ct;
	local_tid = 0;
}

static void avrcp_ct_disconnected(struct bt_avrcp_ct *ct)
{
	bt_shell_print("AVRCP CT disconnected");
	local_tid = 0;
	default_ct = NULL;
}

static void avrcp_ct_browsing_connected(struct bt_conn *conn, struct bt_avrcp_ct *ct)
{
	bt_shell_print("AVRCP CT browsing connected");
}

static void avrcp_ct_browsing_disconnected(struct bt_avrcp_ct *ct)
{
	bt_shell_print("AVRCP CT browsing disconnected");
}

static void avrcp_get_caps_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
			       const struct bt_avrcp_get_caps_rsp *rsp)
{
	uint8_t i;

	switch (rsp->cap_id) {
	case BT_AVRCP_CAP_COMPANY_ID:
		for (i = 0; i < rsp->cap_cnt; i++) {
			bt_shell_print("Remote CompanyID = 0x%06x",
				       sys_get_be24(&rsp->cap[BT_AVRCP_COMPANY_ID_SIZE * i]));
		}
		break;
	case BT_AVRCP_CAP_EVENTS_SUPPORTED:
		for (i = 0; i < rsp->cap_cnt; i++) {
			bt_shell_print("Remote supported EventID = 0x%02x", rsp->cap[i]);
		}
		break;
	}
}

static void avrcp_unit_info_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
				struct bt_avrcp_unit_info_rsp *rsp)
{
	bt_shell_print("AVRCP unit info received, unit type = 0x%02x, company_id = 0x%06x",
		       rsp->unit_type, rsp->company_id);
}

static void avrcp_subunit_info_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
				   struct bt_avrcp_subunit_info_rsp *rsp)
{
	int i;

	bt_shell_print("AVRCP subunit info received, subunit type = 0x%02x, extended subunit = %d",
		       rsp->subunit_type, rsp->max_subunit_id);
	for (i = 0; i < rsp->max_subunit_id; i++) {
		bt_shell_print("extended subunit id = %d, subunit type = 0x%02x",
			       rsp->extended_subunit_id[i], rsp->extended_subunit_type[i]);
	}
}

static void avrcp_passthrough_rsp(struct bt_avrcp_ct *ct, uint8_t tid, bt_avrcp_rsp_t result,
				  const struct bt_avrcp_passthrough_rsp *rsp)
{
	if (result == BT_AVRCP_RSP_ACCEPTED) {
		bt_shell_print(
			"AVRCP passthough command accepted, operation id = 0x%02x, state = %d",
			BT_AVRCP_PASSTHROUGH_GET_OPID(rsp), BT_AVRCP_PASSTHROUGH_GET_STATE(rsp));
	} else {
		bt_shell_print("AVRCP passthough command rejected, operation id = 0x%02x, state = "
			       "%d, response = %d",
			       BT_AVRCP_PASSTHROUGH_GET_OPID(rsp),
			       BT_AVRCP_PASSTHROUGH_GET_STATE(rsp), result);
	}
}

static void avrcp_get_element_attrs_rsp(struct bt_avrcp_ct *ct, uint8_t tid,  bt_avrcp_rsp_t result,
					struct net_buf *buf)
{
	const struct bt_avrcp_get_element_attrs_rsp *rsp;
	struct bt_avrcp_media_attr *attr;
	uint8_t i = 0;
	const char *attr_name;

	if (buf->len < sizeof(*rsp)) {
		bt_shell_print("Invalid GetElementAttributes response length: %d", buf->len);
		return;
	}

	rsp = net_buf_pull_mem(buf, sizeof(*rsp));

	bt_shell_print("AVRCP GetElementAttributes response received, tid=0x%02x, num_attrs=%u",
		       tid, rsp->num_attrs);

	while (buf->len > 0) {
		if (buf->len < sizeof(struct bt_avrcp_media_attr)) {
			bt_shell_print("incompleted message");
			break;
		}
		attr = net_buf_pull_mem(buf, sizeof(struct bt_avrcp_media_attr));

		attr->attr_id = sys_be32_to_cpu(attr->attr_id);
		attr->charset_id = sys_be16_to_cpu(attr->charset_id);
		attr->attr_len = sys_be16_to_cpu(attr->attr_len);
		if (buf->len < attr->attr_len) {
			bt_shell_print("incompleted message for attr_len");
			break;
		}
		net_buf_pull_mem(buf, attr->attr_len);
		switch (attr->attr_id) {
		case BT_AVRCP_MEDIA_ATTR_TITLE:
			attr_name = "TITLE";
			break;
		case BT_AVRCP_MEDIA_ATTR_ARTIST:
			attr_name = "ARTIST";
			break;
		case BT_AVRCP_MEDIA_ATTR_ALBUM:
			attr_name = "ALBUM";
			break;
		case BT_AVRCP_MEDIA_ATTR_TRACK_NUMBER:
			attr_name = "TRACK_NUMBER";
			break;
		case BT_AVRCP_MEDIA_ATTR_TOTAL_TRACKS:
			attr_name = "TOTAL_TRACKS";
			break;
		case BT_AVRCP_MEDIA_ATTR_GENRE:
			attr_name = "GENRE";
			break;
		case BT_AVRCP_MEDIA_ATTR_PLAYING_TIME:
			attr_name = "PLAYING_TIME";
			break;
		default:
			attr_name = "UNKNOWN";
			break;
		}
		bt_shell_print(" Attr[%u]: ID=0x%08x (%s), charset=0x%04x, len=%u",
			       i, attr->attr_id, attr_name, attr->charset_id, attr->attr_len);

		/* Print attribute value (truncate if too long for display) */
		if (attr->attr_len > 0) {
			uint16_t print_len = (attr->attr_len > 64) ? 64 : attr->attr_len;
			char value_str[65];

			memcpy(value_str, attr->attr_val, print_len);
			value_str[print_len] = '\0';
			bt_shell_print("   Value: \"%s\"%s", value_str,
				       (attr->attr_len > 64) ? "..." : "");
		}
		i++;
	}
}

static void avrcp_get_element_attrs_req(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	struct bt_avrcp_get_element_attrs_cmd *cmd;
	uint8_t i;
	uint16_t expected_len = 0;

	tg_tid = tid;
	if (buf->len < sizeof(*cmd)) {
		bt_shell_print("Invalid GetElementAttributes command length: %d", buf->len);
		goto err_rsp;
	}

	cmd = net_buf_pull_mem(buf, sizeof(*cmd));

	expected_len = cmd->num_attrs * sizeof(uint32_t);
	if (buf->len < expected_len) {
		bt_shell_print("Invalid GetElementAttributes command attribute IDs length: %d, "
			       "expected %d",
			       buf->len, expected_len);
		goto err_rsp;
	}
	net_buf_pull_mem(buf, expected_len);
	cmd->identifier = sys_be64_to_cpu(cmd->identifier);

	bt_shell_print("AVRCP GetElementAttributes command received, tid=0x%02x", tid);
	bt_shell_print(" Identifier: 0x%016llx", cmd->identifier);
	bt_shell_print(" Num attrs requested: %u %s", cmd->num_attrs,
		       (cmd->num_attrs == 0U) ? "(all attributes)" : "");

	if (cmd->num_attrs > 0U) {
		bt_shell_print(" Requested attribute IDs:");
		for (i = 0U; i < cmd->num_attrs; i++) {
			const char *attr_name;

			cmd->attr_ids[i] = sys_be32_to_cpu(cmd->attr_ids[i]);
			switch (cmd->attr_ids[i]) {
			case BT_AVRCP_MEDIA_ATTR_TITLE:
				attr_name = "TITLE";
				break;
			case BT_AVRCP_MEDIA_ATTR_ARTIST:
				attr_name = "ARTIST";
				break;
			case BT_AVRCP_MEDIA_ATTR_ALBUM:
				attr_name = "ALBUM";
				break;
			case BT_AVRCP_MEDIA_ATTR_TRACK_NUMBER:
				attr_name = "TRACK_NUMBER";
				break;
			case BT_AVRCP_MEDIA_ATTR_TOTAL_TRACKS:
				attr_name = "TOTAL_TRACKS";
				break;
			case BT_AVRCP_MEDIA_ATTR_GENRE:
				attr_name = "GENRE";
				break;
			case BT_AVRCP_MEDIA_ATTR_PLAYING_TIME:
				attr_name = "PLAYING_TIME";
				break;
			default:
				attr_name = "UNKNOWN";
				break;
			}
			bt_shell_print("   [%u]: 0x%08x (%s)", i, cmd->attr_ids[i], attr_name);
		}
	}

err_rsp:
	return;
}

static void avrcp_notification_rsp(uint8_t event_id, struct bt_avrcp_event_data *data)
{
	const char *type_str = "CHANGED";

	bt_shell_print("AVRCP notification_rsp: type=%s, event_id=0x%02x", type_str, event_id);

	switch (event_id) {
	case BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED:
		bt_shell_print(" PLAYBACK_STATUS_CHANGED: status=0x%02x", data->play_status);
		break;
	case BT_AVRCP_EVT_TRACK_CHANGED:
		uint64_t identifier;

		memcpy(&identifier, data->identifier, sizeof(identifier));
		printf("TRACK_CHANGED:  identifier value: %llx\n", identifier);
		break;
	case BT_AVRCP_EVT_PLAYBACK_POS_CHANGED:
		bt_shell_print(" PLAYBACK_POS_CHANGED: pos=%u", data->playback_pos);
		break;
	case BT_AVRCP_EVT_BATT_STATUS_CHANGED:
		bt_shell_print(" BATT_STATUS_CHANGED: battery_status=0x%02x", data->battery_status);
		break;
	case BT_AVRCP_EVT_SYSTEM_STATUS_CHANGED:
		bt_shell_print(" SYSTEM_STATUS_CHANGED: system_status=0x%02x", data->system_status);
		break;
	case BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED:
		bt_shell_print(" PLAYER_APP_SETTING_CHANGED: num_of_attr=%u",
			       data->setting_changed.num_of_attr);
		break;
	case BT_AVRCP_EVT_ADDRESSED_PLAYER_CHANGED:
		bt_shell_print(" ADDRESSED_PLAYER_CHANGED: player_id=0x%04x, uid_counter=0x%04x",
			       data->addressed_player_changed.player_id,
			       data->addressed_player_changed.uid_counter);
		break;
	case BT_AVRCP_EVT_UIDS_CHANGED:
		bt_shell_print(" UIDS_CHANGED: uid_counter=0x%04x", data->uid_counter);
		break;
	case BT_AVRCP_EVT_VOLUME_CHANGED:
		bt_shell_print(" VOLUME_CHANGED: absolute_volume=0x%02x", data->absolute_volume);
		break;
	default:
		bt_shell_print(" Unknown event_id: 0x%02x", event_id);
		break;
	}
}

static void avrcp_register_notification_req(struct bt_avrcp_tg *tg, uint8_t tid,
					    bt_avrcp_evt_t event_id,
					    uint32_t playback_interval)
{
	bt_shell_print("AVRCP register_notification_req: tid=0x%02x, event_id=0x%02x, interval=%u",
		       tid, event_id, playback_interval);
	tg_tid = tid;
}

static void avrcp_set_absolute_volume_rsp(struct bt_avrcp_ct *ct, uint8_t tid, uint8_t rsp_code,
					  uint8_t absolute_volume)
{
	bt_shell_print("AVRCP set absolute volume rsp: tid=0x%02x, rsp=0x%02x, volume=0x%02x",
		       tid, rsp_code, absolute_volume);
}

static void avrcp_set_absolute_volume_req(struct bt_avrcp_tg *tg, uint8_t tid,
					  uint8_t absolute_volume)
{
	bt_shell_print("AVRCP set_absolute_volume_req: tid=0x%02x, absolute_volume=0x%02x",
		       tid, absolute_volume);
	tg_tid = tid;
}

static void avrcp_set_browsed_player_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
				     struct net_buf *buf)
{
	struct bt_avrcp_set_browsed_player_rsp *rsp;
	struct bt_avrcp_folder_name *folder_name;

	rsp = net_buf_pull_mem(buf, sizeof(*rsp));
	if (rsp->status != BT_AVRCP_STATUS_OPERATION_COMPLETED) {
		bt_shell_print("AVRCP set browsed player failed, tid = %d, status = 0x%02x",
			       tid, rsp->status);
		return;
	}

	bt_shell_print("AVRCP set browsed player success, tid = %d", tid);
	bt_shell_print("  UID Counter: %u", sys_be16_to_cpu(rsp->uid_counter));
	bt_shell_print("  Number of Items: %u", sys_be32_to_cpu(rsp->num_items));
	bt_shell_print("  Charset ID: 0x%04X", sys_be16_to_cpu(rsp->charset_id));
	bt_shell_print("  Folder Depth: %u", rsp->folder_depth);

	while (buf->len > 0) {
		if (buf->len < sizeof(struct bt_avrcp_folder_name)) {
			bt_shell_print("incompleted message");
			break;
		}
		folder_name = net_buf_pull_mem(buf, sizeof(struct bt_avrcp_folder_name));
		folder_name->folder_name_len = sys_be16_to_cpu(folder_name->folder_name_len);
		if (buf->len < folder_name->folder_name_len) {
			bt_shell_print("incompleted message for folder_name");
			break;
		}
		net_buf_pull_mem(buf, folder_name->folder_name_len);

		if (sys_be16_to_cpu(rsp->charset_id) == BT_AVRCP_CHARSET_UTF8) {
			bt_shell_print("Raw folder name:");
			for (int i = 0; i < folder_name->folder_name_len; i++) {
				bt_shell_print("%c", folder_name->folder_name[i]);
			}
		} else {
			bt_shell_print(" Get folder Name : ");
			bt_shell_hexdump(folder_name->folder_name, folder_name->folder_name_len);
		}
		if (rsp->folder_depth > 0) {
			rsp->folder_depth--;
		} else {
			bt_shell_warn("Folder depth is mismatched with received data");
			break;
		}
	}

	if (rsp->folder_depth > 0) {
		bt_shell_print("folder depth mismatch: expected 0, got %u", rsp->folder_depth);
	}
}

static const char *player_app_attr_name(uint8_t id)
{
	switch (id) {
	case 0x01: return "EQUALIZER";
	case 0x02: return "REPEAT_MODE";
	case 0x03: return "SHUFFLE";
	case 0x04: return "SCAN";
	default:   return "UNKNOWN";
	}
}

static void avrcp_list_player_app_setting_attrs_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
						    bt_avrcp_rsp_t result, struct net_buf *buf)
{
	struct bt_avrcp_list_app_setting_attr_rsp *rsp;

	rsp = net_buf_pull_mem(buf, sizeof(*rsp));

	while (buf->len > 0) {
		uint8_t attr = net_buf_pull_u8(buf);

		bt_shell_print(" attr =0x%02x (%s)",  attr, player_app_attr_name(attr));
		if (rsp->num_attrs > 0) {
			rsp->num_attrs--;
		} else {
			bt_shell_warn("num_attrs is mismatched with received data");
			break;
		}
	}

	if (rsp->num_attrs > 0) {
		bt_shell_print("num_attrs mismatch: expected 0, got %u", rsp->num_attrs);
	}
}

static void avrcp_list_player_app_setting_vals_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
						   bt_avrcp_rsp_t result, struct net_buf *buf)
{
	struct bt_avrcp_list_player_app_setting_vals_rsp *rsp;

	rsp = net_buf_pull_mem(buf, sizeof(*rsp));
	while (buf->len > 0) {
		uint8_t val = net_buf_pull_u8(buf);

		bt_shell_print(" val : %u", val);
		if (rsp->num_values > 0) {
			rsp->num_values--;
		} else {
			bt_shell_warn("num_values is mismatched with received data");
			break;
		}
	}

	if (rsp->num_values > 0) {
		bt_shell_print("num_values mismatch: expected 0, got %u", rsp->num_values);
	}
}

static void avrcp_get_curr_player_app_setting_val_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
						      bt_avrcp_rsp_t result, struct net_buf *buf)
{
	struct bt_avrcp_get_curr_player_app_setting_val_rsp *rsp;
	struct bt_avrcp_app_setting_attr_val attr_vals = {0};

	rsp = net_buf_pull_mem(buf, sizeof(*rsp));
	while (buf->len > 0) {

		if (buf->len < sizeof(struct bt_avrcp_app_setting_attr_val)) {
			bt_shell_print("incompleted message");
			break;
		}
		attr_vals.attr_id = net_buf_pull_u8(buf);
		attr_vals.value_id = net_buf_pull_u8(buf);

		bt_shell_print(" attr_id :%u val %u", attr_vals.attr_id, attr_vals.value_id);
		if (rsp->num_attrs > 0) {
			rsp->num_attrs--;
		} else {
			bt_shell_warn("num_attrs %d is mismatched with received", rsp->num_attrs);
			break;
		}
	}

	if (rsp->num_attrs > 0) {
		bt_shell_print("num_attrs mismatch: expected 0, got %u", rsp->num_attrs);
	}
}

static void avrcp_set_player_app_setting_val_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
						 bt_avrcp_rsp_t result)
{
	bt_shell_print("SetPlayerAppSettingValue rsp: tid=0x%02x, result=%u", tid, result);
}

static void avrcp_get_player_app_setting_attr_text_rsp(struct bt_avrcp_ct *ct, uint8_t tid,
						      bt_avrcp_rsp_t result, struct net_buf *buf)
{
	struct bt_avrcp_get_player_app_setting_attr_text_rsp *rsp;
	struct bt_avrcp_app_setting_attr_text *attr_text;

	rsp = net_buf_pull_mem(buf, sizeof(*rsp));

	while (buf->len > 0) {

		if (buf->len < sizeof(struct bt_avrcp_app_setting_attr_text)) {
			bt_shell_print("incompleted message");
			break;
		}
		attr_text = net_buf_pull_mem(buf, sizeof(struct bt_avrcp_app_setting_attr_text));
		attr_text->charset_id = sys_be16_to_cpu(attr_text->charset_id);

		bt_shell_print("attr=0x%02x, charset=0x%04x, text_len=%u", attr_text->attr_id,
			       attr_text->charset_id, attr_text->text_len);

		if (buf->len < attr_text->text_len) {
			bt_shell_print("incompleted message for attr_text");
			break;
		}
		net_buf_pull_mem(buf, attr_text->text_len);

		if (attr_text->charset_id == BT_AVRCP_CHARSET_UTF8) {
			bt_shell_print("Raw attr_text:");
			for (int i = 0; i < attr_text->text_len; i++) {
				bt_shell_print("%c", attr_text->text[i]);
			}
		} else {
			bt_shell_print(" Get attr_text : ");
			bt_shell_hexdump(attr_text->text, attr_text->text_len);
		}

		if (rsp->num_attrs > 0) {
			rsp->num_attrs--;
		} else {
			bt_shell_warn("num_attrs %d is mismatched with received", rsp->num_attrs);
			break;
		}
	}

	if (rsp->num_attrs > 0) {
		bt_shell_print("num_attrs mismatch: expected 0, got %u", rsp->num_attrs);
	}
}

static struct bt_avrcp_ct_cb app_avrcp_ct_cb = {
	.connected = avrcp_ct_connected,
	.disconnected = avrcp_ct_disconnected,
	.browsing_connected = avrcp_ct_browsing_connected,
	.browsing_disconnected = avrcp_ct_browsing_disconnected,
	.get_caps_rsp = avrcp_get_caps_rsp,
	.unit_info_rsp = avrcp_unit_info_rsp,
	.subunit_info_rsp = avrcp_subunit_info_rsp,
	.passthrough_rsp = avrcp_passthrough_rsp,
	.set_browsed_player_rsp = avrcp_set_browsed_player_rsp,
	.set_absolute_volume_rsp = avrcp_set_absolute_volume_rsp,
	.get_element_attrs_rsp = avrcp_get_element_attrs_rsp,
	.list_player_app_setting_attrs_rsp = avrcp_list_player_app_setting_attrs_rsp,
	.list_player_app_setting_vals_rsp  = avrcp_list_player_app_setting_vals_rsp,
	.get_curr_player_app_setting_val_rsp = avrcp_get_curr_player_app_setting_val_rsp,
	.set_player_app_setting_val_rsp = avrcp_set_player_app_setting_val_rsp,
	.get_player_app_setting_attr_text_rsp = avrcp_get_player_app_setting_attr_text_rsp,
};

static void avrcp_tg_connected(struct bt_conn *conn, struct bt_avrcp_tg *tg)
{
	bt_shell_print("AVRCP TG connected");
	default_tg = tg;
}

static void avrcp_tg_disconnected(struct bt_avrcp_tg *tg)
{
	bt_shell_print("AVRCP TG disconnected");
	default_tg = NULL;
}

static void avrcp_tg_browsing_connected(struct bt_conn *conn, struct bt_avrcp_tg *tg)
{
	bt_shell_print("AVRCP TG browsing connected");
}

static void avrcp_unit_info_req(struct bt_avrcp_tg *tg, uint8_t tid)
{
	bt_shell_print("AVRCP unit info request received");
	tg_tid = tid;
}

static void avrcp_subunit_info_req(struct bt_avrcp_tg *tg, uint8_t tid)
{
	bt_shell_print("AVRCP subunit info request received");
	tg_tid = tid;
}

static void avrcp_get_caps_req(struct bt_avrcp_tg *tg, uint8_t tid, uint8_t cap_id)
{
	const char *cap_type_str;

	/* Convert capability ID to string for display */
	switch (cap_id) {
	case BT_AVRCP_CAP_COMPANY_ID:
		cap_type_str = "COMPANY_ID";
		break;
	case BT_AVRCP_CAP_EVENTS_SUPPORTED:
		cap_type_str = "EVENTS_SUPPORTED";
		break;
	default:
		cap_type_str = "UNKNOWN";
		break;
	}

	bt_shell_print("AVRCP get capabilities command received: cap_id 0x%02x (%s), tid = 0x%02x",
		       cap_id, cap_type_str, tid);

	/* Store the transaction ID and capability ID for manual response testing */
	tg_tid = tid;
	tg_cap_id = cap_id;
}

static void avrcp_tg_browsing_disconnected(struct bt_avrcp_tg *tg)
{
	bt_shell_print("AVRCP TG browsing disconnected");
}

static void avrcp_set_browsed_player_req(struct bt_avrcp_tg *tg, uint8_t tid,
					 uint16_t player_id)
{
	bt_shell_print("AVRCP set browsed player request received, player_id = %u", player_id);
	tg_tid = tid;
}

static void avrcp_passthrough_req(struct bt_avrcp_tg *tg, uint8_t tid, struct net_buf *buf)
{
	struct bt_avrcp_passthrough_cmd *cmd;
	struct bt_avrcp_passthrough_opvu_data *opvu = NULL;
	const char *state_str;
	bt_avrcp_opid_t opid;
	bt_avrcp_button_state_t state;

	tg_tid = tid;
	cmd = net_buf_pull_mem(buf, sizeof(*cmd));
	opid = BT_AVRCP_PASSTHROUGH_GET_STATE(cmd);
	state = BT_AVRCP_PASSTHROUGH_GET_OPID(cmd);

	if (cmd->data_len > 0U) {
		if (buf->len < sizeof(struct bt_avrcp_passthrough_opvu_data)) {
			bt_shell_print("Invalid passthrough data: buf len %u < expected_len %zu",
				       buf->len, sizeof(struct bt_avrcp_passthrough_opvu_data));
			return;
		}

		if (buf->len < cmd->data_len) {
			bt_shell_print("Invalid passthrough cmd data length: %u, buf length = %u",
				       cmd->data_len, buf->len);
		}
		opvu = net_buf_pull_mem(buf, sizeof(*opvu));
	}

	/* Convert button state to string */
	state_str = (state == BT_AVRCP_BUTTON_PRESSED) ? "PRESSED" : "RELEASED";

	bt_shell_print("AVRCP passthrough command received: opid = 0x%02x (%s), tid=0x%02x, len=%u",
		       opid, state_str, tid, cmd->data_len);

	if (cmd->data_len > 0U && opvu != NULL) {
		bt_shell_print("company_id: 0x%06x", sys_get_be24(opvu->company_id));
		bt_shell_print("opid_vu: 0x%04x", sys_be16_to_cpu(opvu->opid_vu));
	}

}

static void avrcp_list_player_app_setting_attrs_req(struct bt_avrcp_tg *tg, uint8_t tid)
{
	tg_tid = tid;
	bt_shell_print("AVRCP TG: ListPlayerAppSettingAttributes, tid=0x%02x", tid);
}

static void avrcp_list_player_app_setting_vals_req(struct bt_avrcp_tg *tg, uint8_t tid,
						   uint8_t attr_id)
{
	tg_tid = tid;
	bt_shell_print("AVRCP TG: List App Setting vals, tid=0x%02x, attr_id=0x%02x", tid, attr_id);
}

static void avrcp_get_curr_player_app_setting_val_req(struct bt_avrcp_tg *tg,
						      uint8_t tid, struct net_buf *buf)
{
	struct bt_avrcp_get_curr_player_app_setting_val_cmd *cmd;

	cmd = net_buf_pull_mem(buf, sizeof(*cmd));

	tg_tid = tid;

	while (buf->len > 0) {
		uint8_t attr_ids = net_buf_pull_u8(buf);

		bt_shell_print(" attr_ids: %u", attr_ids);
		if (cmd->num_attrs > 0) {
			cmd->num_attrs--;
		} else {
			bt_shell_warn("num_attrs is mismatched with received data");
			break;
		}
	}

	if (cmd->num_attrs > 0) {
		bt_shell_print("num_values mismatch: expected 0, got %u", cmd->num_attrs);
	}
}

static void avrcp_set_player_app_setting_val_req(struct bt_avrcp_tg *tg, uint8_t tid,
						 struct net_buf *buf)
{
	struct bt_avrcp_set_player_app_setting_val_cmd *cmd;

	cmd = net_buf_pull_mem(buf, sizeof(*cmd));

	tg_tid = tid;
	if (buf->len < (uint16_t)(cmd->num_attrs * 2U)) {
		bt_shell_print("Invalid pairs: n=%u, remain=%u", cmd->num_attrs, buf->len);
		return;
	}

	bt_shell_print("AVRCP TG: SetPlayerApplicationSettingValue, tid=0x%02x, num=%u", tid,
		       cmd->num_attrs);
	for (uint8_t i = 0; i < cmd->num_attrs; i++) {
		cmd->attr_vals[i].attr_id = net_buf_pull_u8(buf);
		cmd->attr_vals[i].value_id = net_buf_pull_u8(buf);
		bt_shell_print(" pair[%u]: attr=0x%02x val=0x%02x", i, cmd->attr_vals[i].attr_id,
			      cmd->attr_vals[i].value_id);
	}
}

static void avrcp_get_player_app_setting_attr_text_req(struct bt_avrcp_tg *tg,
						       uint8_t tid, struct net_buf *buf)
{
	struct bt_avrcp_get_player_app_setting_attr_text_cmd *cmd;

	tg_tid = tid;
	cmd = net_buf_pull_mem(buf, sizeof(*cmd));

	if (buf->len < cmd->num_attrs) {
		bt_shell_print("Invalid AttrText list: n=%u remain=%u", cmd->num_attrs, buf->len);
		return;
	}
	bt_shell_print("GetPlayerAppSettingAttributeText, tid=0x%02x, num=%u", tid, cmd->num_attrs);
	for (uint8_t i = 0; i < cmd->num_attrs; i++) {
		bt_shell_print(" attr_id[%u]=0x%02x", i, net_buf_pull_u8(buf));
	}

}

static struct bt_avrcp_tg_cb app_avrcp_tg_cb = {
	.connected = avrcp_tg_connected,
	.disconnected = avrcp_tg_disconnected,
	.browsing_connected = avrcp_tg_browsing_connected,
	.browsing_disconnected = avrcp_tg_browsing_disconnected,
	.unit_info_req = avrcp_unit_info_req,
	.subunit_info_req = avrcp_subunit_info_req,
	.get_cap_req = avrcp_get_caps_req,
	.set_browsed_player_req = avrcp_set_browsed_player_req,
	.register_notification_req = avrcp_register_notification_req,
	.set_absolute_volume_req = avrcp_set_absolute_volume_req,
	.passthrough_req = avrcp_passthrough_req,
	.get_element_attrs_req = avrcp_get_element_attrs_req,
	.list_player_app_setting_attrs_req = avrcp_list_player_app_setting_attrs_req,
	.list_player_app_setting_vals_req = avrcp_list_player_app_setting_vals_req,
	.get_curr_player_app_setting_val_req = avrcp_get_curr_player_app_setting_val_req,
	.set_player_app_setting_val_req = avrcp_set_player_app_setting_val_req,
	.get_player_app_setting_attr_text_req = avrcp_get_player_app_setting_attr_text_req,
};

static int register_ct_cb(const struct shell *sh)
{
	int err;

	if (avrcp_ct_registered) {
		return 0;
	}

	err = bt_avrcp_ct_register_cb(&app_avrcp_ct_cb);
	if (!err) {
		avrcp_ct_registered = true;
		shell_print(sh, "AVRCP CT callbacks registered");
	} else {
		shell_print(sh, "failed to register AVRCP CT callbacks");
	}

	return err;
}

static int cmd_register_ct_cb(const struct shell *sh, int32_t argc, char *argv[])
{
	if (avrcp_ct_registered) {
		shell_print(sh, "already registered");
		return 0;
	}

	register_ct_cb(sh);

	return 0;
}

static int register_tg_cb(const struct shell *sh)
{
	int err;

	if (avrcp_tg_registered) {
		return 0;
	}

	err = bt_avrcp_tg_register_cb(&app_avrcp_tg_cb);
	if (!err) {
		avrcp_tg_registered = true;
		shell_print(sh, "AVRCP TG callbacks registered");
	} else {
		shell_print(sh, "failed to register AVRCP TG callbacks");
	}

	return err;
}

static int cmd_register_tg_cb(const struct shell *sh, int32_t argc, char *argv[])
{
	if (avrcp_tg_registered) {
		shell_print(sh, "already registered");
		return 0;
	}

	register_tg_cb(sh);

	return 0;
}

static int cmd_connect(const struct shell *sh, int32_t argc, char *argv[])
{
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (!default_conn) {
		shell_error(sh, "BR/EDR not connected");
		return -ENOEXEC;
	}

	err = bt_avrcp_connect(default_conn);
	if (err < 0) {
		shell_error(sh, "fail to connect AVRCP");
	}

	return 0;
}

static int cmd_disconnect(const struct shell *sh, int32_t argc, char *argv[])
{
	if ((!avrcp_ct_registered) && (!avrcp_tg_registered)) {
		shell_error(sh, "Neither CT nor TG callbacks are registered.");
		return -ENOEXEC;
	}

	if (!default_conn) {
		shell_print(sh, "Not connected");
		return -ENOEXEC;
	}

	if ((default_ct != NULL) || (default_tg != NULL)) {
		bt_avrcp_disconnect(default_conn);
	} else {
		shell_error(sh, "AVRCP is not connected");
	}

	return 0;
}

static int cmd_browsing_connect(const struct shell *sh, int32_t argc, char *argv[])
{
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_conn == NULL) {
		shell_error(sh, "BR/EDR not connected");
		return -ENOEXEC;
	}

	err = bt_avrcp_browsing_connect(default_conn);
	if (err < 0) {
		shell_error(sh, "fail to connect AVRCP browsing");
	} else {
		shell_print(sh, "AVRCP browsing connect request sent");
	}

	return err;
}

static int cmd_browsing_disconnect(const struct shell *sh, int32_t argc, char *argv[])
{
	int err;

	if (default_conn == NULL) {
		shell_print(sh, "Not connected");
		return -ENOEXEC;
	}

	if ((default_ct != NULL) || (default_tg != NULL)) {
		err = bt_avrcp_browsing_disconnect(default_conn);
		if (err < 0) {
			shell_error(sh, "fail to disconnect AVRCP browsing");
		} else {
			shell_print(sh, "AVRCP browsing disconnect request sent");
		}
	} else {
		shell_error(sh, "AVRCP is not connected");
		err = -ENOEXEC;
	}

	return err;
}

static int cmd_get_unit_info(const struct shell *sh, int32_t argc, char *argv[])
{
	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct != NULL) {
		bt_avrcp_ct_get_unit_info(default_ct, get_next_tid());
	} else {
		shell_error(sh, "AVRCP is not connected");
	}

	return 0;
}

static int cmd_send_unit_info_rsp(const struct shell *sh, int32_t argc, char *argv[])
{
	struct bt_avrcp_unit_info_rsp rsp;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	rsp.unit_type = BT_AVRCP_SUBUNIT_TYPE_PANEL;
	rsp.company_id = BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG;

	if (default_tg != NULL) {
		err = bt_avrcp_tg_send_unit_info_rsp(default_tg, tg_tid, &rsp);
		if (!err) {
			shell_print(sh, "AVRCP send unit info response");
		} else {
			shell_error(sh, "Failed to send unit info response");
		}
	} else {
		shell_error(sh, "AVRCP is not connected");
	}

	return 0;
}

static int cmd_send_passthrough_rsp(const struct shell *sh, int32_t argc, char *argv[])
{
	struct bt_avrcp_passthrough_rsp *rsp;
	struct bt_avrcp_passthrough_opvu_data *opvu = NULL;
	bt_avrcp_opid_t opid = 0;
	bt_avrcp_button_state_t state;
	uint16_t vu_opid = 0;
	bool is_op_vu = true;
	struct net_buf *buf;
	char *endptr;
	unsigned long val;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	buf = bt_avrcp_create_pdu(NULL);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer for AVRCP passthrough response");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < sizeof(struct bt_avrcp_passthrough_rsp)) {
		shell_error(sh, "Not enough tailroom in buffer for passthrough rsp");
		goto failed;
	}
	rsp = net_buf_add(buf, sizeof(*rsp));

	if (!strcmp(argv[1], "op")) {
		is_op_vu = false;
	} else if (!strcmp(argv[1], "opvu")) {
		is_op_vu = true;
	} else {
		shell_error(sh, "Invalid response: %s", argv[1]);
		goto failed;
	}

	if (!strcmp(argv[2], "play")) {
		opid = BT_AVRCP_OPID_PLAY;
		vu_opid = (uint16_t)opid;
	} else if (!strcmp(argv[2], "pause")) {
		opid = BT_AVRCP_OPID_PAUSE;
		vu_opid = (uint16_t)opid;
	} else {
		/* Try to parse as hex value */
		val = strtoul(argv[2], &endptr, 16);
		if (*endptr != '\0' || val > 0xFFFFU) {
			shell_error(sh, "Invalid opid: %s", argv[2]);
			goto failed;
		}
		if (is_op_vu) {
			vu_opid = (uint16_t)val;
		} else {
			opid = (bt_avrcp_opid_t)val;
		}
	}

	if (!strcmp(argv[3], "pressed")) {
		state = BT_AVRCP_BUTTON_PRESSED;
	} else if (!strcmp(argv[3], "released")) {
		state = BT_AVRCP_BUTTON_RELEASED;
	} else {
		shell_error(sh, "Invalid state: %s", argv[3]);
		goto failed;
	}

	if (is_op_vu) {
		opid = BT_AVRCP_OPID_VENDOR_UNIQUE;
	}

	BT_AVRCP_PASSTHROUGH_SET_STATE_OPID(rsp, state, opid);
	if (is_op_vu) {
		if (net_buf_tailroom(buf) < sizeof(*opvu)) {
			shell_error(sh, "Not enough tailroom in buffer for opvu");
			goto failed;
		}
		opvu = net_buf_add(buf, sizeof(*opvu));
		sys_put_be24(BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, opvu->company_id);
		opvu->opid_vu = sys_cpu_to_be16(vu_opid);
		rsp->data_len = sizeof(*opvu);
	} else {
		rsp->data_len = 0;
	}

	err = bt_avrcp_tg_send_passthrough_rsp(default_tg, tg_tid, BT_AVRCP_RSP_ACCEPTED, buf);
	if (err < 0) {
		shell_error(sh, "Failed to send passthrough response: %d", err);
		goto failed;
	} else {
		shell_print(sh, "Passthrough opid=0x%02x, state=%s", opid, argv[2]);
		return 0;
	}

failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

static int cmd_send_subunit_info_rsp(const struct shell *sh, int32_t argc, char *argv[])
{
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg != NULL) {
		err = bt_avrcp_tg_send_subunit_info_rsp(default_tg, tg_tid);
		if (err == 0) {
			shell_print(sh, "AVRCP send subunit info response");
		} else {
			shell_error(sh, "Failed to send subunit info response");
		}
	} else {
		shell_error(sh, "AVRCP is not connected");
	}

	return 0;
}

static int cmd_send_get_caps_rsp(const struct shell *sh, int32_t argc, char *argv[])
{
	struct bt_avrcp_get_caps_rsp *rsp;
	uint8_t rsp_buffer[32U];
	uint8_t *cap_data;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	/* Initialize response structure */
	rsp = (struct bt_avrcp_get_caps_rsp *)rsp_buffer;
	rsp->cap_id = tg_cap_id;
	cap_data = rsp->cap;

	switch (tg_cap_id) {
	case BT_AVRCP_CAP_COMPANY_ID:
		/* Send Bluetooth SIG company ID as example */
		rsp->cap_cnt = 1;
		sys_put_be24(BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG, cap_data);
		shell_print(sh, "Sending company ID capability response: 0x%06x",
			    BT_AVRCP_COMPANY_ID_BLUETOOTH_SIG);
		break;

	case BT_AVRCP_CAP_EVENTS_SUPPORTED:
		/* Send supported events as example */
		rsp->cap_cnt = 5U;
		cap_data[0] = BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED;
		cap_data[1] = BT_AVRCP_EVT_TRACK_CHANGED;
		cap_data[2] = BT_AVRCP_EVT_TRACK_REACHED_END;
		cap_data[3] = BT_AVRCP_EVT_TRACK_REACHED_START;
		cap_data[4] = BT_AVRCP_EVT_VOLUME_CHANGED;
		shell_print(sh, "Sending events supported capability response with %u events",
			    rsp->cap_cnt);
		break;

	default:
		shell_error(sh, "Unknown capability ID: 0x%02x", tg_cap_id);
		return -EINVAL;
	}

	err = bt_avrcp_tg_send_get_caps_rsp(default_tg, tg_tid, rsp);
	if (err < 0) {
		shell_error(sh, "Failed to send get capabilities response: %d", err);
	} else {
		shell_print(sh, "Get capabilities response sent successfully");
	}

	return err;
}

static int cmd_get_subunit_info(const struct shell *sh, int32_t argc, char *argv[])
{
	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct != NULL) {
		bt_avrcp_ct_get_subunit_info(default_ct, get_next_tid());
	} else {
		shell_error(sh, "AVRCP is not connected");
	}

	return 0;
}

static int cmd_passthrough(const struct shell *sh, bt_avrcp_opid_t opid, const uint8_t *payload,
			   uint8_t len)
{
	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct != NULL) {
		bt_avrcp_ct_passthrough(default_ct, get_next_tid(), opid, BT_AVRCP_BUTTON_PRESSED,
					payload, len);
		bt_avrcp_ct_passthrough(default_ct, get_next_tid(), opid, BT_AVRCP_BUTTON_RELEASED,
					payload, len);
	} else {
		shell_error(sh, "AVRCP is not connected");
	}

	return 0;
}

static int cmd_play(const struct shell *sh, int32_t argc, char *argv[])
{
	return cmd_passthrough(sh, BT_AVRCP_OPID_PLAY, NULL, 0);
}

static int cmd_pause(const struct shell *sh, int32_t argc, char *argv[])
{
	return cmd_passthrough(sh, BT_AVRCP_OPID_PAUSE, NULL, 0);
}

static int cmd_get_cap(const struct shell *sh, int32_t argc, char *argv[])
{
	const char *cap_id;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP is not connected");
		return 0;
	}

	cap_id = argv[1];
	if (!strcmp(cap_id, "company")) {
		bt_avrcp_ct_get_cap(default_ct, get_next_tid(), BT_AVRCP_CAP_COMPANY_ID);
	} else if (!strcmp(cap_id, "events")) {
		bt_avrcp_ct_get_cap(default_ct, get_next_tid(), BT_AVRCP_CAP_EVENTS_SUPPORTED);
	}

	return 0;
}

static int cmd_get_element_attrs(const struct shell *sh, int32_t argc, char *argv[])
{
	struct bt_avrcp_get_element_attrs_cmd *cmd;
	struct net_buf *buf;
	char *endptr;
	unsigned long val;
	int err = 0;
	int i;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate vendor dependent command buffer");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < sizeof(*cmd) + (7 * sizeof(uint32_t))) {
		shell_error(sh, "Not enough tailroom in buffer for browsed player rsp");
		goto failed;
	}
	cmd = net_buf_add(buf, sizeof(*cmd));
	cmd->num_attrs = 0U;

	/* Parse optional identifier */
	if (argc > 1) {
		cmd->identifier = sys_cpu_to_be64(strtoull(argv[1], &endptr, 16));
		if (*endptr != '\0') {
			shell_error(sh, "Invalid identifier: %s", argv[1]);
			goto failed;
		}
	}

	/* Parse optional attribute IDs */
	if (argc > 2 && cmd->identifier != 0) {
		for (i = 2; i < argc && i < 9; i++) { /* Max 7 attributes + cmd + identifier */
			val = strtoul(argv[i], &endptr, 16);
			if (*endptr != '\0' || val > 0xFFFFFFFFUL) {
				shell_error(sh, "Invalid attribute ID: %s", argv[i]);
				goto failed;
			}
			net_buf_add_be32(buf, (uint32_t)val);
			cmd->num_attrs++;
		}
	}

	shell_print(sh, "Requesting element attributes: identifier=0x%016llx, num_attrs=%u",
		    cmd->identifier, cmd->num_attrs);

	err = bt_avrcp_ct_get_element_attrs(default_ct, get_next_tid(), buf);
	if (err < 0) {
		shell_error(sh, "Failed to send get element attrs command: %d", err);
		goto failed;
	} else {
		shell_print(sh, "AVRCP CT get element attrs command sent");
		return 0;
	}
failed:
	net_buf_unref(buf);
	return err;
}

static int cmd_send_get_element_attrs_rsp(const struct shell *sh, int32_t argc, char *argv[])
{

	struct bt_avrcp_get_element_attrs_rsp *rsp;
	struct bt_avrcp_media_attr *attr;
	uint16_t total_size = 0;
	bool use_large_attrs = false;
	struct net_buf *buf;
	char *endptr;
	int err = 0;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	buf = bt_avrcp_create_pdu(&avrcp_big_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate vendor dependent command buffer");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < sizeof(*rsp)) {
		shell_error(sh, "Not enough tailroom in buffer for browsed player rsp");
		goto failed;
	}

	rsp = net_buf_add(buf, sizeof(*rsp));
	if (argc > 1) {
		use_large_attrs = strtoull(argv[1], &endptr, 16);
		if (*endptr != '\0') {
			shell_error(sh, "Invalid identifier: %s", argv[1]);
			goto failed;
		}
	}

	/* Determine which attribute set to use */
	if (use_large_attrs) {
		rsp->num_attrs = ARRAY_SIZE(large_media_attrs);
		for (int i = 0; i < rsp->num_attrs; i++) {
			total_size += sizeof(*attr) + large_media_attrs[i].attr_len;
		}

		if (net_buf_tailroom(buf) < total_size) {
			shell_error(sh, "Not enough tailroom in buffer for large attrs");
			goto failed;
		}

		for (int i = 0; i < rsp->num_attrs; i++) {
			attr = net_buf_add(buf, sizeof(struct bt_avrcp_media_attr));
			attr->attr_id = sys_cpu_to_be32(large_media_attrs[i].attr_id);
			attr->charset_id = sys_cpu_to_be16(large_media_attrs[i].charset_id);
			attr->attr_len = sys_cpu_to_be16(large_media_attrs[i].attr_len);
			net_buf_add(buf, large_media_attrs[i].attr_len);
			memset(attr->attr_val, 0x0, large_media_attrs[i].attr_len);
			memcpy(attr->attr_val, large_media_attrs[i].attr_val,
			       strlen(large_media_attrs[i].attr_val));
		}

		shell_print(sh, "Sending large Attributes response (%u attrs) for fragment test",
			    rsp->num_attrs);
	} else {
		rsp->num_attrs = ARRAY_SIZE(test_media_attrs);
		for (int i = 0; i < rsp->num_attrs; i++) {
			total_size += sizeof(*attr) + test_media_attrs[i].attr_len;
		}

		if (net_buf_tailroom(buf) < total_size) {
			shell_error(sh, "Not enough tailroom in buffer for large attrs");
			goto failed;
		}

		for (int i = 0; i < rsp->num_attrs; i++) {
			attr = net_buf_add(buf, sizeof(*attr));
			attr->attr_id = sys_cpu_to_be32(test_media_attrs[i].attr_id);
			attr->charset_id = sys_cpu_to_be16(test_media_attrs[i].charset_id);
			attr->attr_len = sys_cpu_to_be16(test_media_attrs[i].attr_len);
			net_buf_add_mem(buf, test_media_attrs[i].attr_val,
					test_media_attrs[i].attr_len);
		}
		shell_print(sh, "Sending standard GetElementAttributes response (%u attrs)",
			    rsp->num_attrs);
	}

	err = bt_avrcp_tg_send_get_element_attrs_rsp(default_tg, tg_tid, BT_AVRCP_RSP_STABLE, buf);
	if (err < 0) {
		shell_error(sh, "Failed to send GetElementAttributes response: %d", err);
		goto failed;
	} else {
		shell_print(sh, "GetElementAttributes response sent successfully");
		return 0;
	}

failed:
	net_buf_unref(buf);
	return err;
}

static int cmd_ct_register_notification(const struct shell *sh, int argc, char *argv[])
{
	uint8_t event_id;
	uint32_t playback_interval = 0U;
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	event_id = (uint8_t)strtoul(argv[1], NULL, 0);
	if (argc > 2) {
		playback_interval = (uint32_t)strtoul(argv[2], NULL, 0);
	}

	err = bt_avrcp_ct_register_notification(default_ct, get_next_tid(), event_id,
						playback_interval, avrcp_notification_rsp);
	if (err < 0) {
		shell_error(sh, "Failed to send register_notification: %d", err);
	} else {
		shell_print(sh, "Sent register notification event_id=0x%02x", event_id);
	}
	return err;
}

static int cmd_tg_send_notification_rsp(const struct shell *sh, int argc, char *argv[])
{
	struct bt_avrcp_event_data data;
	struct bt_avrcp_app_setting_attr_val attr_vals[1];
	uint8_t event_id = (uint8_t)strtoul(argv[1], NULL, 0);
	bt_avrcp_rsp_t type;
	uint64_t identifier;
	char *endptr;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	memset(&data, 0, sizeof(data));

	if (strcmp(argv[2], "changed") == 0) {
		type = BT_AVRCP_RSP_CHANGED;
	} else if (strcmp(argv[2], "interim") == 0) {
		type = BT_AVRCP_RSP_INTERIM;
	} else {
		shell_error(sh, "Invalid type: %s (expected: changed|interim)", argv[2]);
		return -EINVAL;
	}

	if (type == BT_AVRCP_RSP_INTERIM) {
		if (event_id == BT_AVRCP_EVT_TRACK_CHANGED) {
			/* Interim response for track changed must have identifier set */
			identifier = 111111;
			sys_put_be64(identifier, data.identifier);
		}
		goto done;
	}
	switch (event_id) {
	case BT_AVRCP_EVT_PLAYBACK_STATUS_CHANGED:
		if (argc < 4) {
			data.play_status = BT_AVRCP_PLAYBACK_STATUS_PLAYING;
		} else {
			data.play_status = (uint8_t)strtoul(argv[3], NULL, 0);
		}
		break;
	case BT_AVRCP_EVT_TRACK_CHANGED:
		if (argc < 11) {
			identifier = 1;
			sys_put_be64(identifier, data.identifier);
		} else {
			identifier = strtoull(argv[3], &endptr, 16);
			if (*endptr != '\0') {
				shell_error(sh, "Invalid identifier: %s", argv[3]);
			}
			sys_put_be64(identifier, data.identifier);
		}
		break;
	case BT_AVRCP_EVT_PLAYBACK_POS_CHANGED:
		if (argc < 4) {
			data.playback_pos = 1000;
		} else {
			data.playback_pos = (uint32_t)strtoul(argv[3], NULL, 0);
		}
		break;
	case BT_AVRCP_EVT_BATT_STATUS_CHANGED:
		if (argc < 4) {
			data.battery_status = BT_AVRCP_BATTERY_STATUS_NORMAL;
		} else {
			data.battery_status = (uint8_t)strtoul(argv[3], NULL, 0);
		}
		break;
	case BT_AVRCP_EVT_SYSTEM_STATUS_CHANGED:
		if (argc < 4) {
			data.system_status = BT_AVRCP_SYSTEM_STATUS_POWER_ON;
		} else {
			data.system_status = (uint8_t)strtoul(argv[3], NULL, 0);
		}
		break;
	case BT_AVRCP_EVT_PLAYER_APP_SETTING_CHANGED:
		data.setting_changed.num_of_attr           = 1;
		data.setting_changed.attr_vals = &attr_vals[0];
		data.setting_changed.attr_vals[0].attr_id  = 1;
		data.setting_changed.attr_vals[0].value_id = 1;
		break;
	case BT_AVRCP_EVT_ADDRESSED_PLAYER_CHANGED:
		if (argc < 5) {
			data.addressed_player_changed.player_id = 0x0001; /* Default player ID */
			data.addressed_player_changed.uid_counter = 0x0001; /* Default UID counter*/
		} else {
			data.addressed_player_changed.player_id = strtoul(argv[3], NULL, 0);
			data.addressed_player_changed.uid_counter = strtoul(argv[4], NULL, 0);
		}
		break;
	case BT_AVRCP_EVT_UIDS_CHANGED:
		if (argc < 4) {
			data.uid_counter = 1;
		} else {
			data.uid_counter = (uint16_t)strtoul(argv[3], NULL, 0);
		}
		break;
	case BT_AVRCP_EVT_VOLUME_CHANGED:
		if (argc < 4) {
			data.absolute_volume = 10;
		} else {
			data.absolute_volume = (uint8_t)strtoul(argv[3], NULL, 0);
		}
		break;
	default:
		shell_error(sh, "Unknown event_id: 0x%02x", event_id);
		return -EINVAL;
	}

done:
	err = bt_avrcp_tg_send_notification_rsp(default_tg, tg_tid, type, event_id, &data);
	if (err < 0) {
		shell_error(sh, "Failed to send notification rsp: %d", err);
	} else {
		shell_print(sh, "Sent notification rsp event_id=0x%02x type=%s",
			    event_id, (type == BT_AVRCP_RSP_CHANGED) ? "changed" : "interim");
	}
	return err;
}

static int cmd_ct_set_absolute_volume(const struct shell *sh, int argc, char *argv[])
{
	uint8_t absolute_volume;
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	absolute_volume = (uint8_t)strtoul(argv[1], NULL, 0);

	err = bt_avrcp_ct_set_absolute_volume(default_ct, get_next_tid(), absolute_volume);
	if (err < 0) {
		shell_error(sh, "Failed to set absolute volume: %d", err);
	} else {
		shell_print(sh, "set absolute volume"
			    " absolute_volume=0x%02x", absolute_volume);
	}
	return err;
}

static int cmd_ct_list_app_attrs(const struct shell *sh, int argc, char *argv[])
{
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	err = bt_avrcp_ct_list_player_app_setting_attrs(default_ct, get_next_tid());
	if (err < 0) {
		shell_error(sh, "list player app setting attrs failed: %d", err);
	} else {
		shell_print(sh, "Sent list player app setting attrs");
	}

	return err;
}

static int cmd_ct_list_app_vals(const struct shell *sh, int argc, char *argv[])
{
	uint8_t attr;
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	attr = (uint8_t)strtoul(argv[1], NULL, 0);

	err = bt_avrcp_ct_list_player_app_setting_vals(default_ct, get_next_tid(), attr);
	if (err < 0) {
		shell_error(sh, "Failed to send list player app setting vals: %d", err);
		return -ENOEXEC;
	}

	shell_print(sh, "Sent list player app setting vals attr=0x%02x", attr);
	return 0;
}

static int cmd_ct_get_app_curr(const struct shell *sh, int argc, char *argv[])
{
	struct bt_avrcp_get_curr_player_app_setting_val_cmd *cmd;
	struct net_buf *buf;
	size_t expected_len;
	int err, i;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	expected_len = 1 + (size_t)((argc > 1) ? (argc - 1) : 0);

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate vendor dependent command buffer");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < expected_len) {
		shell_error(sh, "Not enough tailroom in buffer");
		goto failed;
	}
	cmd = net_buf_add(buf, sizeof(*cmd));

	cmd->num_attrs = (argc > 1) ? (uint8_t)(argc - 1) : 0U;
	for (i = 1; i < argc; i++) {
		net_buf_add_u8(buf, (uint8_t)strtoul(argv[i], NULL, 0));
	}

	err = bt_avrcp_ct_get_curr_player_app_setting_val(default_ct, get_next_tid(), buf);
	if (err < 0) {
		shell_error(sh, "Failed to send get_curr_player_app_setting_val: %d", err);
		goto failed;
	}

	shell_print(sh, "Sent get_curr_player_app_setting_val num=%u", cmd->num_attrs);
	return 0;

failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

static int cmd_ct_set_app_val(const struct shell *sh, int argc, char *argv[])
{
	struct bt_avrcp_set_player_app_setting_val_cmd *cmd;
	struct net_buf *buf;
	size_t expected_len;
	uint8_t pairs;
	int err, i;

	if ((argc < 3) || (((argc - 1) % 2) != 0)) {
		shell_error(sh, "usage: set_app_val <attr1> <val1> [<attr2> <val2> ...]");
		return -ENOEXEC;
	}

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOEXEC;
	}

	pairs = (uint8_t)((argc - 1) / 2);
	/* Payload: NumPairs(1) + (AttrID,ValueID)*pairs */
	expected_len = 1 + (size_t)pairs * 2U;

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate vendor dependent command buffer");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < expected_len) {
		shell_error(sh, "Not enough tailroom in buffer");
		err = -ENOMEM;
		goto failed;
	}
	cmd = net_buf_add(buf, expected_len);
	cmd->num_attrs = pairs;

	for (i = 1; i < argc; i += 2) {
		cmd->attr_vals[(i-1)/2].attr_id = (uint8_t)strtoul(argv[i],  NULL, 0);
		cmd->attr_vals[(i-1)/2].value_id = (uint8_t)strtoul(argv[i+1], NULL, 0);
	}

	err = bt_avrcp_ct_set_player_app_setting_val(default_ct, get_next_tid(), buf);
	if (err < 0) {
		shell_error(sh, "Failed to send set_player_app_setting_val: %d", err);
		goto failed;
	}

	shell_print(sh, "Sent SetPlayerApplicationSettingValue num_attrs=%u", cmd->num_attrs);
	return 0;

failed:
	net_buf_unref(buf);
	return err;
}

static int cmd_ct_get_app_attr_text(const struct shell *sh, int argc, char *argv[])
{
	struct bt_avrcp_get_player_app_setting_attr_text_cmd *cmd;
	struct net_buf *buf;
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (!default_ct) {
		shell_error(sh, "AVRCP CT is not connected");
		return -ENOTCONN;
	}

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "No buffer");
		return -ENOMEM;
	}
	cmd = net_buf_add(buf, sizeof(*cmd));
	cmd->num_attrs = (uint8_t)(argc - 1);

	for (size_t i = 1; i < argc; i++) {
		net_buf_add_u8(buf, (uint8_t)strtoul(argv[i], NULL, 0));
	}

	err = bt_avrcp_ct_get_player_app_setting_attr_text(default_ct, get_next_tid(), buf);
	if (err < 0) {
		shell_error(sh, "get_player_app_setting_attr_text failed: %d", err);
		net_buf_unref(buf);
		return err;
	}

	shell_print(sh, "Sent get_player_app_setting_attr_text num_attrs=%u", cmd->num_attrs);
	return 0;
}

static int cmd_tg_send_absolute_volume_rsp(const struct shell *sh, int32_t argc, char *argv[])
{
	uint8_t absolute_volume;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	absolute_volume = (uint8_t)strtoul(argv[1], NULL, 0);

	err = bt_avrcp_tg_send_absolute_volume_rsp(default_tg, tg_tid, BT_AVRCP_RSP_ACCEPTED,
						   absolute_volume);
	if (err < 0) {
		shell_error(sh, "Failed to send set absolute volume response: %d", err);
	} else {
		shell_print(sh, "Set absolute volume response sent successfully");
	}

	return err;
}

static int cmd_tg_send_list_player_app_setting_attrs_rsp(const struct shell *sh, int argc,
							 char *argv[])
{
	struct net_buf *buf;
	uint8_t num, id;
	size_t expected_len;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	num = (argc >= 2) ? (uint8_t)strtoul(argv[1], NULL, 0) : 2;
	expected_len = 1 + (size_t)num; /* Num + AttrIDs */

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer for AVRCP response");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < expected_len) {
		shell_error(sh, "Not enough tailroom in buffer");
		goto failed;
	}

	net_buf_add_u8(buf, num);
	for (uint8_t i = 0U; i < num; i++) {
		id = (argc >= (2 + i + 1)) ? (uint8_t)strtoul(argv[2 + i], NULL, 0) : (i + 1);
		net_buf_add_u8(buf, id);
	}

	err = bt_avrcp_tg_send_list_player_app_setting_attrs_rsp(default_tg, tg_tid,
								 BT_AVRCP_RSP_STABLE, buf);
	if (err < 0) {
		shell_error(sh, "Failed to send ListPlayerAppSettingAttributes rsp: %d", err);
		goto failed;
	}

	shell_print(sh, "ListPlayerApplicationSettingAttributes rsp sent (num=%u)", num);
	return 0;

failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

static int cmd_tg_send_list_player_app_setting_vals_rsp(const struct shell *sh, int argc,
							char *argv[])
{
	struct net_buf *buf;
	uint8_t num, val;
	size_t expected_len;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	num = (argc >= 2) ? (uint8_t)strtoul(argv[1], NULL, 0) : 2;
	expected_len = 1 + (size_t)num; /* Num + ValueIDs */

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer for AVRCP response");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < expected_len) {
		shell_error(sh, "Not enough tailroom in buffer");
		goto failed;
	}

	net_buf_add_u8(buf, num);
	for (uint8_t i = 0U; i < num; i++) {
		val = (argc >= (2 + i + 1)) ? (uint8_t)strtoul(argv[2 + i], NULL, 0) : (i + 1);
		net_buf_add_u8(buf, val);
	}

	err = bt_avrcp_tg_send_list_player_app_setting_vals_rsp(default_tg, tg_tid,
								BT_AVRCP_RSP_STABLE, buf);
	if (err < 0) {
		shell_error(sh, "Failed to send list player app setting vals rsp: %d", err);
		goto failed;
	}

	shell_print(sh, "List player app setting vals rsp sent (num=%u)", num);
	return 0;

failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

static int cmd_tg_send_get_curr_player_app_setting_val_rsp(const struct shell *sh, int argc,
							   char *argv[])
{
	struct net_buf *buf;
	struct bt_avrcp_get_curr_player_app_setting_val_rsp *rsp;
	size_t expected_len;
	uint8_t num_pairs;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	/* Response payload: Num + (AttrID,ValueID)[n] */
	num_pairs = (argc >= 2) ? (uint8_t)strtoul(argv[1], NULL, 0) : 1;
	expected_len = sizeof(uint8_t) + (size_t)num_pairs *
		       sizeof(struct bt_avrcp_app_setting_attr_val);

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer for AVRCP response");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < expected_len) {
		shell_error(sh, "Not enough tailroom in buffer");
		goto failed;
	}
	rsp = net_buf_add(buf, expected_len);
	rsp->num_attrs = num_pairs;

	/* args: <num> [attr1 val1] [attr2 val2] ... */
	for (uint8_t i = 0U; i < rsp->num_attrs; i++) {
		int ai = 2 + (i * 2);   /* argv index for attr */

		rsp->attr_vals[i].attr_id = (ai < argc) ? (uint8_t)strtoul(argv[ai], NULL, 0) :
					    (uint8_t)(i + 1);
		rsp->attr_vals[i].value_id = (ai + 1 < argc) ?
					     (uint8_t)strtoul(argv[ai + 1], NULL, 0) : 1;
	}

	err = bt_avrcp_tg_send_get_curr_player_app_setting_val_rsp(default_tg, tg_tid,
								   BT_AVRCP_RSP_STABLE, buf);
	if (err < 0) {
		shell_error(sh, "Failed to send get curr player app setting val rsp: %d", err);
		goto failed;
	}

	shell_print(sh, "Send get curr player app setting val rsp sent (num=%u)", rsp->num_attrs);
	return 0;

failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

static int  cmd_tg_send_set_player_app_setting_val_rsp(const struct shell *sh, int argc,
						       char *argv[])
{
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	err = bt_avrcp_tg_send_set_player_app_setting_val_rsp(default_tg, tg_tid,
							      BT_AVRCP_RSP_STABLE);
	if (err < 0) {
		shell_error(sh, "Failed to send set set_player_app_setting_val rsp: %d", err);
		return -ENOEXEC;
	}

	shell_print(sh, "set_player_app_setting_val rsp sent ");
	return 0;
}

static int cmd_tg_send_get_player_app_setting_attr_text_rsp(const struct shell *sh, int argc,
							    char *argv[])
{
	struct bt_avrcp_get_player_app_setting_attr_text_rsp *rsp;
	struct net_buf *buf;
	char *text_str = "AttrText";
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer for AVRCP response");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < sizeof(*rsp) + sizeof(struct bt_avrcp_app_setting_attr_text)) {
		shell_error(sh, "Not enough tailroom in buffer");
		goto failed;
	}
	rsp = net_buf_add(buf, sizeof(*rsp) + sizeof(struct bt_avrcp_app_setting_attr_text));

	rsp->num_attrs = 1;
	rsp->attr_text[0].attr_id = 1;
	rsp->attr_text[0].charset_id = sys_cpu_to_be16(BT_AVRCP_CHARSET_UTF8);
	rsp->attr_text[0].text_len = strlen(text_str);
	net_buf_add_mem(buf, text_str, strlen(text_str));

	err = bt_avrcp_tg_send_get_player_app_setting_attr_text_rsp(default_tg, tg_tid,
								    BT_AVRCP_RSP_STABLE, buf);
	if (err < 0) {
		shell_error(sh, "Failed to send get player app setting attr text rsp: %d", err);
		return -ENOEXEC;
	}

	shell_print(sh, "Get player app setting attr text rsp sent");
	return 0;

failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

static int cmd_set_browsed_player(const struct shell *sh, int32_t argc, char *argv[])
{
	uint16_t player_id;
	int err;

	if (!avrcp_ct_registered && register_ct_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_ct == NULL) {
		shell_error(sh, "AVRCP is not connected");
		return -ENOEXEC;
	}

	player_id = (uint16_t)strtoul(argv[1], NULL, 0);

	err = bt_avrcp_ct_set_browsed_player(default_ct, get_next_tid(), player_id);
	if (err < 0) {
		shell_error(sh, "fail to set browsed player");
	} else {
		shell_print(sh, "AVRCP send set browsed player req");
	}

	return 0;
}

static int cmd_send_set_browsed_player_rsp(const struct shell *sh, int32_t argc, char *argv[])
{
	struct bt_avrcp_set_browsed_player_rsp *rsp;
	struct bt_avrcp_folder_name *folder_name;
	char *folder_name_str = "Music";
	uint8_t folder_name_hex[FOLDER_NAME_HEX_BUF_LEN];
	uint16_t folder_name_len = 0;
	struct net_buf *buf;
	uint16_t param_len;
	int err;

	if (!avrcp_tg_registered && register_tg_cb(sh) != 0) {
		return -ENOEXEC;
	}

	if (default_tg == NULL) {
		shell_error(sh, "AVRCP TG is not connected");
		return -ENOEXEC;
	}

	buf = bt_avrcp_create_pdu(&avrcp_tx_pool);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer for AVRCP browsing response");
		return -ENOMEM;
	}

	if (net_buf_tailroom(buf) < sizeof(struct bt_avrcp_set_browsed_player_rsp)) {
		shell_error(sh, "Not enough tailroom in buffer for browsed player rsp");
		goto failed;
	}

	rsp = net_buf_add(buf, sizeof(*rsp));
	/* Set default rsp */
	rsp->status = BT_AVRCP_STATUS_OPERATION_COMPLETED;
	rsp->uid_counter = sys_cpu_to_be16(0x0001U);
	rsp->num_items = sys_cpu_to_be32(100U);
	rsp->charset_id = sys_cpu_to_be16(BT_AVRCP_CHARSET_UTF8);
	rsp->folder_depth = 1;

	/* Parse command line arguments or use default values */
	if (argc >= 2) {
		rsp->status = (uint8_t)strtoul(argv[1], NULL, 0);
	}

	if (argc >= 3) {
		rsp->uid_counter = sys_cpu_to_be16((uint16_t)strtoul(argv[2], NULL, 0));
	}

	if (argc >= 4) {
		rsp->num_items = sys_cpu_to_be32((uint32_t)strtoul(argv[3], NULL, 0));
	}

	if (argc >= 5) {
		rsp->charset_id = sys_cpu_to_be16((uint16_t)strtoul(argv[4], NULL, 0));
	}

	if (rsp->charset_id == sys_cpu_to_be16(BT_AVRCP_CHARSET_UTF8)) {
		if (argc >= 6) {
			folder_name_str = argv[5];
		}
		folder_name_len = strlen(folder_name_str);
	} else {
		if (argc >= 6) {
			folder_name_len = hex2bin(argv[5], strlen(argv[5]), folder_name_hex,
						  sizeof(folder_name_hex));
			if (folder_name_len == 0) {
				shell_error(sh, "Failed to get folder_name from  %s", argv[5]);
			}
		} else {
			shell_error(sh, "Please input hex string for folder_name");
			goto failed;
		}
	}

	param_len = folder_name_len + sizeof(struct bt_avrcp_folder_name);
	if (net_buf_tailroom(buf) < param_len) {
		shell_error(sh, "Not enough tailroom in buffer for param");
		goto failed;
	}

	folder_name = net_buf_add(buf, sizeof(*folder_name));
	folder_name->folder_name_len = sys_cpu_to_be16(folder_name_len);
	if (rsp->charset_id == sys_cpu_to_be16(BT_AVRCP_CHARSET_UTF8)) {
		net_buf_add_mem(buf, folder_name_str, folder_name_len);
	} else {
		net_buf_add_mem(buf, folder_name_hex, folder_name_len);
	}

	err = bt_avrcp_tg_send_set_browsed_player_rsp(default_tg, tg_tid, buf);
	if (err == 0) {
		shell_print(sh, "Send set browsed player response, status = 0x%02x", rsp->status);
	} else {
		shell_error(sh, "Failed to send set browsed player response, err = %d", err);
		goto failed;
	}

	return 0;
failed:
	net_buf_unref(buf);
	return -ENOEXEC;
}

#define HELP_NONE "[none]"
#define HELP_PASSTHROUGH_RSP                                                     \
	"send_passthrough_rsp <op/opvu> <opid> <state>\n"                        \
	"op/opvu: passthrough command (normal/passthrough VENDOR UNIQUE)\n"      \
	"opid: operation identifier (e.g., play/pause or hex value)\n"           \
	"state: [pressed|released]"

#define HELP_BROWSED_PLAYER_RSP                                                      \
	"Send SetBrowsedPlayer response\n"					     \
	"Usage: send_browsed_player_rsp [status] [uid_counter] [num_items] "         \
	"[charset_id] [folder_name]"

SHELL_STATIC_SUBCMD_SET_CREATE(
	ct_cmds,
	SHELL_CMD_ARG(register_cb, NULL, "register avrcp ct callbacks", cmd_register_ct_cb, 1, 0),
	SHELL_CMD_ARG(get_unit, NULL, "get unit info", cmd_get_unit_info, 1, 0),
	SHELL_CMD_ARG(get_subunit, NULL, "get subunit info", cmd_get_subunit_info, 1, 0),
	SHELL_CMD_ARG(get_cap, NULL, "get capabilities <cap_id: company or events>", cmd_get_cap, 2,
		      0),
	SHELL_CMD_ARG(play, NULL, "request a play at the remote player", cmd_play, 1, 0),
	SHELL_CMD_ARG(pause, NULL, "request a pause at the remote player", cmd_pause, 1, 0),
	SHELL_CMD_ARG(get_element_attrs, NULL, "get element attrs [identifier] [attr1] [attr2] ...",
		      cmd_get_element_attrs, 1, 9),
	SHELL_CMD_ARG(register_notification, NULL, "register notify <event_id> [playback_interval]",
		      cmd_ct_register_notification, 2, 1),
	SHELL_CMD_ARG(set_absolute_volume, NULL, "set absolute volume <volume>",
		      cmd_ct_set_absolute_volume, 2, 0),
	SHELL_CMD_ARG(set_browsed_player, NULL, "set browsed player <player_id>",
		      cmd_set_browsed_player, 2, 0),
	SHELL_CMD_ARG(list_app_attrs, NULL, "List App attrs", cmd_ct_list_app_attrs, 1, 0),
	SHELL_CMD_ARG(list_app_vals,  NULL, "List App vals <attr_id>", cmd_ct_list_app_vals, 2, 0),
	SHELL_CMD_ARG(get_app_curr,   NULL, "Get App vals [attr1] [attr2] ...",
		      cmd_ct_get_app_curr, 1, 8),
	SHELL_CMD_ARG(set_app_val, NULL, "App Setting Value <attr1> <val1> [<attr2> <val2>]  ...",
		      cmd_ct_set_app_val, 3, 14),
	SHELL_CMD_ARG(get_app_attr_text, NULL, "Get PApp Setting attrs text <attr1> [attr2] ...",
		      cmd_ct_get_app_attr_text, 2, 7),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	tg_cmds,
	SHELL_CMD_ARG(register_cb, NULL, "register avrcp tg callbacks", cmd_register_tg_cb, 1, 0),
	SHELL_CMD_ARG(send_unit_rsp, NULL, "send unit info response", cmd_send_unit_info_rsp, 1, 0),
	SHELL_CMD_ARG(send_subunit_rsp, NULL, HELP_NONE, cmd_send_subunit_info_rsp, 1, 0),
	SHELL_CMD_ARG(send_get_caps_rsp, NULL, "send get capabilities response",
		      cmd_send_get_caps_rsp, 1, 0),
	SHELL_CMD_ARG(send_get_element_attrs_rsp, NULL, "send get element attrs response<large: 1>",
		      cmd_send_get_element_attrs_rsp, 2, 0),
	SHELL_CMD_ARG(send_notification_rsp, NULL,
		     "send notification rsp <event_id> <changed|interim> [value...]",
		      cmd_tg_send_notification_rsp, 3, 10),
	SHELL_CMD_ARG(send_absolute_volume_rsp, NULL, "send absolute volume rsp <volume>",
		      cmd_tg_send_absolute_volume_rsp, 2, 0),
	SHELL_CMD_ARG(send_browsed_player_rsp, NULL, HELP_BROWSED_PLAYER_RSP,
		      cmd_send_set_browsed_player_rsp, 1, 5),
	SHELL_CMD_ARG(send_passthrough_rsp, NULL, HELP_PASSTHROUGH_RSP, cmd_send_passthrough_rsp,
		      4, 0),
	SHELL_CMD_ARG(send_list_player_app_setting_attrs_rsp, NULL,
		      "send attrs rsp <num> [attr_id...]",
		       cmd_tg_send_list_player_app_setting_attrs_rsp, 2, 8),
	SHELL_CMD_ARG(send_list_player_app_setting_vals_rsp, NULL,
		      "send vals rsp <num> [val_id...]",
		      cmd_tg_send_list_player_app_setting_vals_rsp, 2, 16),
	SHELL_CMD_ARG(send_get_curr_player_app_setting_val_rsp, NULL,
		      "send current vals rsp <num_pairs> [attr val]...",
		      cmd_tg_send_get_curr_player_app_setting_val_rsp, 2, 16),
	SHELL_CMD_ARG(send_set_player_app_setting_val_rsp, NULL, HELP_NONE,
		      cmd_tg_send_set_player_app_setting_val_rsp, 1, 0),
	SHELL_CMD_ARG(send_get_player_app_setting_attr_text_rsp, NULL, HELP_NONE,
		      cmd_tg_send_get_player_app_setting_attr_text_rsp, 1, 0),
	SHELL_SUBCMD_SET_END);

static int cmd_avrcp(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		/* sh returns 1 when help is printed */
		return 1;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -ENOEXEC;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	avrcp_cmds,
	SHELL_CMD_ARG(connect, NULL, "connect AVRCP", cmd_connect, 1, 0),
	SHELL_CMD_ARG(disconnect, NULL, "disconnect AVRCP", cmd_disconnect, 1, 0),
	SHELL_CMD_ARG(browsing_connect, NULL, "connect browsing AVRCP", cmd_browsing_connect, 1, 0),
	SHELL_CMD_ARG(browsing_disconnect, NULL, "disconnect browsing AVRCP",
		      cmd_browsing_disconnect, 1, 0),
	SHELL_CMD(ct, &ct_cmds, "AVRCP CT shell commands", cmd_avrcp),
	SHELL_CMD(tg, &tg_cmds, "AVRCP TG shell commands", cmd_avrcp),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_ARG_REGISTER(avrcp, &avrcp_cmds, "Bluetooth AVRCP sh commands", cmd_avrcp, 1, 1);
