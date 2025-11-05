/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/firmware/scmi/system.h>
#include <string.h>
#include <zephyr/kernel.h>

DT_SCMI_PROTOCOL_DEFINE_NODEV(DT_INST(0, arm_scmi_system), NULL);

/*
 * TODO: Extract common SCMI protocol query functions
 * (version/attributes/message_attributes)
 * into generic APIs to reduce code duplication across protocols.
 */
struct scmi_system_version_reply {
	int32_t status;
	uint32_t version;
};

struct scmi_system_attributes_reply {
	int32_t status;
	uint32_t attributes;
};

struct scmi_system_message_attributes_reply {
	int32_t status;
	uint32_t attributes;
};

int scmi_system_protocol_version(uint32_t *version)
{
	struct scmi_protocol *proto = &SCMI_PROTOCOL_NAME(SCMI_PROTOCOL_SYSTEM);
	struct scmi_system_version_reply reply_buffer;
	struct scmi_message msg, reply;
	int ret;
	bool use_polling;

	if (!proto || !version) {
		return -EINVAL;
	}

	if (proto->id != SCMI_PROTOCOL_SYSTEM) {
		return -EINVAL;
	}

	msg.hdr = SCMI_MESSAGE_HDR_MAKE(SCMI_SYSTEM_MSG_PROTOCOL_VERSION, SCMI_COMMAND,
					proto->id, 0x0);
	msg.len = 0;
	msg.content = NULL;

	reply.hdr = msg.hdr;
	reply.len = sizeof(reply_buffer);
	reply.content = &reply_buffer;

	use_polling = k_is_pre_kernel();

	ret = scmi_send_message(proto, &msg, &reply, use_polling);
	if (ret < 0) {
		return ret;
	}

	*version = reply_buffer.version;

	return scmi_status_to_errno(reply_buffer.status);
}

int scmi_system_protocol_attributes(uint32_t *attributes)
{
	struct scmi_protocol *proto = &SCMI_PROTOCOL_NAME(SCMI_PROTOCOL_SYSTEM);
	struct scmi_system_attributes_reply reply_buffer;
	struct scmi_message msg, reply;
	int ret;
	bool use_polling;

	if (!proto || !attributes) {
		return -EINVAL;
	}

	if (proto->id != SCMI_PROTOCOL_SYSTEM) {
		return -EINVAL;
	}

	msg.hdr = SCMI_MESSAGE_HDR_MAKE(SCMI_SYSTEM_MSG_PROTOCOL_ATTRIBUTES, SCMI_COMMAND,
					proto->id, 0x0);
	msg.len = 0;
	msg.content = NULL;

	reply.hdr = msg.hdr;
	reply.len = sizeof(reply_buffer);
	reply.content = &reply_buffer;

	use_polling = k_is_pre_kernel();

	ret = scmi_send_message(proto, &msg, &reply, use_polling);
	if (ret < 0) {
		return ret;
	}

	*attributes = reply_buffer.attributes;

	return scmi_status_to_errno(reply_buffer.status);
}

int scmi_system_protocol_message_attributes(uint32_t message_id, uint32_t *attributes)
{
	struct scmi_protocol *proto = &SCMI_PROTOCOL_NAME(SCMI_PROTOCOL_SYSTEM);
	struct scmi_system_message_attributes_reply reply_buffer;
	struct scmi_message msg, reply;
	int ret;
	bool use_polling;

	if (!proto || !attributes) {
		return -EINVAL;
	}

	if (proto->id != SCMI_PROTOCOL_SYSTEM) {
		return -EINVAL;
	}

	msg.hdr = SCMI_MESSAGE_HDR_MAKE(SCMI_SYSTEM_MSG_MESSAGE_ATTRIBUTES, SCMI_COMMAND,
					proto->id, 0x0);
	msg.len = sizeof(message_id);
	msg.content = &message_id;

	reply.hdr = msg.hdr;
	reply.len = sizeof(reply_buffer);
	reply.content = &reply_buffer;

	use_polling = k_is_pre_kernel();

	ret = scmi_send_message(proto, &msg, &reply, use_polling);
	if (ret < 0) {
		return ret;
	}

	*attributes = reply_buffer.attributes;

	return scmi_status_to_errno(reply_buffer.status);
}

int scmi_system_power_state_set(struct scmi_system_power_state_config *cfg)
{
	struct scmi_protocol *proto = &SCMI_PROTOCOL_NAME(SCMI_PROTOCOL_SYSTEM);
	struct scmi_message msg, reply;
	int32_t status;
	int ret;
	bool use_polling;

	/* sanity checks */
	if (!proto || !cfg) {
		return -EINVAL;
	}

	if (proto->id != SCMI_PROTOCOL_SYSTEM) {
		return -EINVAL;
	}

	msg.hdr = SCMI_MESSAGE_HDR_MAKE(SCMI_SYSTEM_MSG_POWER_STATE_SET, SCMI_COMMAND,
					proto->id, 0x0);
	msg.len = sizeof(*cfg);
	msg.content = cfg;

	reply.hdr = msg.hdr;
	reply.len = sizeof(status);
	reply.content = &status;

	use_polling = k_is_pre_kernel();

	ret = scmi_send_message(proto, &msg, &reply, use_polling);
	if (ret < 0) {
		return ret;
	}

	return scmi_status_to_errno(status);
}
