/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_SPDIF_MCUX_HAL_H_
#define ZEPHYR_DRIVERS_AUDIO_SPDIF_MCUX_HAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int32_t status_t;

enum {
	kStatusGroup_Generic = 0,
	kStatusGroup_SPDIF = 75,
};

#define MAKE_STATUS(group, code) ((((group) * 100L) + (code)))

enum {
	kStatus_Success = MAKE_STATUS(kStatusGroup_Generic, 0),
	kStatus_InvalidArgument = MAKE_STATUS(kStatusGroup_Generic, 4),
	kStatus_NoTransferInProgress = MAKE_STATUS(kStatusGroup_Generic, 6),
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

typedef struct {
	volatile uint32_t SCR;
	volatile uint32_t SRCD;
	volatile uint32_t SRPC;
	volatile uint32_t SIE;
	union {
		volatile uint32_t SIC;
		volatile uint32_t SIS;
	};
	volatile const uint32_t SRL;
	volatile const uint32_t SRR;
	volatile const uint32_t SRCSH;
	volatile const uint32_t SRCSL;
	volatile const uint32_t SRU;
	volatile const uint32_t SRQ;
	volatile uint32_t STL;
	volatile uint32_t STR;
	volatile uint32_t STCSCH;
	volatile uint32_t STCSCL;
	uint8_t reserved_0[8];
	volatile const uint32_t SRFM;
	uint8_t reserved_1[8];
	volatile uint32_t STC;
} SPDIF_Type;

#define SPDIF_SCR_USRC_SEL_MASK 0x3U
#define SPDIF_SCR_USRC_SEL_SHIFT 0U
#define SPDIF_SCR_USRC_SEL(x) ((((uint32_t)(x)) << SPDIF_SCR_USRC_SEL_SHIFT) & SPDIF_SCR_USRC_SEL_MASK)
#define SPDIF_SCR_TXSEL_MASK 0x1CU
#define SPDIF_SCR_TXSEL_SHIFT 2U
#define SPDIF_SCR_TXSEL(x) ((((uint32_t)(x)) << SPDIF_SCR_TXSEL_SHIFT) & SPDIF_SCR_TXSEL_MASK)
#define SPDIF_SCR_VALCTRL_MASK 0x20U
#define SPDIF_SCR_VALCTRL_SHIFT 5U
#define SPDIF_SCR_VALCTRL(x) ((((uint32_t)(x)) << SPDIF_SCR_VALCTRL_SHIFT) & SPDIF_SCR_VALCTRL_MASK)
#define SPDIF_SCR_DMA_TX_EN_MASK 0x100U
#define SPDIF_SCR_DMA_RX_EN_MASK 0x200U
#define SPDIF_SCR_TXFIFO_CTRL_MASK 0xC00U
#define SPDIF_SCR_TXFIFO_CTRL_SHIFT 10U
#define SPDIF_SCR_TXFIFO_CTRL(x) ((((uint32_t)(x)) << SPDIF_SCR_TXFIFO_CTRL_SHIFT) & SPDIF_SCR_TXFIFO_CTRL_MASK)
#define SPDIF_SCR_SOFT_RESET_MASK 0x1000U
#define SPDIF_SCR_TXFIFOEMPTY_SEL_MASK 0x18000U
#define SPDIF_SCR_TXFIFOEMPTY_SEL_SHIFT 15U
#define SPDIF_SCR_TXFIFOEMPTY_SEL(x) ((((uint32_t)(x)) << SPDIF_SCR_TXFIFOEMPTY_SEL_SHIFT) & SPDIF_SCR_TXFIFOEMPTY_SEL_MASK)
#define SPDIF_SCR_TXAUTOSYNC_MASK 0x20000U
#define SPDIF_SCR_TXAUTOSYNC_SHIFT 17U
#define SPDIF_SCR_TXAUTOSYNC(x) ((((uint32_t)(x)) << SPDIF_SCR_TXAUTOSYNC_SHIFT) & SPDIF_SCR_TXAUTOSYNC_MASK)
#define SPDIF_SCR_RXAUTOSYNC_MASK 0x40000U
#define SPDIF_SCR_RXAUTOSYNC_SHIFT 18U
#define SPDIF_SCR_RXAUTOSYNC(x) ((((uint32_t)(x)) << SPDIF_SCR_RXAUTOSYNC_SHIFT) & SPDIF_SCR_RXAUTOSYNC_MASK)
#define SPDIF_SCR_RXFIFOFULL_SEL_MASK 0x180000U
#define SPDIF_SCR_RXFIFOFULL_SEL_SHIFT 19U
#define SPDIF_SCR_RXFIFOFULL_SEL(x) ((((uint32_t)(x)) << SPDIF_SCR_RXFIFOFULL_SEL_SHIFT) & SPDIF_SCR_RXFIFOFULL_SEL_MASK)
#define SPDIF_SCR_RXFIFO_RST_MASK 0x200000U
#define SPDIF_SCR_RXFIFO_OFF_ON_MASK 0x400000U
#define SPDIF_SCR_RXFIFO_CTRL_MASK 0x800000U

#define SPDIF_SRPC_GAINSEL_MASK 0x38U
#define SPDIF_SRPC_GAINSEL_SHIFT 3U
#define SPDIF_SRPC_GAINSEL(x) ((((uint32_t)(x)) << SPDIF_SRPC_GAINSEL_SHIFT) & SPDIF_SRPC_GAINSEL_MASK)
#define SPDIF_SRPC_LOCK_MASK 0x40U
#define SPDIF_SRPC_CLKSRC_SEL_MASK 0x780U
#define SPDIF_SRPC_CLKSRC_SEL_SHIFT 7U
#define SPDIF_SRPC_CLKSRC_SEL(x) ((((uint32_t)(x)) << SPDIF_SRPC_CLKSRC_SEL_SHIFT) & SPDIF_SRPC_CLKSRC_SEL_MASK)

#define SPDIF_SIE_RXFIFOFUL_MASK 0x1U
#define SPDIF_SIE_TXEM_MASK 0x2U
#define SPDIF_SIE_LOCKLOSS_MASK 0x4U
#define SPDIF_SIE_RXFIFORESYN_MASK 0x8U
#define SPDIF_SIE_RXFIFOUNOV_MASK 0x10U
#define SPDIF_SIE_UQERR_MASK 0x20U
#define SPDIF_SIE_UQSYNC_MASK 0x40U
#define SPDIF_SIE_QRXOV_MASK 0x80U
#define SPDIF_SIE_QRXFUL_MASK 0x100U
#define SPDIF_SIE_URXOV_MASK 0x200U
#define SPDIF_SIE_URXFUL_MASK 0x400U
#define SPDIF_SIE_BITERR_MASK 0x4000U
#define SPDIF_SIE_SYMERR_MASK 0x8000U
#define SPDIF_SIE_VALNOGOOD_MASK 0x10000U
#define SPDIF_SIE_CNEW_MASK 0x20000U
#define SPDIF_SIE_TXRESYN_MASK 0x40000U
#define SPDIF_SIE_TXUNOV_MASK 0x80000U
#define SPDIF_SIE_LOCK_MASK 0x100000U

#define SPDIF_STC_TXCLK_DF_MASK 0x7FU
#define SPDIF_STC_TXCLK_DF_SHIFT 0U
#define SPDIF_STC_TXCLK_DF(x) ((((uint32_t)(x)) << SPDIF_STC_TXCLK_DF_SHIFT) & SPDIF_STC_TXCLK_DF_MASK)
#define SPDIF_STC_TX_ALL_CLK_EN_MASK 0x80U
#define SPDIF_STC_TXCLK_SOURCE_MASK 0x700U
#define SPDIF_STC_TXCLK_SOURCE_SHIFT 8U
#define SPDIF_STC_TXCLK_SOURCE(x) ((((uint32_t)(x)) << SPDIF_STC_TXCLK_SOURCE_SHIFT) & SPDIF_STC_TXCLK_SOURCE_MASK)
#define SPDIF_STC_SYSCLK_DF_MASK 0xFF800U
#define SPDIF_STC_SYSCLK_DF_SHIFT 11U
#define SPDIF_STC_SYSCLK_DF(x) ((((uint32_t)(x)) << SPDIF_STC_SYSCLK_DF_SHIFT) & SPDIF_STC_SYSCLK_DF_MASK)

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