/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for Sony/Philips Digital Interface (SPDIF) controllers.
 */

#ifndef ZEPHYR_INCLUDE_AUDIO_SPDIF_H_
#define ZEPHYR_INCLUDE_AUDIO_SPDIF_H_

/**
 * @defgroup audio_spdif_interface SPDIF Interface
 * @ingroup audio_interface
 * @brief Interfaces for SPDIF transceivers.
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Packed stereo 24-bit samples: 3 bytes left + 3 bytes right. */
#define SPDIF_FRAME_SIZE_BYTES 6U

/** SPDIF stream direction. */
enum spdif_dir {
	SPDIF_DIR_RX,
	SPDIF_DIR_TX,
	SPDIF_DIR_BOTH,
};

/** SPDIF interface state. */
enum spdif_state {
	SPDIF_STATE_NOT_READY,
	SPDIF_STATE_READY,
	SPDIF_STATE_RUNNING,
	SPDIF_STATE_STOPPING,
	SPDIF_STATE_ERROR,
};

/** Trigger commands for SPDIF streaming. */
enum spdif_trigger_cmd {
	SPDIF_TRIGGER_START,
	SPDIF_TRIGGER_STOP,
	SPDIF_TRIGGER_DRAIN,
	SPDIF_TRIGGER_DROP,
	SPDIF_TRIGGER_PREPARE,
};

/** Supported SPDIF audio frame format. */
enum spdif_frame_format {
	SPDIF_FRAME_FORMAT_PCM_24_STEREO,
};

/** RX FIFO watermark selection. */
enum spdif_rx_watermark {
	SPDIF_RX_WATERMARK_1 = 1,
	SPDIF_RX_WATERMARK_4 = 4,
	SPDIF_RX_WATERMARK_8 = 8,
	SPDIF_RX_WATERMARK_16 = 16,
};

/** TX FIFO watermark selection. */
enum spdif_tx_watermark {
	SPDIF_TX_WATERMARK_0 = 0,
	SPDIF_TX_WATERMARK_4 = 4,
	SPDIF_TX_WATERMARK_8 = 8,
	SPDIF_TX_WATERMARK_12 = 12,
};

/** Receiver DPLL gain selection. */
enum spdif_gain {
	SPDIF_GAIN_24 = 24,
	SPDIF_GAIN_16 = 16,
	SPDIF_GAIN_12 = 12,
	SPDIF_GAIN_8 = 8,
	SPDIF_GAIN_6 = 6,
	SPDIF_GAIN_4 = 4,
	SPDIF_GAIN_3 = 3,
};

/** Configuration options for the SPDIF transceiver. */
typedef uint32_t spdif_opt_t;

/** Enable transmitter auto-resync. */
#define SPDIF_OPT_TX_AUTOSYNC BIT(0)
/** Enable receiver auto-resync. */
#define SPDIF_OPT_RX_AUTOSYNC BIT(1)
/** Source the TX user channel from the receiver. */
#define SPDIF_OPT_TX_U_FROM_RX BIT(2)
/** Source the TX user channel from the transmitter-side registers. */
#define SPDIF_OPT_TX_U_FROM_TX BIT(3)
/** Bypass TX audio data directly from the receiver input stream. */
#define SPDIF_OPT_TX_FROM_RX BIT(4)
/** Force the outgoing validity bit to 1. */
#define SPDIF_OPT_VALIDITY_ALWAYS_SET BIT(5)

/** Latched interface status flags. */
typedef uint32_t spdif_status_flags_t;

#define SPDIF_STATUS_DPLL_LOCKED BIT(0)
#define SPDIF_STATUS_TX_FIFO_ERROR BIT(1)
#define SPDIF_STATUS_TX_FIFO_RESYNC BIT(2)
#define SPDIF_STATUS_RX_CNEW BIT(3)
#define SPDIF_STATUS_VALIDITY_NOGOOD BIT(4)
#define SPDIF_STATUS_RX_ILLEGAL_SYMBOL BIT(5)
#define SPDIF_STATUS_RX_PARITY_ERROR BIT(6)
#define SPDIF_STATUS_RX_U_OVERRUN BIT(7)
#define SPDIF_STATUS_RX_Q_OVERRUN BIT(8)
#define SPDIF_STATUS_RX_UQ_SYNC BIT(9)
#define SPDIF_STATUS_RX_UQ_FRAME_ERROR BIT(10)
#define SPDIF_STATUS_RX_FIFO_ERROR BIT(11)
#define SPDIF_STATUS_RX_FIFO_RESYNC BIT(12)
#define SPDIF_STATUS_DPLL_LOCK_LOSS BIT(13)

/** SPDIF configuration shared by TX/RX streams. */
struct spdif_config {
	/** Packed PCM frame format. */
	enum spdif_frame_format format;
	/** Sample rate in Hz. */
	uint32_t sample_rate;
	/** RX/TX memory slab. */
	struct k_mem_slab *mem_slab;
	/** RX/TX block size in bytes, multiple of SPDIF_FRAME_SIZE_BYTES. */
	size_t block_size;
	/** Read/write timeout: K_NO_WAIT, SYS_FOREVER_MS, or milliseconds. */
	int32_t timeout;
	/** Bitwise OR of SPDIF_OPT_* values. */
	spdif_opt_t options;
	/** DPLL clock source selector. */
	uint8_t dpll_clk_source;
	/** TX clock source selector. */
	uint8_t tx_clk_source;
	/** RX FIFO watermark. */
	enum spdif_rx_watermark rx_watermark;
	/** TX FIFO watermark. */
	enum spdif_tx_watermark tx_watermark;
	/** Receiver DPLL gain. */
	enum spdif_gain gain;
};

/** Snapshot of SPDIF runtime metadata and sticky error state. */
struct spdif_status {
	/** Sticky status flags accumulated by the driver. */
	spdif_status_flags_t flags;
	/** Most recent receiver channel-status low word. */
	uint32_t rx_channel_status_low;
	/** Most recent receiver channel-status high word. */
	uint32_t rx_channel_status_high;
	/** Current transmitter channel-status low word. */
	uint32_t tx_channel_status_low;
	/** Current transmitter channel-status high word. */
	uint32_t tx_channel_status_high;
	/** Last received U-channel word. */
	uint32_t rx_u_bits;
	/** Last received Q-channel word. */
	uint32_t rx_q_bits;
	/** Measured receive sample rate in Hz, or 0 if not locked. */
	uint32_t rx_sample_rate;
};

/** Driver API for SPDIF controllers. */
__subsystem struct spdif_driver_api {
	int (*configure)(const struct device *dev, enum spdif_dir dir,
			 const struct spdif_config *cfg);
	const struct spdif_config *(*config_get)(const struct device *dev,
					 enum spdif_dir dir);
	int (*read)(const struct device *dev, void **mem_block, size_t *size);
	int (*write)(const struct device *dev, void *mem_block, size_t size);
	int (*trigger)(const struct device *dev, enum spdif_dir dir,
		       enum spdif_trigger_cmd cmd);
	int (*status_get)(const struct device *dev, struct spdif_status *status);
	int (*channel_status_set)(const struct device *dev, uint32_t low,
				  uint32_t high);
};

static inline int spdif_configure(const struct device *dev, enum spdif_dir dir,
				  const struct spdif_config *cfg)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->configure(dev, dir, cfg);
}

static inline const struct spdif_config *spdif_config_get(const struct device *dev,
						 enum spdif_dir dir)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->config_get(dev, dir);
}

static inline int spdif_read(const struct device *dev, void **mem_block,
			     size_t *size)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->read(dev, mem_block, size);
}

static inline int spdif_write(const struct device *dev, void *mem_block,
			      size_t size)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->write(dev, mem_block, size);
}

static inline int spdif_trigger(const struct device *dev, enum spdif_dir dir,
				enum spdif_trigger_cmd cmd)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->trigger(dev, dir, cmd);
}

static inline int spdif_status_get(const struct device *dev,
				   struct spdif_status *status)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->status_get(dev, status);
}

static inline int spdif_channel_status_set(const struct device *dev, uint32_t low,
					   uint32_t high)
{
	const struct spdif_driver_api *api =
		(const struct spdif_driver_api *)dev->api;

	return api->channel_status_set(dev, low, high);
}

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_INCLUDE_AUDIO_SPDIF_H_ */