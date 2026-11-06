/*
 * Copyright 2023-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for NXP ENET QOS Ethernet driver internal definitions.
 * @ingroup ethernet
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ETH_NXP_ENET_QOS_H__
#define ZEPHYR_INCLUDE_DRIVERS_ETH_NXP_ENET_QOS_H__

#include <fsl_device_registers.h>
#include <zephyr/drivers/clock_control.h>

/** @cond INTERNAL_HIDDEN */

/* Different platforms named the peripheral different in the register definitions */
#if defined(CONFIG_SOC_FAMILY_MCXN) || defined(CONFIG_SOC_FAMILY_MCXA)
#undef ENET
#define ENET_QOS_NAME ENET
#define ENET_QOS_ALIGNMENT 4
typedef ENET_Type enet_qos_t;
#define ENET_QOS_MAC_ADDR_HIGH(base) ((base)->MAC_ADDRESS0_HIGH)
#define ENET_QOS_MAC_ADDR_LOW(base)  ((base)->MAC_ADDRESS0_LOW)
#elif defined(CONFIG_SOC_SERIES_IMXRT11XX)
#undef ENET_QOS
#define ENET_QOS_NAME ENET_QOS
#define ENET_QOS_ALIGNMENT 4
typedef ENET_QOS_Type enet_qos_t;

/*
 * The RT11xx register headers drop the containing-struct prefix that the MCX
 * headers keep (for example DMA_CHX_* instead of DMA_CH_DMA_CHX_*, and
 * HIGH_ADDRHI instead of MAC_ADDRESS0_HIGH_ADDRHI). Alias the names the
 * driver's ENET_QOS_REG_* macros build to the RT11xx ones so the single
 * driver source compiles unchanged on both families.
 */
#define ENET_QOS_DMA_CH_DMA_CHX_TX_CTRL_ST_MASK    ENET_QOS_DMA_CHX_TX_CTRL_ST_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_TX_CTRL_TxPBL_MASK ENET_QOS_DMA_CHX_TX_CTRL_TxPBL_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_TX_CTRL_OSF_MASK   ENET_QOS_DMA_CHX_TX_CTRL_OSF_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_RX_CTRL_SR_MASK    ENET_QOS_DMA_CHX_RX_CTRL_SR_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_RX_CTRL_RxPBL_MASK ENET_QOS_DMA_CHX_RX_CTRL_RxPBL_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_RX_CTRL_RBSZ_13_Y_MASK                                             \
	ENET_QOS_DMA_CHX_RX_CTRL_RBSZ_13_y_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_STAT_TI_MASK   ENET_QOS_DMA_CHX_STAT_TI_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_STAT_RI_MASK   ENET_QOS_DMA_CHX_STAT_RI_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_STAT_RBU_MASK  ENET_QOS_DMA_CHX_STAT_RBU_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_STAT_TBU_MASK  ENET_QOS_DMA_CHX_STAT_TBU_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_STAT_FBE_MASK  ENET_QOS_DMA_CHX_STAT_FBE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_INT_EN_NIE_MASK  ENET_QOS_DMA_CHX_INT_EN_NIE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_INT_EN_TIE_MASK  ENET_QOS_DMA_CHX_INT_EN_TIE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_INT_EN_RIE_MASK  ENET_QOS_DMA_CHX_INT_EN_RIE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_INT_EN_AIE_MASK  ENET_QOS_DMA_CHX_INT_EN_AIE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_INT_EN_RBUE_MASK ENET_QOS_DMA_CHX_INT_EN_RBUE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_INT_EN_RSE_MASK  ENET_QOS_DMA_CHX_INT_EN_RSE_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_TXDESC_LIST_ADDR_TDESLA_MASK                                       \
	ENET_QOS_DMA_CHX_TXDESC_LIST_ADDR_TDESLA_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_TXDESC_TAIL_PTR_TDTP_MASK                                          \
	ENET_QOS_DMA_CHX_TXDESC_TAIL_PTR_TDTP_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_TXDESC_RING_LENGTH_TDRL_MASK                                       \
	ENET_QOS_DMA_CHX_TXDESC_RING_LENGTH_TDRL_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_RXDESC_LIST_ADDR_RDESLA_MASK                                       \
	ENET_QOS_DMA_CHX_RXDESC_LIST_ADDR_RDESLA_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_RXDESC_TAIL_PTR_RDTP_MASK                                          \
	ENET_QOS_DMA_CHX_RXDESC_TAIL_PTR_RDTP_MASK
#define ENET_QOS_DMA_CH_DMA_CHX_RX_CONTROL2_RDRL_MASK                                              \
	ENET_QOS_DMA_CHX_RX_CONTROL2_RDRL_MASK
#define ENET_QOS_MTL_QUEUE_MTL_TXQX_OP_MODE_FTQ_MASK   ENET_QOS_MTL_TXQX_OP_MODE_FTQ_MASK
#define ENET_QOS_MTL_QUEUE_MTL_TXQX_OP_MODE_TSF_MASK   ENET_QOS_MTL_TXQX_OP_MODE_TSF_MASK
#define ENET_QOS_MTL_QUEUE_MTL_TXQX_OP_MODE_TQS_MASK   ENET_QOS_MTL_TXQX_OP_MODE_TQS_MASK
#define ENET_QOS_MTL_QUEUE_MTL_TXQX_OP_MODE_TXQEN_MASK ENET_QOS_MTL_TXQX_OP_MODE_TXQEN_MASK
#define ENET_QOS_MTL_QUEUE_MTL_RXQX_OP_MODE_RQS_MASK   ENET_QOS_MTL_RXQX_OP_MODE_RQS_MASK
#define ENET_QOS_MTL_QUEUE_MTL_RXQX_OP_MODE_RSF_MASK   ENET_QOS_MTL_RXQX_OP_MODE_RSF_MASK
#define ENET_QOS_MTL_QUEUE_MTL_RXQX_OP_MODE_FUP_MASK   ENET_QOS_MTL_RXQX_OP_MODE_FUP_MASK
#define ENET_QOS_MAC_ADDRESS0_HIGH_ADDRHI_MASK ENET_QOS_HIGH_ADDRHI_MASK
#define ENET_QOS_MAC_ADDRESS0_LOW_ADDRLO_MASK  ENET_QOS_LOW_ADDRLO_MASK

/* RT11xx lays the MAC address registers out as an array of {HIGH, LOW}. */
#define ENET_QOS_MAC_ADDR_HIGH(base) ((base)->MAC_ADDRESS[0].HIGH)
#define ENET_QOS_MAC_ADDR_LOW(base)  ((base)->MAC_ADDRESS[0].LOW)
#else
#error "ENET_QOS not enabled on this SOC series"
#endif

#define _PREFIX_UNDERLINE(x) _##x
#define _ENET_QOS_REG_FIELD(reg, field) MACRO_MAP_CAT(_PREFIX_UNDERLINE, reg, field, MASK)
#define _ENET_QOS_REG_MASK(reg, field) CONCAT(ENET_QOS_NAME, _ENET_QOS_REG_FIELD(reg, field))

/* Deciphers value of a field from a read value of an enet qos register
 *
 * reg: name of the register
 * field: name of the bit field within the register
 * val: value that had been read from the register
 */
#define ENET_QOS_REG_GET(reg, field, val) FIELD_GET(_ENET_QOS_REG_MASK(reg, field), val)

/* Prepares value of a field for a write to an enet qos register
 *
 * reg: name of the register
 * field: name of the bit field within the register
 * val: value to put into the field
 */
#define ENET_QOS_REG_PREP(reg, field, val) FIELD_PREP(_ENET_QOS_REG_MASK(reg, field), val)

#define ENET_QOS_DESC_ADDR(reg, field, addr)                                                       \
	((uint32_t)(addr) & _ENET_QOS_REG_MASK(reg, field))

struct nxp_enet_qos_config {
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	enet_qos_t *base;
};
#define ENET_QOS_MODULE_CFG(module_dev) ((struct nxp_enet_qos_config *) module_dev->config)

/** INTERNAL_HIDDEN @endcond */

#endif /* ZEPHYR_INCLUDE_DRIVERS_ETH_NXP_ENET_H__ */
