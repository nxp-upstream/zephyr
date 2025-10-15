/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USBH_MSC_SCSI_H_
#define USBH_MSC_SCSI_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SCSI Commands */
#define SCSI_INQUIRY              0x12
#define SCSI_READ_CAPACITY_10     0x25
#define SCSI_READ_10              0x28
#define SCSI_WRITE_10             0x2A
#define SCSI_TEST_UNIT_READY      0x00
#define SCSI_REQUEST_SENSE        0x03

/* SCSI Status */
#define SCSI_STATUS_GOOD          0x00
#define SCSI_STATUS_CHECK         0x02

/* Transfer optimization parameters */
#define SCSI_MAX_TRANSFER_BLOCKS  256
#define SCSI_MIN_TRANSFER_BLOCKS  1
#define SCSI_OPTIMAL_TRANSFER_SIZE (64 * 1024)  /* 64KB */

struct scsi_context {
	uint32_t total_blocks;
	uint16_t block_size;
	bool device_ready;
	uint8_t last_sense_key;
	uint8_t last_asc;
	uint8_t last_ascq;
};

/* Function prototypes */
void scsi_init(struct scsi_context *ctx);
int scsi_device_init_sequence(struct scsi_context *ctx, 
			      int (*exec_cmd)(void *user_data, const uint8_t *cdb, 
					     uint8_t cdb_len, uint8_t *data, 
					     uint32_t data_len, bool data_in),
			      void *user_data);
int scsi_build_read_10(uint8_t *cdb, uint32_t lba, uint16_t blocks);
int scsi_build_write_10(uint8_t *cdb, uint32_t lba, uint16_t blocks);
uint16_t scsi_calc_optimal_transfer_blocks(struct scsi_context *ctx, uint32_t requested_blocks);
int scsi_validate_rw_params(struct scsi_context *ctx, uint32_t lba, uint16_t blocks);

#ifdef __cplusplus
}
#endif

#endif /* USBH_MSC_SCSI_H_ */
