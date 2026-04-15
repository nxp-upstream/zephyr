/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_SPDIF_MCUX_HAL_H_
#define ZEPHYR_DRIVERS_AUDIO_SPDIF_MCUX_HAL_H_

#include <fsl_common.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
	kStatus_SPDIF_RxDPLLLocked = MAKE_STATUS(kStatusGroup_SPDIF, 0),
	kStatus_SPDIF_TxFIFOError = MAKE_STATUS(kStatusGroup_SPDIF, 1),
	kStatus_SPDIF_TxFIFOResync = MAKE_STATUS(kStatusGroup_SPDIF, 2),
	kStatus_SPDIF_RxCnew = MAKE_STATUS(kStatusGroup_SPDIF, 3),
	kStatus_SPDIF_ValidatyNoGood = MAKE_STATUS(kStatusGroup_SPDIF, 4),
	kStatus_SPDIF_RxIllegalSymbol = MAKE_STATUS(kStatusGroup_SPDIF, 5),
	kStatus_SPDIF_RxParityBitError = MAKE_STATUS(kStatusGroup_SPDIF, 6),
	kStatus_SPDIF_UChannelOverrun = MAKE_STATUS(kStatusGroup_SPDIF, 7),
	kStatus_SPDIF_QChannelOverrun = MAKE_STATUS(kStatusGroup_SPDIF, 8),
	kStatus_SPDIF_UQChannelSync = MAKE_STATUS(kStatusGroup_SPDIF, 9),
	kStatus_SPDIF_UQChannelFrameError = MAKE_STATUS(kStatusGroup_SPDIF, 10),
	kStatus_SPDIF_RxFIFOError = MAKE_STATUS(kStatusGroup_SPDIF, 11),
	kStatus_SPDIF_RxFIFOResync = MAKE_STATUS(kStatusGroup_SPDIF, 12),
	kStatus_SPDIF_LockLoss = MAKE_STATUS(kStatusGroup_SPDIF, 13),
	kStatus_SPDIF_TxIdle = MAKE_STATUS(kStatusGroup_SPDIF, 14),
	kStatus_SPDIF_RxIdle = MAKE_STATUS(kStatusGroup_SPDIF, 15),
	kStatus_SPDIF_QueueFull = MAKE_STATUS(kStatusGroup_SPDIF, 16),
};

typedef enum {
	kSPDIF_RxFull1Sample = 0x0u,
	kSPDIF_RxFull4Samples,
	kSPDIF_RxFull8Samples,
	kSPDIF_RxFull16Samples,
} spdif_rxfull_select_t;

typedef enum {
	kSPDIF_TxEmpty0Sample = 0x0u,
	kSPDIF_TxEmpty4Samples,
	kSPDIF_TxEmpty8Samples,
	kSPDIF_TxEmpty12Samples,
} spdif_txempty_select_t;

typedef enum {
	kSPDIF_NoUChannel = 0x0U,
	kSPDIF_UChannelFromRx = 0x1U,
	kSPDIF_UChannelFromTx = 0x3U,
} spdif_uchannel_source_t;

typedef enum {
	kSPDIF_GAIN_24 = 0x0U,
	kSPDIF_GAIN_16,
	kSPDIF_GAIN_12,
	kSPDIF_GAIN_8,
	kSPDIF_GAIN_6,
	kSPDIF_GAIN_4,
	kSPDIF_GAIN_3,
} spdif_gain_select_t;

typedef enum {
	kSPDIF_txFromReceiver = 0x1U,
	kSPDIF_txNormal = 0x5U,
} spdif_tx_source_t;

typedef enum {
	kSPDIF_validityFlagAlwaysSet = 0x0U,
	kSPDIF_validityFlagAlwaysClear,
} spdif_validity_config_t;

enum {
	kSPDIF_RxDPLLLocked = SPDIF_SIE_LOCK_MASK,
	kSPDIF_TxFIFOError = SPDIF_SIE_TXUNOV_MASK,
	kSPDIF_TxFIFOResync = SPDIF_SIE_TXRESYN_MASK,
	kSPDIF_RxControlChannelChange = SPDIF_SIE_CNEW_MASK,
	kSPDIF_ValidityFlagNoGood = SPDIF_SIE_VALNOGOOD_MASK,
	kSPDIF_RxIllegalSymbol = SPDIF_SIE_SYMERR_MASK,
	kSPDIF_RxParityBitError = SPDIF_SIE_BITERR_MASK,
	kSPDIF_UChannelReceiveRegisterFull = SPDIF_SIE_URXFUL_MASK,
	kSPDIF_UChannelReceiveRegisterOverrun = SPDIF_SIE_URXOV_MASK,
	kSPDIF_QChannelReceiveRegisterFull = SPDIF_SIE_QRXFUL_MASK,
	kSPDIF_QChannelReceiveRegisterOverrun = SPDIF_SIE_QRXOV_MASK,
	kSPDIF_UQChannelSync = SPDIF_SIE_UQSYNC_MASK,
	kSPDIF_UQChannelFrameError = SPDIF_SIE_UQERR_MASK,
	kSPDIF_RxFIFOError = SPDIF_SIE_RXFIFOUNOV_MASK,
	kSPDIF_RxFIFOResync = SPDIF_SIE_RXFIFORESYN_MASK,
	kSPDIF_LockLoss = SPDIF_SIE_LOCKLOSS_MASK,
	kSPDIF_TxFIFOEmpty = SPDIF_SIE_TXEM_MASK,
	kSPDIF_RxFIFOFull = SPDIF_SIE_RXFIFOFUL_MASK,
	kSPDIF_AllInterrupt = SPDIF_SIE_RXFIFOFUL_MASK | SPDIF_SIE_TXEM_MASK |
		SPDIF_SIE_LOCKLOSS_MASK | SPDIF_SIE_RXFIFORESYN_MASK |
		SPDIF_SIE_RXFIFOUNOV_MASK | SPDIF_SIE_UQERR_MASK |
		SPDIF_SIE_UQSYNC_MASK | SPDIF_SIE_QRXOV_MASK |
		SPDIF_SIE_QRXFUL_MASK | SPDIF_SIE_URXOV_MASK |
		SPDIF_SIE_URXFUL_MASK | SPDIF_SIE_BITERR_MASK |
		SPDIF_SIE_SYMERR_MASK | SPDIF_SIE_VALNOGOOD_MASK |
		SPDIF_SIE_CNEW_MASK | SPDIF_SIE_TXRESYN_MASK |
		SPDIF_SIE_TXUNOV_MASK | SPDIF_SIE_LOCK_MASK,
};

enum {
	kSPDIF_RxDMAEnable = SPDIF_SCR_DMA_RX_EN_MASK,
	kSPDIF_TxDMAEnable = SPDIF_SCR_DMA_TX_EN_MASK,
};

typedef struct {
	bool isTxAutoSync;
	bool isRxAutoSync;
	uint8_t DPLLClkSource;
	uint8_t txClkSource;
	spdif_rxfull_select_t rxFullSelect;
	spdif_txempty_select_t txFullSelect;
	spdif_uchannel_source_t uChannelSrc;
	spdif_tx_source_t txSource;
	spdif_validity_config_t validityConfig;
	spdif_gain_select_t gain;
} spdif_config_t;

#define SPDIF_XFER_QUEUE_SIZE 4U

typedef struct {
	uint8_t *data;
	uint8_t *qdata;
	uint8_t *udata;
	size_t dataSize;
} spdif_transfer_t;

typedef struct _spdif_handle spdif_handle_t;

typedef void (*spdif_transfer_callback_t)(SPDIF_Type *base, spdif_handle_t *handle,
					  status_t status, void *userData);

struct _spdif_handle {
	uint32_t state;
	spdif_transfer_callback_t callback;
	void *userData;
	spdif_transfer_t spdifQueue[SPDIF_XFER_QUEUE_SIZE];
	size_t transferSize[SPDIF_XFER_QUEUE_SIZE];
	volatile uint8_t queueUser;
	volatile uint8_t queueDriver;
	uint8_t watermark;
};

static inline uint32_t SPDIF_GetStatusFlag(SPDIF_Type *base)
{
	return base->SIS;
}

static inline void SPDIF_ClearStatusFlags(SPDIF_Type *base, uint32_t mask)
{
	base->SIC = mask;
}

static inline void SPDIF_EnableInterrupts(SPDIF_Type *base, uint32_t mask)
{
	base->SIE |= mask;
}

static inline void SPDIF_DisableInterrupts(SPDIF_Type *base, uint32_t mask)
{
	base->SIE &= ~mask;
}

static inline void SPDIF_WriteLeftData(SPDIF_Type *base, uint32_t data)
{
	base->STL = data;
}

static inline void SPDIF_WriteRightData(SPDIF_Type *base, uint32_t data)
{
	base->STR = data;
}

static inline void SPDIF_WriteChannelStatusHigh(SPDIF_Type *base, uint32_t data)
{
	base->STCSCH = data;
}

static inline void SPDIF_WriteChannelStatusLow(SPDIF_Type *base, uint32_t data)
{
	base->STCSCL = data;
}

static inline uint32_t SPDIF_ReadLeftData(SPDIF_Type *base)
{
	return base->SRL;
}

static inline uint32_t SPDIF_ReadRightData(SPDIF_Type *base)
{
	return base->SRR;
}

static inline uint32_t SPDIF_ReadQChannel(SPDIF_Type *base)
{
	return base->SRQ;
}

static inline uint32_t SPDIF_ReadUChannel(SPDIF_Type *base)
{
	return base->SRU;
}

void SPDIF_GetDefaultConfig(spdif_config_t *config);
void SPDIF_Init(SPDIF_Type *base, const spdif_config_t *config);
void SPDIF_TxEnable(SPDIF_Type *base, bool enable);
void SPDIF_RxEnable(SPDIF_Type *base, bool enable);
void SPDIF_TxSetSampleRate(SPDIF_Type *base, uint32_t sampleRate_Hz, uint32_t sourceClockFreq_Hz);
uint32_t SPDIF_GetRxSampleRate(SPDIF_Type *base, uint32_t clockSourceFreq_Hz);
void SPDIF_TransferTxCreateHandle(SPDIF_Type *base, spdif_handle_t *handle,
					  spdif_transfer_callback_t callback, void *userData);
void SPDIF_TransferRxCreateHandle(SPDIF_Type *base, spdif_handle_t *handle,
					  spdif_transfer_callback_t callback, void *userData);
status_t SPDIF_TransferSendNonBlocking(SPDIF_Type *base, spdif_handle_t *handle,
					       spdif_transfer_t *xfer);
status_t SPDIF_TransferReceiveNonBlocking(SPDIF_Type *base, spdif_handle_t *handle,
						  spdif_transfer_t *xfer);
void SPDIF_TransferAbortSend(SPDIF_Type *base, spdif_handle_t *handle);
void SPDIF_TransferAbortReceive(SPDIF_Type *base, spdif_handle_t *handle);
void SPDIF_TransferTxHandleIRQ(SPDIF_Type *base, spdif_handle_t *handle);
void SPDIF_TransferRxHandleIRQ(SPDIF_Type *base, spdif_handle_t *handle);

#endif