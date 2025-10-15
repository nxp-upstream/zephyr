/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usbh_msc_scsi.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usbh_msc_scsi, CONFIG_USBH_MSC_CLASS_LOG_LEVEL);

/* SCSI Command Operation Codes */
#define SCSI_TEST_UNIT_READY      0x00
#define SCSI_REQUEST_SENSE        0x03
#define SCSI_INQUIRY              0x12
#define SCSI_MODE_SENSE_6         0x1A
#define SCSI_READ_CAPACITY_10     0x25
#define SCSI_READ_10              0x28
#define SCSI_WRITE_10             0x2A
#define SCSI_MODE_SENSE_10        0x5A

/* SCSI Device Types */
#define SCSI_DEVICE_DIRECT_ACCESS 0x00
#define SCSI_DEVICE_RMB           0x80

/* SCSI Status Codes */
#define SCSI_STATUS_GOOD          0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02

/* Sense Key Codes */
#define SCSI_SENSE_NO_SENSE       0x00
#define SCSI_SENSE_RECOVERED_ERROR 0x01
#define SCSI_SENSE_NOT_READY      0x02
#define SCSI_SENSE_MEDIUM_ERROR   0x03
#define SCSI_SENSE_HARDWARE_ERROR 0x04
#define SCSI_SENSE_ILLEGAL_REQUEST 0x05
#define SCSI_SENSE_UNIT_ATTENTION 0x06

/* Additional Sense Codes */
#define SCSI_ASC_MEDIUM_NOT_PRESENT 0x3A00
#define SCSI_ASC_INVALID_COMMAND    0x2400

void scsi_init(struct scsi_context *ctx)
{
	if (!ctx) {
		return;
	}
	
	memset(ctx, 0, sizeof(struct scsi_context));
	ctx->device_ready = false;
}

static int scsi_test_unit_ready(int (*exec_cmd)(void *, const uint8_t *, uint8_t, 
					       uint8_t *, uint32_t, bool),
				void *user_data)
{
	uint8_t cdb[6] = {SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0};
	
	return exec_cmd(user_data, cdb, sizeof(cdb), NULL, 0, false);
}

static int scsi_inquiry(int (*exec_cmd)(void *, const uint8_t *, uint8_t, 
				       uint8_t *, uint32_t, bool),
		       void *user_data, uint8_t *data, uint32_t data_len)
{
	uint8_t cdb[6] = {SCSI_INQUIRY, 0, 0, 0, (uint8_t)data_len, 0};
	
	return exec_cmd(user_data, cdb, sizeof(cdb), data, data_len, true);
}

static int scsi_read_capacity_10(int (*exec_cmd)(void *, const uint8_t *, uint8_t, 
						 uint8_t *, uint32_t, bool),
				 void *user_data, uint32_t *total_blocks, 
				 uint32_t *block_size)
{
	uint8_t cdb[10] = {SCSI_READ_CAPACITY_10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint8_t data[8];
	int ret;
	
	ret = exec_cmd(user_data, cdb, sizeof(cdb), data, sizeof(data), true);
	if (ret == 0) {
		*total_blocks = sys_get_be32(&data[0]) + 1;
		*block_size = sys_get_be32(&data[4]);
	}
	
	return ret;
}

int scsi_device_init_sequence(struct scsi_context *ctx, 
			      int (*exec_cmd)(void *user_data, const uint8_t *cdb, 
					     uint8_t cdb_len, uint8_t *data, 
					     uint32_t data_len, bool data_in),
			      void *user_data)
{
	uint8_t inquiry_data[36];
	uint32_t total_blocks, block_size;
	int ret;
	int retry_count = 0;
	const int max_retries = 3;

	if (!ctx || !exec_cmd) {
		return -EINVAL;
	}

	/* Test Unit Ready with retries */
	while (retry_count < max_retries) {
		ret = scsi_test_unit_ready(exec_cmd, user_data);
		if (ret == 0) {
			break;
		}
		
		retry_count++;
		LOG_WRN("Test Unit Ready failed, retry %d/%d", retry_count, max_retries);
		k_sleep(K_MSEC(100 * retry_count));  /* Exponential backoff */
	}
	
	if (ret != 0) {
		LOG_ERR("Test Unit Ready failed after %d retries", max_retries);
		return ret;
	}

	/* INQUIRY command */
	ret = scsi_inquiry(exec_cmd, user_data, inquiry_data, sizeof(inquiry_data));
	if (ret != 0) {
		LOG_ERR("INQUIRY command failed: %d", ret);
		return ret;
	}

	LOG_INF("Device: %.8s %.16s %.4s", 
		&inquiry_data[8], &inquiry_data[16], &inquiry_data[32]);

	/* Read Capacity */
	ret = scsi_read_capacity_10(exec_cmd, user_data, &total_blocks, &block_size);
	if (ret != 0) {
		LOG_ERR("Read Capacity failed: %d", ret);
		return ret;
	}

	ctx->total_blocks = total_blocks;
	ctx->block_size = block_size;
	ctx->device_ready = true;

	LOG_INF("Capacity: %u blocks x %u bytes = %llu MB",
		total_blocks, block_size, 
		((uint64_t)total_blocks * block_size) / (1024 * 1024));

	return 0;
}

int scsi_build_read_10(uint8_t *cdb, uint32_t lba, uint16_t blocks)
{
	if (!cdb) {
		return -EINVAL;
	}

	cdb[0] = SCSI_READ_10;
	cdb[1] = 0;
	sys_put_be32(lba, &cdb[2]);
	cdb[6] = 0;
	sys_put_be16(blocks, &cdb[7]);
	cdb[9] = 0;

	return 10;  /* CDB length */
}

int scsi_build_write_10(uint8_t *cdb, uint32_t lba, uint16_t blocks)
{
	if (!cdb) {
		return -EINVAL;
	}

	cdb[0] = SCSI_WRITE_10;
	cdb[1] = 0;
	sys_put_be32(lba, &cdb[2]);
	cdb[6] = 0;
	sys_put_be16(blocks, &cdb[7]);
	cdb[9] = 0;

	return 10;  /* CDB length */
}

uint16_t scsi_calc_optimal_transfer_blocks(struct scsi_context *ctx, uint32_t requested_blocks)
{
	uint16_t optimal_blocks;
	uint32_t optimal_bytes;

	if (!ctx || !ctx->device_ready) {
		return SCSI_MIN_TRANSFER_BLOCKS;
	}

	/* Calculate optimal transfer size in blocks */
	optimal_bytes = SCSI_OPTIMAL_TRANSFER_SIZE;
	optimal_blocks = optimal_bytes / ctx->block_size;

	/* Ensure we don't exceed limits */
	if (optimal_blocks > SCSI_MAX_TRANSFER_BLOCKS) {
		optimal_blocks = SCSI_MAX_TRANSFER_BLOCKS;
	}
	
	if (optimal_blocks < SCSI_MIN_TRANSFER_BLOCKS) {
		optimal_blocks = SCSI_MIN_TRANSFER_BLOCKS;
	}

	/* Don't transfer more than requested */
	if (optimal_blocks > requested_blocks) {
		optimal_blocks = requested_blocks;
	}

	return optimal_blocks;
}

int scsi_validate_rw_params(struct scsi_context *ctx, uint32_t lba, uint16_t blocks)
{
	if (!ctx || !ctx->device_ready) {
		return -ENODEV;
	}

	if (blocks == 0) {
		return -EINVAL;
	}

	if (lba >= ctx->total_blocks) {
		LOG_ERR("LBA %u out of range (max: %u)", lba, ctx->total_blocks - 1);
		return -EINVAL;
	}

	if ((uint64_t)lba + blocks > ctx->total_blocks) {
		LOG_ERR("Transfer beyond device capacity: LBA %u + %u blocks", lba, blocks);
		return -EINVAL;
	}

	return 0;
}
