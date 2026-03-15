/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_ADC_ADC_DMA_H_
#define ZEPHYR_DRIVERS_ADC_ADC_DMA_H_

#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>

static inline void adc_dma_block_setup(struct dma_block_config *block,
#ifdef CONFIG_DMA_64BIT
				       uint64_t source_address,
				       uint64_t dest_address,
#else
				       uint32_t source_address,
				       uint32_t dest_address,
#endif
				       uint32_t block_size,
				       uint16_t source_addr_adj,
				       uint16_t dest_addr_adj)
{
	block->source_address = source_address;
	block->dest_address = dest_address;
	block->source_gather_interval = 0U;
	block->dest_scatter_interval = 0U;
	block->dest_scatter_count = 0U;
	block->source_gather_count = 0U;
	block->block_size = block_size;
	block->next_block = NULL;
	block->source_gather_en = 0U;
	block->dest_scatter_en = 0U;
	block->source_addr_adj = source_addr_adj;
	block->dest_addr_adj = dest_addr_adj;
	block->source_reload_en = 0U;
	block->dest_reload_en = 0U;
	block->flow_control_mode = 0U;
}

static inline void adc_dma_config_setup(struct dma_config *config,
					uint32_t dma_slot,
					uint32_t source_data_size,
					uint32_t dest_data_size,
					uint32_t source_burst_length,
					uint32_t dest_burst_length,
					struct dma_block_config *block,
					dma_callback_t callback,
					void *user_data)
{
	config->dma_slot = dma_slot;
	config->channel_direction = PERIPHERAL_TO_MEMORY;
	config->source_data_size = source_data_size;
	config->dest_data_size = dest_data_size;
	config->source_burst_length = source_burst_length;
	config->dest_burst_length = dest_burst_length;
	config->block_count = 1U;
	config->head_block = block;
	config->user_data = user_data;
	config->dma_callback = callback;
}

static inline int adc_dma_configure(const struct device *dma_dev,
				    uint32_t channel,
				    struct dma_config *config)
{
	return dma_config(dma_dev, channel, config);
}

static inline int adc_dma_start(const struct device *dma_dev, uint32_t channel)
{
	return dma_start(dma_dev, channel);
}

static inline int adc_dma_configure_start(const struct device *dma_dev,
					  uint32_t channel,
					  struct dma_config *config)
{
	int ret;

	ret = adc_dma_configure(dma_dev, channel, config);
	if (ret != 0) {
		return ret;
	}

	return adc_dma_start(dma_dev, channel);
}

static inline int adc_dma_stop(const struct device *dma_dev, uint32_t channel)
{
	return dma_stop(dma_dev, channel);
}

#endif /* ZEPHYR_DRIVERS_ADC_ADC_DMA_H_ */