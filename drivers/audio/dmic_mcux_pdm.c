/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dma.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <soc.h>

#include <fsl_pdm.h>

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
LOG_MODULE_REGISTER(dmic_mcux_pdm, CONFIG_AUDIO_DMIC_LOG_LEVEL);

#define DT_DRV_COMPAT nxp_mcux_pdm

/* Default oversampling rate */
#define PDM_OSR_DEFAULT 16
/* Maximum number of PDM channels */
#define PDM_MAX_CHANNELS 8

#define CONFIG_DMIC_RX_BLOCK_COUNT 4
#define DMA_BLOCKS 8

struct mcux_pdm_channel {
	pdm_df_output_gain_t gain;
	pdm_dc_remover_t cutOffFreq;
};

struct mcux_pdm_drv_data {
	struct k_mem_slab *mem_slab;
	uint32_t block_size;
	PDM_Type *base;
	const struct device *dma_dev;
	uint8_t dma_channel;
	uint8_t act_num_chan;
	uint32_t chan_map_lo;
	uint32_t chan_map_hi;
	enum dmic_state dmic_state;
	uint32_t sample_rate;
	const struct mcux_pdm_channel *channels;

	struct k_msgq in_queue;
	struct k_msgq out_queue;
	void *in_msgs[CONFIG_DMIC_RX_BLOCK_COUNT];
	void *out_msgs[CONFIG_DMIC_RX_BLOCK_COUNT];
	struct dma_config dma_cfg;
	struct dma_block_config dma_block[DMA_BLOCKS];
	uint32_t curr_chan;
	void *curr_buf;
};

struct mcux_pdm_cfg {
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_name;
	const struct reset_dt_spec reset;
	const struct device *dma_dev;
	uint8_t dma_channel;
	uint8_t dma_source;
	uint32_t fifo_depth;
	const struct mcux_pdm_channel *channels;
	uint8_t num_channels;
	void (*irq_config_func)(const struct device *dev);
};

/* Parse gain enum from device tree */
static pdm_df_output_gain_t pdm_parse_gain(uint32_t gain)
{
	return (pdm_df_output_gain_t)gain;
}

/* Parse DC cutoff enum from device tree */
static pdm_dc_remover_t pdm_parse_dc_cutoff(uint32_t dc_idx)
{
#if SOC_SERIES_IMXRT11XX
	switch (dc_idx) {
		case 21U: return kPDM_DcRemoverCutOff21Hz;
		case 83U: return kPDM_DcRemoverCutOff83Hz;
		case 152U: return kPDM_DcRemoverCutOff152Hz;
		default: return kPDM_DcRemoverBypass;
	}
#elif SOC_SERIES_MCXN
	switch (dc_idx) {
		case 13U: return kPDM_DcRemoverCutOff13Hz;
		case 20U: return kPDM_DcRemoverCutOff20Hz;
		case 40U: return kPDM_DcRemoverCutOff40Hz;
		default: return kPDM_DcRemoverBypass;
	}
#else
	return 0;
#endif
}

/* Gets hardware channel index from logical channel */
static uint8_t pdm_mcux_hw_chan(struct mcux_pdm_drv_data *drv_data,
				uint8_t log_chan)
{
	enum pdm_lr lr;
	uint8_t hw_chan;

	dmic_parse_channel_map(drv_data->chan_map_lo,
			       drv_data->chan_map_hi,
			       log_chan, &hw_chan, &lr);
	
	return hw_chan;
}

static void pdm_mcux_activate_channels(struct mcux_pdm_drv_data *drv_data,
				       bool enable)
{
	uint32_t mask = 0;

	/* Build channel enable mask */
	for (uint8_t chan = 0; chan < drv_data->act_num_chan; chan++) {
		mask |= BIT(pdm_mcux_hw_chan(drv_data, chan));
	}

	if (enable) {
		/* Enable selected channels */
		for (uint8_t i = 0; i < PDM_MAX_CHANNELS; i++) {
			if (mask & BIT(i)) {
				drv_data->base->CTRL_1 |= (PDM_CTRL_1_CH0EN_MASK << i);
			}
		}
		/* Enable PDM module */
		PDM_Enable(drv_data->base, true);
	} else {
		/* Disable PDM module */
		PDM_Enable(drv_data->base, false);
		/* Disable selected channels */
		for (uint8_t i = 0; i < PDM_MAX_CHANNELS; i++) {
			if (mask & BIT(i)) {
				drv_data->base->CTRL_1 &= ~(PDM_CTRL_1_CH0EN_MASK << i);
			}
		}
	}
}

static void pdm_purge_stream_buffers(struct mcux_pdm_drv_data *drv_data, 
				     bool in_drop, bool out_drop)
{
	void *buffer;

	if (in_drop) {
		while (k_msgq_get(&drv_data->in_queue, &buffer, K_NO_WAIT) == 0) {
			k_mem_slab_free(drv_data->mem_slab, buffer);
		}
	}

	if (out_drop) {
		while (k_msgq_get(&drv_data->out_queue, &buffer, K_NO_WAIT) == 0) {
			k_mem_slab_free(drv_data->mem_slab, buffer);
		}
	}
}

static void pdm_mcux_stream_disable(const struct device *dev, bool in_drop, bool out_drop)
{
	struct mcux_pdm_drv_data *drv_data = dev->data;

	LOG_DBG("Stopping PDM stream & DMA channel %u", drv_data->dma_channel);
	
	/* Stop DMA */
	dma_stop(drv_data->dma_dev, drv_data->dma_channel);

	/* Disable PDM DMA requests */
	PDM_EnableDMA(drv_data->base, false);

	/* Disable channels */
	pdm_mcux_activate_channels(drv_data, false);

	/* Purge buffers queued in the stream */
	pdm_purge_stream_buffers(drv_data, in_drop, out_drop);
}

static void pdm_mcux_dma_cb(const struct device *dma_dev, void *arg, uint32_t channel,
			    int status)
{
	struct device *dev = (struct device *)arg;
	struct mcux_pdm_drv_data *drv_data = dev->data;
	int ret;

	LOG_DBG("PDM RX cb");

	if (drv_data->dmic_state == DMIC_STATE_ERROR) {
		pdm_mcux_stream_disable(dev, true, true);
		return;
	}

	if (drv_data->dmic_state != DMIC_STATE_ACTIVE) {
		return;
	}

	// Advance the current channel index
	//drv_data->curr_chan = (drv_data->curr_chan + 1) % drv_data->act_num_chan;

	// That is 0 - the next series of DMA transfers should use a new buffer

	/*
		Likewise as below here - this was supposed to count channels (transferred TCDs),
		but isn't relevant as we're using a single block now because of nxp,mcux-edma driver
		and dma API limitations.
	*/
	if(true)
	{
		drv_data->curr_chan = 0;
		LOG_DBG("Acquiring new buffer.");

		/* Put current buffer back into the input queue - it is expected to be used later (we're simulating a ring of buffers this way) */
		ret = k_msgq_put(&drv_data->in_queue, &drv_data->curr_buf, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("%p -> in_queue %p err %d", drv_data->curr_buf, &drv_data->in_queue, ret);
		}

		/* Put the current buffer into the output queue, it's ready for the user */
		ret = k_msgq_put(&drv_data->out_queue, &drv_data->curr_buf, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("buffer %p -> out_queue %p err %d", drv_data->curr_buf, &drv_data->out_queue, ret);
			goto error;
		}

		/* Get a new buffer from the input queue */
		ret = k_msgq_get(&drv_data->in_queue, &drv_data->curr_buf, K_NO_WAIT);
		__ASSERT_NO_MSG(ret == 0);
	}

	/* Reload completed DMA transfer with a new buffer */
	ret = dma_reload(
		drv_data->dma_dev, 
		drv_data->dma_channel, 
		PDM_GetDataRegisterAddress(drv_data->base, pdm_mcux_hw_chan(drv_data, drv_data->curr_chan)), 
		(uint32_t)(((uint32_t *)drv_data->curr_buf) + drv_data->curr_chan), 
		drv_data->block_size / drv_data->act_num_chan
	);
	if (ret < 0) {
		LOG_ERR("dma_reload() failed with error 0x%x", ret);
		goto error;
	}

	return;

error:
	pdm_mcux_stream_disable(dev, false, false);
	drv_data->dmic_state = DMIC_STATE_ERROR;
}

static int pdm_mcux_stream_start(const struct device *dev)
{
	int ret = 0;
	struct mcux_pdm_drv_data *drv_data = dev->data;

	for(size_t i = 0; i < CONFIG_DMIC_RX_BLOCK_COUNT; i++)
	{
		void *buf;

		/* Allocate 1st receive buffer from SLAB */
		ret = k_mem_slab_alloc(drv_data->mem_slab, &buf, K_NO_WAIT);
		if (ret != 0) {
			LOG_DBG("buffer alloc from mem_slab failed (%d)", ret);
			return ret;
		}

		/* Put it into the queues */
		ret = k_msgq_put(&drv_data->in_queue, &buf, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("failed to put buffer in input queue, ret1 %d", ret);
			return ret;
		}

		ret = k_msgq_put(&drv_data->out_queue, &buf, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("Failed to put buffer in output queue, ret %d", ret);
			return ret;
		}
	}
	
	/* ...and retrieve the first one */
	void *buffer;
	ret = k_msgq_get(&drv_data->in_queue, &buffer, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("failed to get buffer from input queue, ret1 %d", ret);
		return ret;
	}

	/* ...and pop off the same buffer from the out one (this is to preload the queue with zeroes) */
	ret = k_msgq_get(&drv_data->out_queue, &buffer, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("failed to get buffer from output queue, ret2 %d", ret);
		return ret;
	}

	/* 
		Iterate over enabled channels and build DMA block descriptors.

		The for loop construct here is redundant as this driver WILL enable
		only a single channel, but it's left in here as a placeholder for 
		further development, if DMA ever gets implemented properly.
	*/
	for(uint8_t i = 0; i < 1; i++)
	{
		drv_data->dma_block[i] = (struct dma_block_config) {
			.source_address = PDM_GetDataRegisterAddress(drv_data->base, pdm_mcux_hw_chan(drv_data, i)),
			.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
			.source_reload_en = 1,

			.dest_address = (uint32_t)buffer + (i * sizeof(uint32_t)),
			.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT,
			.dest_scatter_interval = drv_data->act_num_chan * sizeof(uint32_t),
			.dest_scatter_en = 1,
			.dest_reload_en = 1,

			.block_size = drv_data->block_size / drv_data->act_num_chan,

			.next_block = NULL
		};
	}
	
	drv_data->dma_cfg.block_count = drv_data->act_num_chan;
	dma_config(drv_data->dma_dev, drv_data->dma_channel, &drv_data->dma_cfg);

	drv_data->curr_chan = 0;
	drv_data->curr_buf = buffer;

	LOG_DBG("Starting DMA Ch%u", drv_data->dma_channel);
	ret = dma_start(drv_data->dma_dev, drv_data->dma_channel);
	if (ret < 0) {
		LOG_ERR("Failed to start DMA Ch%d (%d)", drv_data->dma_channel, ret);
		return ret;
	}

	/* Enable PDM DMA requests */
	PDM_EnableDMA(drv_data->base, true);

	/* Enable PDM channels */
	pdm_mcux_activate_channels(drv_data, true);

	return 0;
}

/* Initializes a PDM hardware channel */
static int pdm_mcux_init_channel(const struct device *dev, uint8_t chan)
{
	const struct mcux_pdm_cfg *config = dev->config;
	struct mcux_pdm_drv_data *drv_data = dev->data;
	pdm_channel_config_t chan_config = {0};

	if (chan >= config->num_channels || !config->channels) {
		/* Channel not configured */
		return -EINVAL;
	}

	/* Set channel configuration from device tree */
	chan_config.gain = pdm_parse_gain(config->channels[chan].gain);
#if CONFIG_SOC_SERIES_IMXRT11XX
	chan_config.cutOffFreq = pdm_parse_dc_cutoff(config->channels[chan].cutOffFreq);
#elif CONFIG_SOC_SERIES_MCXN
	chan_config.outputCutOffFreq = pdm_parse_dc_cutoff(config->channels[chan].cutOffFreq);
#endif

	/* Configure the channel */
	PDM_SetChannelConfig(drv_data->base, chan, &chan_config);

	return 0;
}

static int mcux_pdm_init(const struct device *dev)
{
	const struct mcux_pdm_cfg *config = dev->config;
	struct mcux_pdm_drv_data *drv_data = dev->data;
	pdm_config_t pdm_config = {0};
	int ret;

	if (!drv_data->dma_dev) {
		LOG_ERR("DMA device not found");
		return -ENODEV;
	}

	k_msgq_init(&drv_data->in_queue, (char *)drv_data->in_msgs, sizeof(void *),
		    CONFIG_DMIC_RX_BLOCK_COUNT);
	k_msgq_init(&drv_data->out_queue, (char *)drv_data->out_msgs, sizeof(void *),
		    CONFIG_DMIC_RX_BLOCK_COUNT);

	/* Apply pinctrl configuration */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Failed to apply pinctrl state: %d", ret);
		return ret;
	}

	pdm_config.fifoWatermark = config->fifo_depth - 1;
	pdm_config.qualityMode = kPDM_QualityModeHigh;
	pdm_config.cicOverSampleRate = PDM_OSR_DEFAULT;

	PDM_Init(drv_data->base, &pdm_config);

	drv_data->dma_cfg = (struct dma_config) {
		.dma_slot = config->dma_source,
		.channel_direction = PERIPHERAL_TO_MEMORY,
		.complete_callback_en = 1,
		.error_callback_dis = 1,
		.cyclic = 1,

		.source_data_size = sizeof(uint32_t),
		.dest_data_size = sizeof(uint32_t),
		.source_burst_length = sizeof(uint32_t),
		.dest_burst_length = sizeof(uint32_t),

		.head_block = &drv_data->dma_block[0],
		
		.user_data = (void *)dev,
		.dma_callback = pdm_mcux_dma_cb,
	};

	/* Configure interrupt if available */
	if (config->irq_config_func) {
		config->irq_config_func(dev);
	}

	drv_data->dmic_state = DMIC_STATE_INITIALIZED;
	
	return 0;
}

static int dmic_pdm_mcux_configure(const struct device *dev,
				   struct dmic_cfg *config)
{
	const struct mcux_pdm_cfg *drv_config = dev->config;
	struct mcux_pdm_drv_data *drv_data = dev->data;
	struct pdm_chan_cfg *channel = &config->channel;
	struct pcm_stream_cfg *stream = &config->streams[0];
	uint32_t bit_clk_rate;
	int ret;

	if (drv_data->dmic_state == DMIC_STATE_ACTIVE) {
		LOG_ERR("Cannot configure device while it is active");
		return -EBUSY;
	}

	/* Only one active stream is supported */
	if (channel->req_num_streams != 1) {
		return -EINVAL;
	}

	/* PDM supports up to 8 active channels */
	if (channel->req_num_chan > PDM_MAX_CHANNELS) {
		LOG_ERR("PDM only supports %d channels or less", PDM_MAX_CHANNELS);
		return -ENOTSUP;
	}

	if (stream->pcm_rate == 0 || stream->pcm_width == 0) {
		if (drv_data->dmic_state == DMIC_STATE_CONFIGURED) {
			PDM_Deinit(drv_data->base);
			drv_data->dmic_state = DMIC_STATE_UNINIT;
		}
		return 0;
	}

	/* Re-initialize if needed */
	if (drv_data->dmic_state == DMIC_STATE_UNINIT) {
		ret = mcux_pdm_init(dev);
		if (ret < 0) {
			LOG_ERR("Could not reinit PDM");
			return ret;
		}
	}

	/* Only 32-bit samples are supported */
	if (stream->pcm_width != 32) {
		LOG_ERR("Only 32 bit samples are supported");
		return -ENOTSUP;
	}

	/* Get clock rate */
	ret = clock_control_get_rate(drv_config->clock_dev,
				     drv_config->clock_name, &bit_clk_rate);
	if (ret < 0) {
		return ret;
	}

	/* Check bit clock rate versus user requirements */
	if ((config->io.min_pdm_clk_freq > bit_clk_rate) ||
	    (config->io.max_pdm_clk_freq < bit_clk_rate)) {
		return -EINVAL;
	}

	/* Save channel mapping */
	drv_data->chan_map_lo = channel->req_chan_map_lo;
	drv_data->chan_map_hi = channel->req_chan_map_hi;

	PDM_Reset(drv_data->base);

	/* Configure channels */
	channel->act_num_chan = 0;
	for (uint8_t chan = 0; chan < channel->req_num_chan; chan++) {
		/* Configure selected channels */
		ret = pdm_mcux_init_channel(dev, pdm_mcux_hw_chan(drv_data, chan));
		if (ret < 0) {
			return ret;
		}
		channel->act_num_chan++;
	}

	/* Configure sample rate */
	ret = PDM_SetSampleRateConfig(drv_data->base, bit_clk_rate, stream->pcm_rate);
	if (ret == kStatus_Fail) {
		LOG_ERR("Failed to set sample rate config: %d", stream->pcm_rate);
		return -EINVAL;
	}

	channel->act_chan_map_lo = channel->req_chan_map_lo;
	channel->act_chan_map_hi = channel->req_chan_map_hi;

	/* Save stream configuration */
	drv_data->mem_slab = stream->mem_slab;
	drv_data->block_size = stream->block_size;
	drv_data->act_num_chan = channel->act_num_chan;
	drv_data->sample_rate = stream->pcm_rate;
	drv_data->dmic_state = DMIC_STATE_CONFIGURED;

	return 0;
}

static int dmic_pdm_mcux_trigger(const struct device *dev,
				 enum dmic_trigger cmd)
{
	struct mcux_pdm_drv_data *drv_data = dev->data;
	unsigned int key;
	int ret = 0;

	key = irq_lock();
	
	switch (cmd) {
	case DMIC_TRIGGER_START:
		if (drv_data->dmic_state != DMIC_STATE_CONFIGURED) {
			LOG_ERR("START trigger: invalid state %u", drv_data->dmic_state);
			ret = -EIO;
			break;
		}

		ret = pdm_mcux_stream_start(dev);
		if (ret < 0) {
			LOG_DBG("START trigger failed %d", ret);
			ret = -EIO;
			break;
		}

		drv_data->dmic_state = DMIC_STATE_ACTIVE;
		break;

	case DMIC_TRIGGER_STOP:
		if (drv_data->dmic_state != DMIC_STATE_ACTIVE) {
			LOG_ERR("STOP trigger: invalid state %d", drv_data->dmic_state);
			ret = -EIO;
			break;
		}

		drv_data->dmic_state = DMIC_STATE_CONFIGURED;
		pdm_mcux_stream_disable(dev, true, true);
		break;

	case DMIC_TRIGGER_PAUSE:
		if (drv_data->dmic_state != DMIC_STATE_ACTIVE) {
			LOG_ERR("PAUSE trigger: invalid state %d", drv_data->dmic_state);
			ret = -EIO;
			break;
		}

		pdm_mcux_activate_channels(drv_data, false);
		drv_data->dmic_state = DMIC_STATE_PAUSED;
		break;

	case DMIC_TRIGGER_RELEASE:
		if (drv_data->dmic_state != DMIC_STATE_PAUSED) {
			LOG_ERR("RELEASE trigger: invalid state %d", drv_data->dmic_state);
			ret = -EIO;
			break;
		}

		pdm_mcux_activate_channels(drv_data, true);
		drv_data->dmic_state = DMIC_STATE_ACTIVE;
		break;

	case DMIC_TRIGGER_RESET:
		/* Reset PDM to uninitialized state */
		pdm_mcux_stream_disable(dev, true, true);
		PDM_Deinit(drv_data->base);
		drv_data->dmic_state = DMIC_STATE_UNINIT;
		break;

	default:
		LOG_ERR("Invalid command: %d", cmd);
		ret = -EINVAL;
	}

	irq_unlock(key);
	return ret;
}

static int dmic_pdm_mcux_read(const struct device *dev,
			      uint8_t stream,
			      void **buffer, size_t *size, int32_t timeout)
{
	struct mcux_pdm_drv_data *drv_data = dev->data;
	int ret;

	ARG_UNUSED(stream);

	LOG_DBG("dmic_pdm_mcux_read");
	if (drv_data->dmic_state == DMIC_STATE_UNINIT) {
		LOG_ERR("invalid state %d", drv_data->dmic_state);
		return -EIO;
	}

	ret = k_msgq_get(&drv_data->out_queue, buffer, SYS_TIMEOUT_MS(timeout));
	if (ret != 0) {
		if (drv_data->dmic_state == DMIC_STATE_ERROR) {
			ret = -EIO;
		} else {
			LOG_DBG("need retry");
			ret = -EAGAIN;
		}
		return ret;
	}

	*size = drv_data->block_size;
	return 0;
}

static const struct _dmic_ops dmic_ops = {
	.configure = dmic_pdm_mcux_configure,
	.trigger = dmic_pdm_mcux_trigger,
	.read = dmic_pdm_mcux_read,
};

/* IRQ handler for PDM */
static void pdm_mcux_isr(const struct device *dev)
{
	struct mcux_pdm_drv_data *drv_data = dev->data;
	uint32_t status;
	
	/* Get and clear interrupt status */
	status = PDM_GetStatus(drv_data->base);
	PDM_ClearStatus(drv_data->base, status);
	
#if SOC_SERIES_IMXRT11XX
	/* Handle errors if any */
	if (status & PDM_STAT_FIR_RDY_MASK) {
		/* FIR filter ready - normal operation */
		LOG_DBG("PDM FIR ready: 0x%08x", status);
	}
#endif
}

/* Defines structure for a given PDM channel node */
#define PDM_MCUX_CHAN_DEFINE(pdm_node)						\
	{									\
		.gain = DT_PROP(pdm_node, gain),				\
		.cutOffFreq = DT_ENUM_IDX(pdm_node, dc_cutoff),			\
	},

/* Defines channel config array for all enabled PDM channels */
#define PDM_MCUX_CHANNELS_DEFINE(idx)						\
	static const struct mcux_pdm_channel pdm_channels_##idx[] = {		\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(idx, PDM_MCUX_CHAN_DEFINE)	\
	};

/* Count enabled channels */
#define PDM_MCUX_CHAN_COUNT(pdm_node) (1)
#define PDM_MCUX_CHANNELS_COUNT(idx) \
	(DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(idx, PDM_MCUX_CHAN_COUNT, (+)))

/* IRQ configuration macro */
#define PDM_MCUX_IRQ_CONFIG(idx)						\
	static void pdm_mcux_irq_config_##idx(const struct device *dev)	\
	{									\
		IRQ_CONNECT(DT_INST_IRQN(idx), DT_INST_IRQ(idx, priority),	\
			    pdm_mcux_isr, DEVICE_DT_INST_GET(idx), 0);		\
		irq_enable(DT_INST_IRQN(idx));					\
	}

#define PDM_MCUX_DEVICE(idx)							\
	PDM_MCUX_CHANNELS_DEFINE(idx)						\
	PDM_MCUX_IRQ_CONFIG(idx)						\
	static struct mcux_pdm_drv_data mcux_pdm_data_##idx = {			\
		.base = (PDM_Type *)DT_INST_REG_ADDR(idx),			\
		.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_IDX(idx, 0)),	\
		.dma_channel = DT_INST_DMAS_CELL_BY_IDX(idx, 0, mux),	\
		.dmic_state = DMIC_STATE_UNINIT,				\
		.channels = pdm_channels_##idx,					\
	};									\
										\
	PINCTRL_DT_INST_DEFINE(idx);						\
	static const struct mcux_pdm_cfg mcux_pdm_cfg_##idx = {			\
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),			\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),		\
		.clock_name = (clock_control_subsys_t)				\
			      DT_INST_CLOCKS_CELL(idx, name),			\
		.reset = RESET_DT_SPEC_INST_GET_OR(idx, {0}),			\
		.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_IDX(idx, 0)),	\
		.dma_channel = DT_INST_DMAS_CELL_BY_IDX(idx, 0, mux),	\
		.dma_source = DT_INST_DMAS_CELL_BY_IDX(idx, 0, source),		\
		.fifo_depth = DT_INST_PROP_OR(idx, fifo_depth, 8),		\
		.channels = pdm_channels_##idx,					\
		.num_channels = PDM_MCUX_CHANNELS_COUNT(idx),			\
		.irq_config_func = COND_CODE_1(DT_INST_IRQ_HAS_IDX(idx, 0),	\
					       (pdm_mcux_irq_config_##idx),	\
					       (NULL)),				\
	};									\
										\
	DEVICE_DT_INST_DEFINE(idx, mcux_pdm_init, NULL,			\
			      &mcux_pdm_data_##idx, &mcux_pdm_cfg_##idx,		\
			      POST_KERNEL, CONFIG_AUDIO_DMIC_INIT_PRIORITY,	\
			      &dmic_ops);

DT_INST_FOREACH_STATUS_OKAY(PDM_MCUX_DEVICE)
