/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Common ADC DMA helper utilities for ADC drivers.
 *
 * This header provides shared data structures, inline helper functions,
 * and macros to reduce boilerplate DMA code across ADC drivers.
 * It follows the same header-only pattern as adc_context.h.
 */

#ifndef ZEPHYR_DRIVERS_ADC_ADC_DMA_H_
#define ZEPHYR_DRIVERS_ADC_ADC_DMA_H_

#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/logging/log.h>

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compile-time DMA configuration for an ADC peripheral.
 *
 * Embed this in the driver's config struct.  Populate it via the
 * ADC_DMA_CONFIG_DT_INST_INIT_BY_NAME / ADC_DMA_CONFIG_DT_INST_INIT_BY_IDX
 * macros or manually.
 */
struct adc_dma_config {
	/** DMA controller device (may be NULL if DMA is optional and absent). */
	const struct device *dma_dev;
	/** DMA channel number. */
	uint32_t dma_channel;
	/** DMA request slot / mux (vendor-specific). */
	uint32_t dma_slot;
};

/**
 * @brief Runtime DMA transfer state for an ADC peripheral.
 *
 * Embed this in the driver's data struct.
 */
struct adc_dma_data {
	struct dma_config dma_cfg;
	struct dma_block_config dma_blk_cfg;
};

/*
 * ---------------------------------------------------------------------------
 *  Devicetree initialization macros
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Initialize an adc_dma_config from a named DT dmas entry.
 *
 * @param n          DT instance number.
 * @param dma_name   Name of the dmas entry (e.g. fifoa).
 * @param chan_cell  Name of the channel cell (e.g. mux, channel).
 * @param slot_cell  Name of the slot/source cell (e.g. source, slot).
 */
#define ADC_DMA_CONFIG_DT_INST_INIT_BY_NAME(n, dma_name, chan_cell, slot_cell) \
	{                                                                      \
		.dma_dev = DEVICE_DT_GET(                                      \
			DT_INST_DMAS_CTLR_BY_NAME(n, dma_name)),              \
		.dma_channel = DT_INST_DMAS_CELL_BY_NAME(                     \
			n, dma_name, chan_cell),                                \
		.dma_slot = DT_INST_DMAS_CELL_BY_NAME(                        \
			n, dma_name, slot_cell),                               \
	}

/**
 * @brief Initialize an adc_dma_config from a DT dmas entry by index.
 *
 * @param n          DT instance number.
 * @param idx        Index of the dmas entry (typically 0).
 * @param slot_cell  Name of the slot/source cell (e.g. source, slot).
 */
#define ADC_DMA_CONFIG_DT_INST_INIT_BY_IDX(n, idx, slot_cell)                 \
	{                                                                      \
		.dma_dev = DEVICE_DT_GET(                                      \
			DT_INST_DMAS_CTLR_BY_IDX(n, idx)),                    \
		.dma_channel = DT_INST_DMAS_CELL_BY_IDX(n, idx, channel),     \
		.dma_slot = DT_INST_DMAS_CELL_BY_IDX(n, idx, slot_cell),      \
	}

/*
 * ---------------------------------------------------------------------------
 *  Inline helper functions
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Check whether the DMA device is present and ready.
 *
 * @param dma_cfg  Pointer to the compile-time adc_dma_config.
 * @retval true  DMA device is non-NULL and ready.
 * @retval false DMA device is NULL or not ready.
 */
static inline bool adc_dma_is_ready(const struct adc_dma_config *dma_cfg)
{
	return (dma_cfg->dma_dev != NULL) && device_is_ready(dma_cfg->dma_dev);
}

/**
 * @brief Configure a DMA block for a peripheral-to-memory ADC transfer.
 *
 * Sets the common fields for a single-block peripheral-to-memory transfer:
 * source address fixed (ADC result register), destination address incrementing
 * (sample buffer).
 *
 * @param blk        Pointer to the dma_block_config to configure.
 * @param src_addr   Hardware register address (ADC result FIFO / data register).
 * @param dst_addr   Destination buffer address.
 * @param block_size Total bytes to transfer (channel_count * sample_size).
 */
static inline void adc_dma_configure_block(struct dma_block_config *blk,
					   uint32_t src_addr,
					   uint32_t dst_addr,
					   uint32_t block_size)
{
	memset(blk, 0, sizeof(*blk));
	blk->source_address = src_addr;
	blk->dest_address = dst_addr;
	blk->block_size = block_size;
	blk->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	blk->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
}

/**
 * @brief Populate the DMA config for an ADC peripheral-to-memory transfer.
 *
 * Fills in @p dma_data->dma_cfg with standard settings and links the
 * block config.  Does NOT call dma_config() or dma_start().
 *
 * @param dma_data      Runtime DMA data (contains dma_cfg and dma_blk_cfg).
 * @param dma_cfg_const Compile-time DMA identity (provides dma_slot).
 * @param data_size     Width of a single sample in bytes (e.g. 2 or 4).
 * @param callback      DMA completion callback (may be NULL).
 * @param user_data     Opaque pointer passed to the callback.
 */
static inline void adc_dma_configure(struct adc_dma_data *dma_data,
				     const struct adc_dma_config *dma_cfg_const,
				     uint32_t data_size,
				     dma_callback_t callback,
				     void *user_data)
{
	struct dma_config *cfg = &dma_data->dma_cfg;

	memset(cfg, 0, sizeof(*cfg));
	cfg->dma_slot = dma_cfg_const->dma_slot;
	cfg->channel_direction = PERIPHERAL_TO_MEMORY;
	cfg->source_data_size = data_size;
	cfg->dest_data_size = data_size;
	cfg->source_burst_length = data_size;
	cfg->dest_burst_length = data_size;
	cfg->block_count = 1;
	cfg->head_block = &dma_data->dma_blk_cfg;
	cfg->dma_callback = callback;
	cfg->user_data = user_data;
}

/**
 * @brief Configure the DMA channel and start the transfer.
 *
 * Calls dma_config() followed by dma_start().
 *
 * @param dma_cfg_const  Compile-time DMA config (device + channel).
 * @param dma_data       Runtime DMA data (contains the populated dma_cfg).
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
static inline int adc_dma_start(const struct adc_dma_config *dma_cfg_const,
				struct adc_dma_data *dma_data)
{
	int ret;

	ret = dma_config(dma_cfg_const->dma_dev, dma_cfg_const->dma_channel,
			 &dma_data->dma_cfg);
	if (ret != 0) {
		return ret;
	}

	return dma_start(dma_cfg_const->dma_dev, dma_cfg_const->dma_channel);
}

/**
 * @brief Stop a running DMA transfer.
 *
 * @param dma_cfg_const  Compile-time DMA config (device + channel).
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
static inline int adc_dma_stop(const struct adc_dma_config *dma_cfg_const)
{
	return dma_stop(dma_cfg_const->dma_dev, dma_cfg_const->dma_channel);
}

/*
 * ---------------------------------------------------------------------------
 *  Callback generation macro
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Generate a standard ADC DMA callback function.
 *
 * Creates a static function named @p _name with the dma_callback_t signature.
 * user_data must point to the driver's data struct.  On success the callback
 * stops DMA and calls adc_context_on_sampling_done(); on error it calls
 * adc_context_complete() with the error status.
 *
 * @param _name       Name of the generated callback function.
 * @param _data_type  Driver data struct type (e.g. struct iadc_data).
 * @param _ctx_field  Name of the adc_context field in the data struct.
 * @param _dev_field  Name of the const struct device * field in the data struct.
 * @param _dma_cfg    Expression to obtain the adc_dma_config pointer from data.
 */
#define ADC_DMA_CALLBACK_DEFINE(_name, _data_type, _ctx_field, _dev_field,     \
				_dma_cfg)                                      \
static void _name(const struct device *dma_dev, void *user_data,               \
		  uint32_t channel, int status)                                \
{                                                                              \
	_data_type *_data = (_data_type *)user_data;                           \
	const struct adc_dma_config *_cfg = _dma_cfg;                          \
                                                                               \
	ARG_UNUSED(dma_dev);                                                   \
	ARG_UNUSED(channel);                                                   \
                                                                               \
	adc_dma_stop(_cfg);                                                    \
                                                                               \
	if (status < 0) {                                                      \
		LOG_ERR("DMA transfer error: %d", status);                     \
		adc_context_complete(&_data->_ctx_field, status);              \
		return;                                                        \
	}                                                                      \
                                                                               \
	adc_context_on_sampling_done(&_data->_ctx_field, _data->_dev_field);   \
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_ADC_ADC_DMA_H_ */
