/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "spdif_mcux_hal.h"

enum {
	kSPDIF_Busy = 0x0U,
	kSPDIF_Idle,
};

static const uint8_t spdif_gain_lut[] = {24U, 16U, 12U, 8U, 6U, 4U, 3U, 1U};
static const uint8_t spdif_tx_watermark_lut[] = {16U, 12U, 8U, 4U};
static const uint8_t spdif_rx_watermark_lut[] = {1U, 4U, 8U, 16U};

void SPDIF_GetDefaultConfig(spdif_config_t *config)
{
	memset(config, 0, sizeof(*config));
	config->isTxAutoSync = true;
	config->isRxAutoSync = true;
	config->DPLLClkSource = 1U;
	config->txClkSource = 1U;
	config->rxFullSelect = kSPDIF_RxFull8Samples;
	config->txFullSelect = kSPDIF_TxEmpty8Samples;
	config->uChannelSrc = kSPDIF_UChannelFromTx;
	config->txSource = kSPDIF_txNormal;
	config->validityConfig = kSPDIF_validityFlagAlwaysClear;
	config->gain = kSPDIF_GAIN_8;
}

void SPDIF_Init(SPDIF_Type *base, const spdif_config_t *config)
{
	uint32_t value;

	base->SCR |= SPDIF_SCR_SOFT_RESET_MASK;
	base->SCR &= ~SPDIF_SCR_SOFT_RESET_MASK;

	base->SCR = SPDIF_SCR_RXFIFOFULL_SEL(config->rxFullSelect) |
		SPDIF_SCR_RXAUTOSYNC(config->isRxAutoSync ? 1U : 0U) |
		SPDIF_SCR_TXAUTOSYNC(config->isTxAutoSync ? 1U : 0U) |
		SPDIF_SCR_TXFIFOEMPTY_SEL(config->txFullSelect) |
		SPDIF_SCR_TXFIFO_CTRL(1U) |
		SPDIF_SCR_VALCTRL(config->validityConfig) |
		SPDIF_SCR_TXSEL(config->txSource) |
		SPDIF_SCR_USRC_SEL(config->uChannelSrc);

	base->SRPC = SPDIF_SRPC_CLKSRC_SEL(config->DPLLClkSource) |
		SPDIF_SRPC_GAINSEL(config->gain);

	value = base->STC & ~SPDIF_STC_TXCLK_SOURCE_MASK;
	value |= SPDIF_STC_TXCLK_SOURCE(config->txClkSource);
	base->STC = value;

	base->SIC = (uint32_t)kSPDIF_AllInterrupt;
	base->SIE &= ~(uint32_t)kSPDIF_AllInterrupt;
}

void SPDIF_TxEnable(SPDIF_Type *base, bool enable)
{
	uint32_t value;

	if (enable) {
		value = base->SCR & ~SPDIF_SCR_TXFIFO_CTRL_MASK;
		value |= SPDIF_SCR_TXFIFO_CTRL(1U);
		base->SCR = value;
		base->STC |= SPDIF_STC_TX_ALL_CLK_EN_MASK;
	} else {
		base->SCR &= ~(SPDIF_SCR_TXFIFO_CTRL_MASK | SPDIF_SCR_TXSEL_MASK);
		base->STC &= ~SPDIF_STC_TX_ALL_CLK_EN_MASK;
	}
}

void SPDIF_RxEnable(SPDIF_Type *base, bool enable)
{
	if (enable) {
		base->SCR &= ~(SPDIF_SCR_RXFIFO_CTRL_MASK | SPDIF_SCR_RXFIFO_OFF_ON_MASK);
	} else {
		base->SCR |= SPDIF_SCR_RXFIFO_OFF_ON_MASK;
	}
}

void SPDIF_TxSetSampleRate(SPDIF_Type *base, uint32_t sampleRate_Hz, uint32_t sourceClockFreq_Hz)
{
	uint32_t divisor = sampleRate_Hz * 64U;
	uint32_t clock_div = sourceClockFreq_Hz / divisor;
	uint32_t mod = sourceClockFreq_Hz % divisor;
	uint32_t value;
	uint8_t clock_source = (uint8_t)((base->STC & SPDIF_STC_TXCLK_SOURCE_MASK) >>
		SPDIF_STC_TXCLK_SOURCE_SHIFT);

	assert(sampleRate_Hz > 0U);
	assert(divisor > 0U);

	if (mod > (divisor / 2U)) {
		clock_div += 1U;
	}

	value = base->STC & ~(SPDIF_STC_TXCLK_DF_MASK | SPDIF_STC_SYSCLK_DF_MASK);
	if ((clock_source == 5U) && (clock_div > 256U)) {
		value |= SPDIF_STC_SYSCLK_DF((clock_div / 128U) - 1U) |
			SPDIF_STC_TXCLK_DF(127U);
	} else if (clock_source == 5U) {
		value |= SPDIF_STC_SYSCLK_DF(1U) | SPDIF_STC_TXCLK_DF(clock_div - 1U);
	} else {
		value |= SPDIF_STC_TXCLK_DF(clock_div - 1U);
	}

	base->STC = value;
}

uint32_t SPDIF_GetRxSampleRate(SPDIF_Type *base, uint32_t clockSourceFreq_Hz)
{
	uint64_t gain = spdif_gain_lut[(base->SRPC & SPDIF_SRPC_GAINSEL_MASK) >>
		SPDIF_SRPC_GAINSEL_SHIFT];
	uint64_t temp = (uint64_t)base->SRFM * (uint64_t)clockSourceFreq_Hz;

	temp /= 1024U * 1024U * 128U * gain;
	return (uint32_t)temp;
}

void SPDIF_TransferTxCreateHandle(SPDIF_Type *base, spdif_handle_t *handle,
					  spdif_transfer_callback_t callback, void *userData)
{
	(void)base;
	memset(handle, 0, sizeof(*handle));
	handle->callback = callback;
	handle->userData = userData;
	handle->watermark = spdif_tx_watermark_lut[(base->SCR & SPDIF_SCR_TXFIFOEMPTY_SEL_MASK) >>
		SPDIF_SCR_TXFIFOEMPTY_SEL_SHIFT];
}

void SPDIF_TransferRxCreateHandle(SPDIF_Type *base, spdif_handle_t *handle,
					  spdif_transfer_callback_t callback, void *userData)
{
	(void)base;
	memset(handle, 0, sizeof(*handle));
	handle->callback = callback;
	handle->userData = userData;
	handle->watermark = spdif_rx_watermark_lut[(base->SCR & SPDIF_SCR_RXFIFOFULL_SEL_MASK) >>
		SPDIF_SCR_RXFIFOFULL_SEL_SHIFT];
}

status_t SPDIF_TransferSendNonBlocking(SPDIF_Type *base, spdif_handle_t *handle,
					       spdif_transfer_t *xfer)
{
	if (handle->spdifQueue[handle->queueUser].data != NULL) {
		return kStatus_SPDIF_QueueFull;
	}

	handle->transferSize[handle->queueUser] = xfer->dataSize;
	handle->spdifQueue[handle->queueUser] = *xfer;
	handle->queueUser = (handle->queueUser + 1U) % SPDIF_XFER_QUEUE_SIZE;
	handle->state = kSPDIF_Busy;
	SPDIF_EnableInterrupts(base, kSPDIF_TxFIFOEmpty);
	SPDIF_TxEnable(base, true);

	return kStatus_Success;
}

status_t SPDIF_TransferReceiveNonBlocking(SPDIF_Type *base, spdif_handle_t *handle,
						  spdif_transfer_t *xfer)
{
	uint32_t enable_interrupts = (uint32_t)kSPDIF_RxFIFOFull |
		(uint32_t)kSPDIF_RxControlChannelChange;

	if (handle->spdifQueue[handle->queueUser].data != NULL) {
		return kStatus_SPDIF_QueueFull;
	}

	handle->transferSize[handle->queueUser] = xfer->dataSize;
	handle->spdifQueue[handle->queueUser] = *xfer;
	handle->queueUser = (handle->queueUser + 1U) % SPDIF_XFER_QUEUE_SIZE;
	handle->state = kSPDIF_Busy;

	if (xfer->qdata != NULL) {
		enable_interrupts |= (uint32_t)kSPDIF_QChannelReceiveRegisterFull;
	}

	if (xfer->udata != NULL) {
		enable_interrupts |= (uint32_t)kSPDIF_UChannelReceiveRegisterFull;
	}

	SPDIF_EnableInterrupts(base, enable_interrupts);
	SPDIF_RxEnable(base, true);

	return kStatus_Success;
}

void SPDIF_TransferAbortSend(SPDIF_Type *base, spdif_handle_t *handle)
{
	SPDIF_DisableInterrupts(base, kSPDIF_TxFIFOEmpty);
	handle->state = kSPDIF_Idle;
	memset(handle->spdifQueue, 0, sizeof(handle->spdifQueue));
	handle->queueDriver = 0U;
	handle->queueUser = 0U;
}

void SPDIF_TransferAbortReceive(SPDIF_Type *base, spdif_handle_t *handle)
{
	SPDIF_DisableInterrupts(base, (uint32_t)kSPDIF_UChannelReceiveRegisterFull |
		(uint32_t)kSPDIF_QChannelReceiveRegisterFull |
		(uint32_t)kSPDIF_RxFIFOFull |
		(uint32_t)kSPDIF_RxControlChannelChange);
	handle->state = kSPDIF_Idle;
	memset(handle->spdifQueue, 0, sizeof(handle->spdifQueue));
	handle->queueDriver = 0U;
	handle->queueUser = 0U;
}

void SPDIF_TransferTxHandleIRQ(SPDIF_Type *base, spdif_handle_t *handle)
{
	uint8_t *buffer = handle->spdifQueue[handle->queueDriver].data;
	uint8_t data_size = 0U;
	uint32_t data = 0U;

	if (((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_TxFIFOEmpty) == 0U) ||
	    ((base->SIE & (uint32_t)kSPDIF_TxFIFOEmpty) == 0U)) {
		return;
	}

	data_size = handle->watermark;
	for (uint32_t index = 0; index < data_size; index++) {
		data = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8U) |
			((uint32_t)buffer[2] << 16U);
		SPDIF_WriteLeftData(base, data);
		buffer += 3;

		data = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8U) |
			((uint32_t)buffer[2] << 16U);
		SPDIF_WriteRightData(base, data);
		buffer += 3;
	}

	handle->spdifQueue[handle->queueDriver].dataSize -= (uint32_t)data_size * 6U;
	handle->spdifQueue[handle->queueDriver].data += data_size * 6U;

	if (handle->spdifQueue[handle->queueDriver].dataSize == 0U) {
		memset(&handle->spdifQueue[handle->queueDriver], 0,
		       sizeof(spdif_transfer_t));
		handle->queueDriver = (handle->queueDriver + 1U) % SPDIF_XFER_QUEUE_SIZE;
		if (handle->callback != NULL) {
			handle->callback(base, handle, kStatus_SPDIF_TxIdle, handle->userData);
		}
	}

	if (handle->spdifQueue[handle->queueDriver].data == NULL) {
		SPDIF_TransferAbortSend(base, handle);
	}
}

void SPDIF_TransferRxHandleIRQ(SPDIF_Type *base, spdif_handle_t *handle)
{
	uint8_t *buffer;
	uint8_t data_size = 0U;
	uint32_t data;

	if ((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_RxControlChannelChange) != 0U) {
		SPDIF_ClearStatusFlags(base, SPDIF_SIE_CNEW_MASK);
		if (handle->callback != NULL) {
			handle->callback(base, handle, kStatus_SPDIF_RxCnew, handle->userData);
		}
	}

	if ((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_RxIllegalSymbol) != 0U) {
		SPDIF_ClearStatusFlags(base, kSPDIF_RxIllegalSymbol);
		if (handle->callback != NULL) {
			handle->callback(base, handle, kStatus_SPDIF_RxIllegalSymbol, handle->userData);
		}
	}

	if ((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_RxParityBitError) != 0U) {
		SPDIF_ClearStatusFlags(base, kSPDIF_RxParityBitError);
		if (handle->callback != NULL) {
			handle->callback(base, handle, kStatus_SPDIF_RxParityBitError, handle->userData);
		}
	}

	if ((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_RxDPLLLocked) != 0U) {
		SPDIF_ClearStatusFlags(base, kSPDIF_RxDPLLLocked);
		if (handle->callback != NULL) {
			handle->callback(base, handle, kStatus_SPDIF_RxDPLLLocked, handle->userData);
		}
	}

	if (((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_QChannelReceiveRegisterFull) != 0U) &&
	    ((base->SIE & (uint32_t)kSPDIF_QChannelReceiveRegisterFull) != 0U)) {
		buffer = handle->spdifQueue[handle->queueDriver].qdata;
		if (buffer != NULL) {
			data = SPDIF_ReadQChannel(base);
			buffer[0] = (uint8_t)(data & 0xFFU);
			buffer[1] = (uint8_t)((data >> 8U) & 0xFFU);
			buffer[2] = (uint8_t)((data >> 16U) & 0xFFU);
		}
	}

	if (((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_UChannelReceiveRegisterFull) != 0U) &&
	    ((base->SIE & (uint32_t)kSPDIF_UChannelReceiveRegisterFull) != 0U)) {
		buffer = handle->spdifQueue[handle->queueDriver].udata;
		if (buffer != NULL) {
			data = SPDIF_ReadUChannel(base);
			buffer[0] = (uint8_t)(data & 0xFFU);
			buffer[1] = (uint8_t)((data >> 8U) & 0xFFU);
			buffer[2] = (uint8_t)((data >> 16U) & 0xFFU);
		}
	}

	if (((SPDIF_GetStatusFlag(base) & (uint32_t)kSPDIF_RxFIFOFull) == 0U) ||
	    ((base->SIE & (uint32_t)kSPDIF_RxFIFOFull) == 0U)) {
		return;
	}

	data_size = handle->watermark;
	buffer = handle->spdifQueue[handle->queueDriver].data;
	for (uint32_t index = 0; index < data_size; index++) {
		data = SPDIF_ReadLeftData(base);
		buffer[0] = (uint8_t)(data & 0xFFU);
		buffer[1] = (uint8_t)((data >> 8U) & 0xFFU);
		buffer[2] = (uint8_t)((data >> 16U) & 0xFFU);
		buffer += 3;

		data = SPDIF_ReadRightData(base);
		buffer[0] = (uint8_t)(data & 0xFFU);
		buffer[1] = (uint8_t)((data >> 8U) & 0xFFU);
		buffer[2] = (uint8_t)((data >> 16U) & 0xFFU);
		buffer += 3;
	}

	handle->spdifQueue[handle->queueDriver].dataSize -= (uint32_t)data_size * 6U;
	handle->spdifQueue[handle->queueDriver].data += data_size * 6U;

	if (handle->spdifQueue[handle->queueDriver].dataSize == 0U) {
		memset(&handle->spdifQueue[handle->queueDriver], 0,
		       sizeof(spdif_transfer_t));
		handle->queueDriver = (handle->queueDriver + 1U) % SPDIF_XFER_QUEUE_SIZE;
		if (handle->callback != NULL) {
			handle->callback(base, handle, kStatus_SPDIF_RxIdle, handle->userData);
		}
	}

	if (handle->spdifQueue[handle->queueDriver].data == NULL) {
		SPDIF_TransferAbortReceive(base, handle);
	}
}