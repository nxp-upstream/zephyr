/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_mcux_spdif

#include <errno.h>
#include <string.h>

#include <zephyr/audio/spdif.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <soc.h>

#include "spdif_mcux_hal.h"

LOG_MODULE_REGISTER(spdif_mcux, CONFIG_AUDIO_SPDIF_LOG_LEVEL);

struct spdif_q_entry {
	void *mem_block;
	size_t size;
};

struct spdif_stream {
	enum spdif_state state;
	struct spdif_config cfg;
	struct k_msgq in_queue;
	struct k_msgq out_queue;
	bool last_block;
	uint8_t queued;
	spdif_handle_t handle;
};

struct spdif_mcux_config {
	SPDIF_Type *base;
	const struct device *tx_dma_dev;
	const struct device *rx_dma_dev;
	uint32_t tx_dma_channel;
	uint32_t rx_dma_channel;
	uint32_t tx_dma_slot;
	uint32_t rx_dma_slot;
	uint32_t clk_src;
	uint32_t clk_pre_div;
	uint32_t clk_src_div;
	uint32_t pll_src;
	uint32_t pll_lp;
	uint32_t pll_pd;
	uint32_t pll_num;
	uint32_t pll_den;
	clock_control_subsys_t clk_subsys;
	const struct device *clock_dev;
	const struct pinctrl_dev_config *pinctrl;
	void (*irq_config_func)(const struct device *dev);
};

struct spdif_mcux_data {
	struct spdif_stream tx;
	struct spdif_q_entry tx_in_msgs[CONFIG_AUDIO_SPDIF_TX_BLOCK_COUNT];
	struct spdif_q_entry tx_out_msgs[CONFIG_AUDIO_SPDIF_TX_BLOCK_COUNT];
	struct spdif_stream rx;
	struct spdif_q_entry rx_in_msgs[CONFIG_AUDIO_SPDIF_RX_BLOCK_COUNT];
	struct spdif_q_entry rx_out_msgs[CONFIG_AUDIO_SPDIF_RX_BLOCK_COUNT];
	spdif_status_flags_t latched_status;
	uint32_t rx_channel_status_low;
	uint32_t rx_channel_status_high;
	uint32_t tx_channel_status_low;
	uint32_t tx_channel_status_high;
	uint32_t rx_u_bits;
	uint32_t rx_q_bits;
};

static inline const struct spdif_mcux_config *dev_cfg(const struct device *dev)
{
	return dev->config;
}

static inline struct spdif_mcux_data *dev_data(const struct device *dev)
{
	return dev->data;
}

static void spdif_purge_msgq(struct k_msgq *msgq, struct k_mem_slab *mem_slab)
{
	struct spdif_q_entry q_entry;

	while (k_msgq_get(msgq, &q_entry, K_NO_WAIT) == 0) {
		k_mem_slab_free(mem_slab, q_entry.mem_block);
	}
}

static int spdif_get_clock_rate(const struct device *dev, uint32_t *rate)
{
	const struct spdif_mcux_config *cfg = dev_cfg(dev);

	return clock_control_get_rate(cfg->clock_dev, cfg->clk_subsys, rate);
}

static void spdif_clock_settings(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void spdif_apply_hw_config(const struct device *dev,
				  const struct spdif_config *cfg)
{
	const struct spdif_mcux_config *config = dev_cfg(dev);
	SPDIF_Type *base = config->base;
	spdif_config_t hw_cfg;
	uint32_t source_clock_hz;

	SPDIF_GetDefaultConfig(&hw_cfg);
	hw_cfg.isTxAutoSync = (cfg->options & SPDIF_OPT_TX_AUTOSYNC) != 0U;
	hw_cfg.isRxAutoSync = (cfg->options & SPDIF_OPT_RX_AUTOSYNC) != 0U;
	hw_cfg.DPLLClkSource = cfg->dpll_clk_source;
	hw_cfg.txClkSource = cfg->tx_clk_source;
	hw_cfg.uChannelSrc = (cfg->options & SPDIF_OPT_TX_U_FROM_RX) ?
		kSPDIF_UChannelFromRx :
		((cfg->options & SPDIF_OPT_TX_U_FROM_TX) ?
		 kSPDIF_UChannelFromTx : kSPDIF_NoUChannel);
	hw_cfg.txSource = (cfg->options & SPDIF_OPT_TX_FROM_RX) ?
		kSPDIF_txFromReceiver : kSPDIF_txNormal;
	hw_cfg.validityConfig = (cfg->options & SPDIF_OPT_VALIDITY_ALWAYS_SET) ?
		kSPDIF_validityFlagAlwaysSet : kSPDIF_validityFlagAlwaysClear;

	switch (cfg->rx_watermark) {
	case SPDIF_RX_WATERMARK_1:
		hw_cfg.rxFullSelect = kSPDIF_RxFull1Sample;
		break;
	case SPDIF_RX_WATERMARK_4:
		hw_cfg.rxFullSelect = kSPDIF_RxFull4Samples;
		break;
	case SPDIF_RX_WATERMARK_8:
		hw_cfg.rxFullSelect = kSPDIF_RxFull8Samples;
		break;
	default:
		hw_cfg.rxFullSelect = kSPDIF_RxFull16Samples;
		break;
	}

	switch (cfg->tx_watermark) {
	case SPDIF_TX_WATERMARK_0:
		hw_cfg.txFullSelect = kSPDIF_TxEmpty0Sample;
		break;
	case SPDIF_TX_WATERMARK_4:
		hw_cfg.txFullSelect = kSPDIF_TxEmpty4Samples;
		break;
	case SPDIF_TX_WATERMARK_8:
		hw_cfg.txFullSelect = kSPDIF_TxEmpty8Samples;
		break;
	default:
		hw_cfg.txFullSelect = kSPDIF_TxEmpty12Samples;
		break;
	}

	switch (cfg->gain) {
	case SPDIF_GAIN_24:
		hw_cfg.gain = kSPDIF_GAIN_24;
		break;
	case SPDIF_GAIN_16:
		hw_cfg.gain = kSPDIF_GAIN_16;
		break;
	case SPDIF_GAIN_12:
		hw_cfg.gain = kSPDIF_GAIN_12;
		break;
	case SPDIF_GAIN_6:
		hw_cfg.gain = kSPDIF_GAIN_6;
		break;
	case SPDIF_GAIN_4:
		hw_cfg.gain = kSPDIF_GAIN_4;
		break;
	case SPDIF_GAIN_3:
		hw_cfg.gain = kSPDIF_GAIN_3;
		break;
	default:
		hw_cfg.gain = kSPDIF_GAIN_8;
		break;
	}

	SPDIF_Init(base, &hw_cfg);

	if (spdif_get_clock_rate(dev, &source_clock_hz) == 0 && cfg->sample_rate != 0U) {
		SPDIF_TxSetSampleRate(base, cfg->sample_rate, source_clock_hz);
	}
}

static int spdif_validate_config(const struct spdif_config *cfg)
{
	if ((cfg == NULL) || (cfg->mem_slab == NULL) || (cfg->block_size == 0U)) {
		return -EINVAL;
	}

	if (cfg->format != SPDIF_FRAME_FORMAT_PCM_24_STEREO) {
		return -ENOTSUP;
	}

	if ((cfg->block_size % SPDIF_FRAME_SIZE_BYTES) != 0U) {
		return -EINVAL;
	}

	return 0;
}

static int spdif_configure_stream(struct spdif_stream *stream,
				 const struct spdif_config *cfg)
{
	if ((stream->state == SPDIF_STATE_RUNNING) ||
	    (stream->state == SPDIF_STATE_STOPPING)) {
		return -EBUSY;
	}

	memcpy(&stream->cfg, cfg, sizeof(*cfg));
	stream->state = SPDIF_STATE_READY;
	stream->last_block = false;
	return 0;
}

static int spdif_mcux_configure(const struct device *dev, enum spdif_dir dir,
				const struct spdif_config *cfg)
{
	struct spdif_mcux_data *data = dev_data(dev);
	int ret;

	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->sample_rate == 0U) {
		if (dir == SPDIF_DIR_TX || dir == SPDIF_DIR_BOTH) {
			data->tx.state = SPDIF_STATE_NOT_READY;
		}

		if (dir == SPDIF_DIR_RX || dir == SPDIF_DIR_BOTH) {
			data->rx.state = SPDIF_STATE_NOT_READY;
		}

		return 0;
	}

	ret = spdif_validate_config(cfg);
	if (ret < 0) {
		return ret;
	}

	spdif_apply_hw_config(dev, cfg);

	if (dir == SPDIF_DIR_TX || dir == SPDIF_DIR_BOTH) {
		ret = spdif_configure_stream(&data->tx, cfg);
		if (ret < 0) {
			return ret;
		}
	}

	if (dir == SPDIF_DIR_RX || dir == SPDIF_DIR_BOTH) {
		ret = spdif_configure_stream(&data->rx, cfg);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static const struct spdif_config *spdif_mcux_config_get(const struct device *dev,
						 enum spdif_dir dir)
{
	struct spdif_mcux_data *data = dev_data(dev);

	if (dir == SPDIF_DIR_TX) {
		return (data->tx.state == SPDIF_STATE_NOT_READY) ? NULL : &data->tx.cfg;
	}

	if (dir == SPDIF_DIR_RX) {
		return (data->rx.state == SPDIF_STATE_NOT_READY) ? NULL : &data->rx.cfg;
	}

	return NULL;
}

static void spdif_tx_disable(const struct device *dev, bool drop)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->tx;
	SPDIF_Type *base = dev_cfg(dev)->base;

	SPDIF_TransferAbortSend(base, &stream->handle);
	SPDIF_TxEnable(base, false);
	stream->queued = 0U;

	if (stream->cfg.mem_slab != NULL) {
		if (drop) {
			spdif_purge_msgq(&stream->in_queue, stream->cfg.mem_slab);
			spdif_purge_msgq(&stream->out_queue, stream->cfg.mem_slab);
		}
	}
}

static void spdif_rx_disable(const struct device *dev, bool drop_in, bool drop_out)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->rx;
	SPDIF_Type *base = dev_cfg(dev)->base;

	SPDIF_TransferAbortReceive(base, &stream->handle);
	SPDIF_RxEnable(base, false);
	stream->queued = 0U;

	if (stream->cfg.mem_slab != NULL) {
		if (drop_in) {
			spdif_purge_msgq(&stream->in_queue, stream->cfg.mem_slab);
		}
		if (drop_out) {
			spdif_purge_msgq(&stream->out_queue, stream->cfg.mem_slab);
		}
	}
}

static int spdif_tx_submit_available(const struct device *dev)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->tx;
	struct spdif_q_entry q_entry;
	spdif_transfer_t xfer;
	status_t status;
	int ret;

	while (stream->queued < SPDIF_XFER_QUEUE_SIZE) {
		ret = k_msgq_get(&stream->in_queue, &q_entry, K_NO_WAIT);
		if (ret != 0) {
			return 0;
		}

		xfer.data = q_entry.mem_block;
		xfer.qdata = NULL;
		xfer.udata = NULL;
		xfer.dataSize = q_entry.size;

		status = SPDIF_TransferSendNonBlocking(dev_cfg(dev)->base,
						      &stream->handle, &xfer);
		if (status != kStatus_Success) {
			(void)k_msgq_put(&stream->in_queue, &q_entry, K_NO_WAIT);
			return (status == kStatus_SPDIF_QueueFull) ? 0 : -EIO;
		}

		ret = k_msgq_put(&stream->out_queue, &q_entry, K_NO_WAIT);
		if (ret != 0) {
			return ret;
		}

		stream->queued++;
	}

	return 0;
}

static int spdif_rx_submit_buffer(const struct device *dev)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->rx;
	struct spdif_q_entry q_entry;
	spdif_transfer_t xfer = {0};
	status_t status;
	int ret;

	ret = k_mem_slab_alloc(stream->cfg.mem_slab, &q_entry.mem_block, K_NO_WAIT);
	if (ret != 0) {
		return ret;
	}

	q_entry.size = stream->cfg.block_size;
	xfer.data = q_entry.mem_block;
	xfer.dataSize = q_entry.size;

	status = SPDIF_TransferReceiveNonBlocking(dev_cfg(dev)->base,
						 &stream->handle, &xfer);
	if (status != kStatus_Success) {
		k_mem_slab_free(stream->cfg.mem_slab, q_entry.mem_block);
		return (status == kStatus_SPDIF_QueueFull) ? 0 : -EIO;
	}

	ret = k_msgq_put(&stream->in_queue, &q_entry, K_NO_WAIT);
	if (ret != 0) {
		k_mem_slab_free(stream->cfg.mem_slab, q_entry.mem_block);
		return ret;
	}

	stream->queued++;
	return 0;
}

static int spdif_rx_prime(const struct device *dev)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->rx;
	int ret = 0;

	while ((stream->queued < SPDIF_XFER_QUEUE_SIZE) &&
	       (stream->queued < CONFIG_AUDIO_SPDIF_RX_BLOCK_COUNT)) {
		ret = spdif_rx_submit_buffer(dev);
		if (ret != 0) {
			break;
		}
	}

	return (stream->queued == 0U) ? -ENOMEM : 0;
}

static void spdif_tx_callback(SPDIF_Type *base, spdif_handle_t *handle,
			      status_t status, void *user_data)
{
	const struct device *dev = user_data;
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->tx;
	struct spdif_q_entry q_entry;
	int ret;

	ARG_UNUSED(base);
	ARG_UNUSED(handle);

	if (status != kStatus_SPDIF_TxIdle) {
		data->latched_status |= SPDIF_STATUS_TX_FIFO_ERROR;
		stream->state = SPDIF_STATE_ERROR;
		spdif_tx_disable(dev, true);
		return;
	}

	ret = k_msgq_get(&stream->out_queue, &q_entry, K_NO_WAIT);
	if (ret == 0) {
		k_mem_slab_free(stream->cfg.mem_slab, q_entry.mem_block);
	}

	if (stream->queued > 0U) {
		stream->queued--;
	}

	ret = spdif_tx_submit_available(dev);
	if (ret != 0) {
		stream->state = SPDIF_STATE_ERROR;
		spdif_tx_disable(dev, true);
		return;
	}

	if (stream->state == SPDIF_STATE_STOPPING) {
		if ((stream->last_block || stream->queued == 0U) &&
		    k_msgq_num_used_get(&stream->in_queue) == 0U) {
			spdif_tx_disable(dev, false);
			stream->state = SPDIF_STATE_READY;
		}
		return;
	}

	if ((stream->queued == 0U) && (k_msgq_num_used_get(&stream->in_queue) == 0U)) {
		spdif_tx_disable(dev, false);
		stream->state = SPDIF_STATE_READY;
	}
}

static void spdif_rx_callback(SPDIF_Type *base, spdif_handle_t *handle,
			      status_t status, void *user_data)
{
	const struct device *dev = user_data;
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->rx;
	struct spdif_q_entry q_entry;
	int ret;

	ARG_UNUSED(base);
	ARG_UNUSED(handle);

	switch (status) {
	case kStatus_SPDIF_RxCnew:
		data->latched_status |= SPDIF_STATUS_RX_CNEW;
		data->rx_channel_status_low = dev_cfg(dev)->base->SRCSL;
		data->rx_channel_status_high = dev_cfg(dev)->base->SRCSH;
		return;
	case kStatus_SPDIF_RxIllegalSymbol:
		data->latched_status |= SPDIF_STATUS_RX_ILLEGAL_SYMBOL;
		return;
	case kStatus_SPDIF_RxParityBitError:
		data->latched_status |= SPDIF_STATUS_RX_PARITY_ERROR;
		return;
	case kStatus_SPDIF_RxDPLLLocked:
		data->latched_status |= SPDIF_STATUS_DPLL_LOCKED;
		return;
	case kStatus_SPDIF_RxIdle:
		break;
	default:
		data->latched_status |= SPDIF_STATUS_RX_FIFO_ERROR;
		stream->state = SPDIF_STATE_ERROR;
		spdif_rx_disable(dev, true, true);
		return;
	}

	ret = k_msgq_get(&stream->in_queue, &q_entry, K_NO_WAIT);
	if (ret != 0) {
		stream->state = SPDIF_STATE_ERROR;
		spdif_rx_disable(dev, true, true);
		return;
	}

	if (stream->queued > 0U) {
		stream->queued--;
	}

	ret = k_msgq_put(&stream->out_queue, &q_entry, K_NO_WAIT);
	if (ret != 0) {
		k_mem_slab_free(stream->cfg.mem_slab, q_entry.mem_block);
		stream->state = SPDIF_STATE_ERROR;
		spdif_rx_disable(dev, true, true);
		return;
	}

	if (stream->state == SPDIF_STATE_STOPPING) {
		if (stream->queued == 0U) {
			spdif_rx_disable(dev, false, false);
			stream->state = SPDIF_STATE_READY;
		}
		return;
	}

	ret = spdif_rx_submit_buffer(dev);
	if (ret != 0) {
		stream->state = SPDIF_STATE_ERROR;
		spdif_rx_disable(dev, true, false);
	}
}

static int spdif_tx_start(const struct device *dev)
{
	struct spdif_mcux_data *data = dev_data(dev);

	if (k_msgq_num_used_get(&data->tx.in_queue) == 0U) {
		return -EIO;
	}

	data->tx.queued = 0U;
	return spdif_tx_submit_available(dev);
}

static int spdif_rx_start(const struct device *dev)
{
	struct spdif_mcux_data *data = dev_data(dev);

	data->rx.queued = 0U;
	return spdif_rx_prime(dev);
}

static int spdif_mcux_trigger(const struct device *dev, enum spdif_dir dir,
			      enum spdif_trigger_cmd cmd)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream;
	int ret = 0;
	unsigned int key = irq_lock();

	if (dir == SPDIF_DIR_BOTH) {
		irq_unlock(key);
		ret = spdif_mcux_trigger(dev, SPDIF_DIR_TX, cmd);
		if (ret != 0) {
			return ret;
		}
		return spdif_mcux_trigger(dev, SPDIF_DIR_RX, cmd);
	}

	stream = (dir == SPDIF_DIR_TX) ? &data->tx : &data->rx;

	switch (cmd) {
	case SPDIF_TRIGGER_START:
		if (stream->state != SPDIF_STATE_READY) {
			ret = -EIO;
			break;
		}

		ret = (dir == SPDIF_DIR_TX) ? spdif_tx_start(dev) : spdif_rx_start(dev);
		if (ret == 0) {
			stream->state = SPDIF_STATE_RUNNING;
			stream->last_block = false;
		}
		break;
	case SPDIF_TRIGGER_STOP:
		if (stream->state != SPDIF_STATE_RUNNING) {
			ret = -EIO;
			break;
		}
		stream->state = SPDIF_STATE_STOPPING;
		stream->last_block = true;
		break;
	case SPDIF_TRIGGER_DRAIN:
		if (stream->state != SPDIF_STATE_RUNNING) {
			ret = -EIO;
			break;
		}
		stream->state = SPDIF_STATE_STOPPING;
		break;
	case SPDIF_TRIGGER_DROP:
		if (stream->state == SPDIF_STATE_NOT_READY) {
			ret = -EIO;
			break;
		}
		if (dir == SPDIF_DIR_TX) {
			spdif_tx_disable(dev, true);
		} else {
			spdif_rx_disable(dev, true, true);
		}
		stream->state = SPDIF_STATE_READY;
		break;
	case SPDIF_TRIGGER_PREPARE:
		if (stream->state != SPDIF_STATE_ERROR) {
			ret = -EIO;
			break;
		}
		if (dir == SPDIF_DIR_TX) {
			spdif_tx_disable(dev, true);
		} else {
			spdif_rx_disable(dev, true, true);
		}
		stream->state = SPDIF_STATE_READY;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	irq_unlock(key);
	return ret;
}

static int spdif_mcux_read(const struct device *dev, void **mem_block, size_t *size)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->rx;
	struct spdif_q_entry q_entry;
	int ret;

	if (stream->state == SPDIF_STATE_NOT_READY) {
		return -EIO;
	}

	ret = k_msgq_get(&stream->out_queue, &q_entry, SYS_TIMEOUT_MS(stream->cfg.timeout));
	if (ret != 0) {
		return (stream->state == SPDIF_STATE_ERROR) ? -EIO : -EAGAIN;
	}

	*mem_block = q_entry.mem_block;
	*size = q_entry.size;
	return 0;
}

static int spdif_mcux_write(const struct device *dev, void *mem_block, size_t size)
{
	struct spdif_mcux_data *data = dev_data(dev);
	struct spdif_stream *stream = &data->tx;
	struct spdif_q_entry q_entry = {
		.mem_block = mem_block,
		.size = size,
	};
	int ret;

	if ((stream->state != SPDIF_STATE_READY) && (stream->state != SPDIF_STATE_RUNNING)) {
		return -EIO;
	}

	if ((size == 0U) || (size > stream->cfg.block_size) ||
	    ((size % SPDIF_FRAME_SIZE_BYTES) != 0U)) {
		return -EINVAL;
	}

	ret = k_msgq_put(&stream->in_queue, &q_entry, SYS_TIMEOUT_MS(stream->cfg.timeout));
	if (ret != 0) {
		return ret;
	}

	if (stream->state == SPDIF_STATE_RUNNING) {
		ret = spdif_tx_submit_available(dev);
	}

	return ret;
}

static int spdif_mcux_status_get(const struct device *dev, struct spdif_status *status)
{
	const struct spdif_mcux_config *cfg = dev_cfg(dev);
	struct spdif_mcux_data *data = dev_data(dev);
	uint32_t source_clock_hz = 0U;

	if (status == NULL) {
		return -EINVAL;
	}

	memset(status, 0, sizeof(*status));
	status->flags = data->latched_status;
	status->rx_channel_status_low = data->rx_channel_status_low;
	status->rx_channel_status_high = data->rx_channel_status_high;
	status->tx_channel_status_low = data->tx_channel_status_low;
	status->tx_channel_status_high = data->tx_channel_status_high;
	status->rx_u_bits = data->rx_u_bits;
	status->rx_q_bits = data->rx_q_bits;

	if ((cfg->base->SRPC & SPDIF_SRPC_LOCK_MASK) != 0U) {
		status->flags |= SPDIF_STATUS_DPLL_LOCKED;
		if (spdif_get_clock_rate(dev, &source_clock_hz) == 0) {
			status->rx_sample_rate = SPDIF_GetRxSampleRate(cfg->base, source_clock_hz);
		}
	}

	return 0;
}

static int spdif_mcux_channel_status_set(const struct device *dev, uint32_t low,
					 uint32_t high)
{
	const struct spdif_mcux_config *cfg = dev_cfg(dev);
	struct spdif_mcux_data *data = dev_data(dev);

	SPDIF_WriteChannelStatusLow(cfg->base, low & 0x00FFFFFFU);
	SPDIF_WriteChannelStatusHigh(cfg->base, high & 0x00FFFFFFU);
	data->tx_channel_status_low = low & 0x00FFFFFFU;
	data->tx_channel_status_high = high & 0x00FFFFFFU;

	return 0;
}

static void spdif_mcux_isr(const struct device *dev)
{
	const struct spdif_mcux_config *cfg = dev_cfg(dev);
	struct spdif_mcux_data *data = dev_data(dev);
	uint32_t flags = SPDIF_GetStatusFlag(cfg->base);

	if ((flags & kSPDIF_TxFIFOError) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_TxFIFOError);
		data->latched_status |= SPDIF_STATUS_TX_FIFO_ERROR;
		data->tx.state = SPDIF_STATE_ERROR;
		spdif_tx_disable(dev, true);
	}

	if ((flags & kSPDIF_TxFIFOResync) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_TxFIFOResync);
		data->latched_status |= SPDIF_STATUS_TX_FIFO_RESYNC;
	}

	if ((flags & kSPDIF_RxFIFOError) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_RxFIFOError);
		data->latched_status |= SPDIF_STATUS_RX_FIFO_ERROR;
		data->rx.state = SPDIF_STATE_ERROR;
		spdif_rx_disable(dev, true, false);
	}

	if ((flags & kSPDIF_RxFIFOResync) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_RxFIFOResync);
		data->latched_status |= SPDIF_STATUS_RX_FIFO_RESYNC;
	}

	if ((flags & kSPDIF_LockLoss) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_LockLoss);
		data->latched_status |= SPDIF_STATUS_DPLL_LOCK_LOSS;
	}

	if ((flags & kSPDIF_ValidityFlagNoGood) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_ValidityFlagNoGood);
		data->latched_status |= SPDIF_STATUS_VALIDITY_NOGOOD;
	}

	if ((flags & kSPDIF_UChannelReceiveRegisterOverrun) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_UChannelReceiveRegisterOverrun);
		data->latched_status |= SPDIF_STATUS_RX_U_OVERRUN;
	}

	if ((flags & kSPDIF_QChannelReceiveRegisterOverrun) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_QChannelReceiveRegisterOverrun);
		data->latched_status |= SPDIF_STATUS_RX_Q_OVERRUN;
	}

	if ((flags & kSPDIF_UQChannelSync) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_UQChannelSync);
		data->latched_status |= SPDIF_STATUS_RX_UQ_SYNC;
	}

	if ((flags & kSPDIF_UQChannelFrameError) != 0U) {
		SPDIF_ClearStatusFlags(cfg->base, kSPDIF_UQChannelFrameError);
		data->latched_status |= SPDIF_STATUS_RX_UQ_FRAME_ERROR;
	}

	if ((flags & kSPDIF_QChannelReceiveRegisterFull) != 0U) {
		data->rx_q_bits = SPDIF_ReadQChannel(cfg->base) & 0x00FFFFFFU;
	}

	if ((flags & kSPDIF_UChannelReceiveRegisterFull) != 0U) {
		data->rx_u_bits = SPDIF_ReadUChannel(cfg->base) & 0x00FFFFFFU;
	}

	if (data->tx.state != SPDIF_STATE_ERROR) {
		SPDIF_TransferTxHandleIRQ(cfg->base, &data->tx.handle);
	}

	if (data->rx.state != SPDIF_STATE_ERROR) {
		SPDIF_TransferRxHandleIRQ(cfg->base, &data->rx.handle);
	}
}

static int spdif_mcux_init(const struct device *dev)
{
	const struct spdif_mcux_config *cfg = dev_cfg(dev);
	struct spdif_mcux_data *data = dev_data(dev);
	int ret;

	if ((cfg->tx_dma_dev == NULL) || (cfg->rx_dma_dev == NULL)) {
		return -ENODEV;
	}

	if (!device_is_ready(cfg->clock_dev)) {
		return -ENODEV;
	}

	if (!device_is_ready(cfg->tx_dma_dev) || !device_is_ready(cfg->rx_dma_dev)) {
		return -ENODEV;
	}

	ret = pinctrl_apply_state(cfg->pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(cfg->clock_dev, cfg->clk_subsys);
	if (ret != 0) {
		return ret;
	}

	spdif_clock_settings(dev);

	k_msgq_init(&data->tx.in_queue, (char *)data->tx_in_msgs,
		    sizeof(struct spdif_q_entry), CONFIG_AUDIO_SPDIF_TX_BLOCK_COUNT);
	k_msgq_init(&data->tx.out_queue, (char *)data->tx_out_msgs,
		    sizeof(struct spdif_q_entry), CONFIG_AUDIO_SPDIF_TX_BLOCK_COUNT);
	k_msgq_init(&data->rx.in_queue, (char *)data->rx_in_msgs,
		    sizeof(struct spdif_q_entry), CONFIG_AUDIO_SPDIF_RX_BLOCK_COUNT);
	k_msgq_init(&data->rx.out_queue, (char *)data->rx_out_msgs,
		    sizeof(struct spdif_q_entry), CONFIG_AUDIO_SPDIF_RX_BLOCK_COUNT);

	data->tx.state = SPDIF_STATE_NOT_READY;
	data->rx.state = SPDIF_STATE_NOT_READY;

	cfg->irq_config_func(dev);
	SPDIF_TransferTxCreateHandle(cfg->base, &data->tx.handle, spdif_tx_callback,
				      (void *)dev);
	SPDIF_TransferRxCreateHandle(cfg->base, &data->rx.handle, spdif_rx_callback,
				      (void *)dev);
	spdif_mcux_channel_status_set(dev, 0U, 0U);

	return 0;
}

static DEVICE_API(spdif, spdif_mcux_driver_api) = {
	.configure = spdif_mcux_configure,
	.config_get = spdif_mcux_config_get,
	.read = spdif_mcux_read,
	.write = spdif_mcux_write,
	.trigger = spdif_mcux_trigger,
	.status_get = spdif_mcux_status_get,
	.channel_status_set = spdif_mcux_channel_status_set,
};

#define SPDIF_MCUX_INIT(inst)                                                                  \
	BUILD_ASSERT(DT_INST_DMAS_HAS_NAME(inst, tx),                                             \
			     "nxp,mcux-spdif requires a tx DMA specifier");                           \
	BUILD_ASSERT(DT_INST_DMAS_HAS_NAME(inst, rx),                                             \
			     "nxp,mcux-spdif requires an rx DMA specifier");                           \
	static void spdif_mcux_irq_config_func_##inst(const struct device *dev)                 \
	{                                                                                        \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),                      \
			    spdif_mcux_isr, DEVICE_DT_INST_GET(inst), 0);                           \
		irq_enable(DT_INST_IRQN(inst));                                                  \
	}                                                                                        \
	                                                                                         \
	PINCTRL_DT_INST_DEFINE(inst);                                                           \
	                                                                                         \
	static const struct spdif_mcux_config spdif_mcux_config_##inst = {                      \
		.base = (SPDIF_Type *)DT_INST_REG_ADDR(inst),                                    \
		.tx_dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, tx)),                \
		.rx_dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx)),                \
		.tx_dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, tx, mux),                      \
		.rx_dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, rx, mux),                      \
		.tx_dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, tx, source),                      \
		.rx_dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, rx, source),                      \
		.clk_src = DT_INST_PROP_OR(inst, clock_mux, 0),                                  \
		.clk_pre_div = DT_INST_PROP_OR(inst, pre_div, 0),                                \
		.clk_src_div = DT_INST_PROP_OR(inst, podf, 0),                                   \
		.pll_src = DT_PHA_BY_NAME_OR(DT_DRV_INST(inst), pll_clocks, src, value, 0),      \
		.pll_lp = DT_PHA_BY_NAME_OR(DT_DRV_INST(inst), pll_clocks, lp, value, 0),        \
		.pll_pd = DT_PHA_BY_NAME_OR(DT_DRV_INST(inst), pll_clocks, pd, value, 0),        \
		.pll_num = DT_PHA_BY_NAME_OR(DT_DRV_INST(inst), pll_clocks, num, value, 0),      \
		.pll_den = DT_PHA_BY_NAME_OR(DT_DRV_INST(inst), pll_clocks, den, value, 0),      \
		.clk_subsys =                                                                     \
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(inst, 0, name),          \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                           \
		.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                 \
		.irq_config_func = spdif_mcux_irq_config_func_##inst,                            \
	};                                                                                       \
	                                                                                         \
	static struct spdif_mcux_data spdif_mcux_data_##inst;                                  \
	                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, spdif_mcux_init, NULL, &spdif_mcux_data_##inst,            \
			      &spdif_mcux_config_##inst, POST_KERNEL,                      \
			      CONFIG_AUDIO_SPDIF_INIT_PRIORITY,                          \
			      &spdif_mcux_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SPDIF_MCUX_INIT)