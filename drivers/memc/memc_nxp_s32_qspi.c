/*
 * Copyright 2023-2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_s32_qspi

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nxp_s32_qspi_memc, CONFIG_MEMC_LOG_LEVEL);

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/util.h>

#include "memc_nxp_s32_qspi.h"

/* Module Configuration Register */
#define QSPI_MCR                            0x0
#define QSPI_MCR_SWRSTSD_MASK               BIT(0)
#define QSPI_MCR_SWRSTSD(v)                 FIELD_PREP(QSPI_MCR_SWRSTSD_MASK, (v))
#define QSPI_MCR_SWRSTHD_MASK               BIT(1)
#define QSPI_MCR_SWRSTHD(v)                 FIELD_PREP(QSPI_MCR_SWRSTHD_MASK, (v))
#define QSPI_MCR_CLR_RXF_MASK               BIT(10)
#define QSPI_MCR_CLR_RXF(v)                 FIELD_PREP(QSPI_MCR_CLR_RXF_MASK, (v))
#define QSPI_MCR_CLR_TXF_MASK               BIT(11)
#define QSPI_MCR_CLR_TXF(v)                 FIELD_PREP(QSPI_MCR_CLR_TXF_MASK, (v))
#define QSPI_MCR_MDIS_MASK                  BIT(14)
#define QSPI_MCR_MDIS(v)                    FIELD_PREP(QSPI_MCR_MDIS_MASK, (v))
#define QSPI_MCR_DQS_FA_SEL_MASK            GENMASK(25, 24)
#define QSPI_MCR_DQS_FA_SEL(v)              FIELD_PREP(QSPI_MCR_DQS_FA_SEL_MASK, (v))
/* IP Configuration Register */
#define QSPI_IPCR                           0x8
#define QSPI_IPCR_IDATSZ_MASK               GENMASK(15, 0)
#define QSPI_IPCR_IDATSZ(v)                 FIELD_PREP(QSPI_IPCR_IDATSZ_MASK, (v))
#define QSPI_IPCR_SEQID_MASK                GENMASK(27, 24)
#define QSPI_IPCR_SEQID(v)                  FIELD_PREP(QSPI_IPCR_SEQID_MASK, (v))
/* Flash Memory Configuration Register */
#define QSPI_FLSHCR                         0xc
#define QSPI_FLSHCR_TCSS_MASK               GENMASK(3, 0)
#define QSPI_FLSHCR_TCSS(v)                 FIELD_PREP(QSPI_FLSHCR_TCSS_MASK, (v))
#define QSPI_FLSHCR_TCSH_MASK               GENMASK(11, 8)
#define QSPI_FLSHCR_TCSH(v)                 FIELD_PREP(QSPI_FLSHCR_TCSH_MASK, (v))
/* Buffer n Configuration Register */
#define QSPI_BUFCR(n)                       (0x10 + 0x4 * (n))
#define QSPI_BUFCR_MSTRID_MASK              GENMASK(3, 0)
#define QSPI_BUFCR_MSTRID(v)                FIELD_PREP(QSPI_BUFCR_MSTRID_MASK, (v))
#define QSPI_BUFCR_ADATSZ_MASK              GENMASK(13, 8)
#define QSPI_BUFCR_ADATSZ(v)                FIELD_PREP(QSPI_BUFCR_ADATSZ_MASK, (v))
#define QSPI_BUFCR_ALLMST_MASK              BIT(31)
#define QSPI_BUFCR_ALLMST(v)                FIELD_PREP(QSPI_BUFCR_ALLMST_MASK, (v))
/* Buffer Generic Configuration Register */
#define QSPI_BFGENCR                        0x20
#define QSPI_BFGENCR_SEQID_MASK             GENMASK(15, 12)
#define QSPI_BFGENCR_SEQID(v)               FIELD_PREP(QSPI_BFGENCR_SEQID_MASK, (v))
/* SOC Configuration Register */
#define QSPI_SOCCR                          0x24
#define QSPI_SOCCR_SOCCFG_MASK              GENMASK(31, 0)
#define QSPI_SOCCR_SOCCFG(v)                FIELD_PREP(QSPI_SOCCR_SOCCFG_MASK, (v))
/* Buffer n Top Index Register */
#define QSPI_BUFIND(n)                      (0x30 + 0x4 * (n))
#define QSPI_BUFIND_TPINDX_MASK             GENMASK(8, 3)
#define QSPI_BUFIND_TPINDX(v)               FIELD_PREP(QSPI_BUFIND_TPINDX_MASK, (v))
/* DLL Flash Memory A Configuration Register */
#define QSPI_DLLCRA                         0x60
#define QSPI_DLLCRA_SLV_UPD_MASK            BIT(0)
#define QSPI_DLLCRA_SLV_UPD(v)              FIELD_PREP(QSPI_DLLCRA_SLV_UPD_MASK, (v))
#define QSPI_DLLCRA_SLV_DLL_BYPASS_MASK     BIT(1)
#define QSPI_DLLCRA_SLV_DLL_BYPASS(v)       FIELD_PREP(QSPI_DLLCRA_SLV_DLL_BYPASS_MASK, (v))
#define QSPI_DLLCRA_SLV_EN_MASK             BIT(2)
#define QSPI_DLLCRA_SLV_EN(v)               FIELD_PREP(QSPI_DLLCRA_SLV_EN_MASK, (v))
#define QSPI_DLLCRA_SLV_DLY_COARSE_MASK     GENMASK(11, 8)
#define QSPI_DLLCRA_SLV_DLY_COARSE(v)       FIELD_PREP(QSPI_DLLCRA_SLV_DLY_COARSE_MASK, (v))
#define QSPI_DLLCRA_SLV_DLY_OFFSET_MASK     GENMASK(14, 12)
#define QSPI_DLLCRA_SLV_DLY_OFFSET(v)       FIELD_PREP(QSPI_DLLCRA_SLV_DLY_OFFSET_MASK, (v))
#define QSPI_DLLCRA_SLV_FINE_OFFSET_MASK    GENMASK(19, 16)
#define QSPI_DLLCRA_SLV_FINE_OFFSET(v)      FIELD_PREP(QSPI_DLLCRA_SLV_FINE_OFFSET_MASK, (v))
#define QSPI_DLLCRA_FREQEN_MASK             BIT(30)
#define QSPI_DLLCRA_FREQEN(v)               FIELD_PREP(QSPI_DLLCRA_FREQEN_MASK, (v))
/* Serial Flash Memory Address Register */
#define QSPI_SFAR                           0x100
#define QSPI_SFAR_SFADR_MASK                GENMASK(31, 0)
#define QSPI_SFAR_SFADR(v)                  FIELD_PREP(QSPI_SFAR_SFADR_MASK, (v))
/* Sampling Register */
#define QSPI_SMPR                           0x108
#define QSPI_SMPR_FSPHS_MASK                BIT(5)
#define QSPI_SMPR_FSPHS(v)                  FIELD_PREP(QSPI_SMPR_FSPHS_MASK, (v))
#define QSPI_SMPR_FSDLY_MASK                BIT(6)
#define QSPI_SMPR_FSDLY(v)                  FIELD_PREP(QSPI_SMPR_FSDLY_MASK, (v))
#define QSPI_SMPR_DLLFSMPFA_MASK            GENMASK(26, 24)
#define QSPI_SMPR_DLLFSMPFA(v)              FIELD_PREP(QSPI_SMPR_DLLFSMPFA_MASK, (v))
/* RX Buffer Status Register */
#define QSPI_RBSR                           0x10c
#define QSPI_RBSR_RDBFL_MASK                GENMASK(7, 0)
#define QSPI_RBSR_RDBFL(v)                  FIELD_PREP(QSPI_RBSR_RDBFL_MASK, (v))
#define QSPI_RBSR_RDCTR_MASK                GENMASK(31, 16)
#define QSPI_RBSR_RDCTR(v)                  FIELD_PREP(QSPI_RBSR_RDCTR_MASK, (v))
/* RX Buffer Control Register */
#define QSPI_RBCT                           0x110
#define QSPI_RBCT_WMRK_MASK                 GENMASK(6, 0)
#define QSPI_RBCT_WMRK(v)                   FIELD_PREP(QSPI_RBCT_WMRK_MASK, (v))
#define QSPI_RBCT_RXBRD_MASK                BIT(8)
#define QSPI_RBCT_RXBRD(v)                  FIELD_PREP(QSPI_RBCT_RXBRD_MASK, (v))
/* DLL Status Register */
#define QSPI_DLLSR                          0x12c
#define QSPI_DLLSR_DLLA_SLV_COARSE_VAL_MASK GENMASK(3, 0)
#define QSPI_DLLSR_DLLA_SLV_COARSE_VAL(v)   FIELD_PREP(QSPI_DLLSR_DLLA_SLV_COARSE_VAL_MASK, (v))
#define QSPI_DLLSR_DLLA_SLV_FINE_VAL_MASK   GENMASK(7, 4)
#define QSPI_DLLSR_DLLA_SLV_FINE_VAL(v)     FIELD_PREP(QSPI_DLLSR_DLLA_SLV_FINE_VAL_MASK, (v))
#define QSPI_DLLSR_DLLA_FINE_UNDERFLOW_MASK BIT(12)
#define QSPI_DLLSR_DLLA_FINE_UNDERFLOW(v)   FIELD_PREP(QSPI_DLLSR_DLLA_FINE_UNDERFLOW_MASK, (v))
#define QSPI_DLLSR_DLLA_RANGE_ERR_MASK      BIT(13)
#define QSPI_DLLSR_DLLA_RANGE_ERR(v)        FIELD_PREP(QSPI_DLLSR_DLLA_RANGE_ERR_MASK, (v))
#define QSPI_DLLSR_SLVA_LOCK_MASK           BIT(14)
#define QSPI_DLLSR_SLVA_LOCK(v)             FIELD_PREP(QSPI_DLLSR_SLVA_LOCK_MASK, (v))
#define QSPI_DLLSR_DLLA_LOCK_MASK           BIT(15)
#define QSPI_DLLSR_DLLA_LOCK(v)             FIELD_PREP(QSPI_DLLSR_DLLA_LOCK_MASK, (v))
/* Data Learning Status Flash Memory A Register */
#define QSPI_DLSR_FA                        0x134
#define QSPI_DLSR_FA_NEG_EDGE_MASK          GENMASK(7, 0)
#define QSPI_DLSR_FA_NEG_EDGE(v)            FIELD_PREP(QSPI_DLSR_FA_NEG_EDGE_MASK, (v))
#define QSPI_DLSR_FA_POS_EDGE_MASK          GENMASK(15, 8)
#define QSPI_DLSR_FA_POS_EDGE(v)            FIELD_PREP(QSPI_DLSR_FA_POS_EDGE_MASK, (v))
#define QSPI_DLSR_FA_DLPFFA_MASK            BIT(31)
#define QSPI_DLSR_FA_DLPFFA(v)              FIELD_PREP(QSPI_DLSR_FA_DLPFFA_MASK, (v))
/* TX Buffer Status Register */
#define QSPI_TBSR                           0x150
#define QSPI_TBSR_TRBFL_MASK                GENMASK(5, 0)
#define QSPI_TBSR_TRBFL(v)                  FIELD_PREP(QSPI_TBSR_TRBFL_MASK, (v))
#define QSPI_TBSR_TRCTR_MASK                GENMASK(31, 16)
#define QSPI_TBSR_TRCTR(v)                  FIELD_PREP(QSPI_TBSR_TRCTR_MASK, (v))
/* TX Buffer Data Register */
#define QSPI_TBDR                           0x154
#define QSPI_TBDR_TXDATA_MASK               GENMASK(31, 0)
#define QSPI_TBDR_TXDATA(v)                 FIELD_PREP(QSPI_TBDR_TXDATA_MASK, (v))
/* TX Buffer Control Register */
#define QSPI_TBCT                           0x158
#define QSPI_TBCT_WMRK_MASK                 GENMASK(4, 0)
#define QSPI_TBCT_WMRK(v)                   FIELD_PREP(QSPI_TBCT_WMRK_MASK, (v))
/* Status Register */
#define QSPI_SR                             0x15c
#define QSPI_SR_BUSY_MASK                   BIT(0)
#define QSPI_SR_BUSY(v)                     FIELD_PREP(QSPI_SR_BUSY_MASK, (v))
#define QSPI_SR_IP_ACC_MASK                 BIT(1)
#define QSPI_SR_IP_ACC(v)                   FIELD_PREP(QSPI_SR_IP_ACC_MASK, (v))
#define QSPI_SR_AHB_ACC_MASK                BIT(2)
#define QSPI_SR_AHB_ACC(v)                  FIELD_PREP(QSPI_SR_AHB_ACC_MASK, (v))
#define QSPI_SR_AHBTRN_MASK                 BIT(6)
#define QSPI_SR_AHBTRN(v)                   FIELD_PREP(QSPI_SR_AHBTRN_MASK, (v))
#define QSPI_SR_AHB0NE_MASK                 BIT(7)
#define QSPI_SR_AHB0NE(v)                   FIELD_PREP(QSPI_SR_AHB0NE_MASK, (v))
#define QSPI_SR_AHB1NE_MASK                 BIT(8)
#define QSPI_SR_AHB1NE(v)                   FIELD_PREP(QSPI_SR_AHB1NE_MASK, (v))
#define QSPI_SR_AHB2NE_MASK                 BIT(9)
#define QSPI_SR_AHB2NE(v)                   FIELD_PREP(QSPI_SR_AHB2NE_MASK, (v))
#define QSPI_SR_AHB3NE_MASK                 BIT(10)
#define QSPI_SR_AHB3NE(v)                   FIELD_PREP(QSPI_SR_AHB3NE_MASK, (v))
#define QSPI_SR_AHB0FUL_MASK                BIT(11)
#define QSPI_SR_AHB0FUL(v)                  FIELD_PREP(QSPI_SR_AHB0FUL_MASK, (v))
#define QSPI_SR_AHB1FUL_MASK                BIT(12)
#define QSPI_SR_AHB1FUL(v)                  FIELD_PREP(QSPI_SR_AHB1FUL_MASK, (v))
#define QSPI_SR_AHB2FUL_MASK                BIT(13)
#define QSPI_SR_AHB2FUL(v)                  FIELD_PREP(QSPI_SR_AHB2FUL_MASK, (v))
#define QSPI_SR_AHB3FUL_MASK                BIT(14)
#define QSPI_SR_AHB3FUL(v)                  FIELD_PREP(QSPI_SR_AHB3FUL_MASK, (v))
#define QSPI_SR_RXWE_MASK                   BIT(16)
#define QSPI_SR_RXWE(v)                     FIELD_PREP(QSPI_SR_RXWE_MASK, (v))
#define QSPI_SR_RXFULL_MASK                 BIT(19)
#define QSPI_SR_RXFULL(v)                   FIELD_PREP(QSPI_SR_RXFULL_MASK, (v))
#define QSPI_SR_RXDMA_MASK                  BIT(23)
#define QSPI_SR_RXDMA(v)                    FIELD_PREP(QSPI_SR_RXDMA_MASK, (v))
#define QSPI_SR_TXNE_MASK                   BIT(24)
#define QSPI_SR_TXNE(v)                     FIELD_PREP(QSPI_SR_TXNE_MASK, (v))
#define QSPI_SR_TXWA_MASK                   BIT(25)
#define QSPI_SR_TXWA(v)                     FIELD_PREP(QSPI_SR_TXWA_MASK, (v))
#define QSPI_SR_TXDMA_MASK                  BIT(26)
#define QSPI_SR_TXDMA(v)                    FIELD_PREP(QSPI_SR_TXDMA_MASK, (v))
#define QSPI_SR_TXFULL_MASK                 BIT(27)
#define QSPI_SR_TXFULL(v)                   FIELD_PREP(QSPI_SR_TXFULL_MASK, (v))
/* Flag Register */
#define QSPI_FR                             0x160
#define QSPI_FR_TFF_MASK                    BIT(0)
#define QSPI_FR_TFF(v)                      FIELD_PREP(QSPI_FR_TFF_MASK, (v))
#define QSPI_FR_IPIEF_MASK                  BIT(6)
#define QSPI_FR_IPIEF(v)                    FIELD_PREP(QSPI_FR_IPIEF_MASK, (v))
#define QSPI_FR_IPAEF_MASK                  BIT(7)
#define QSPI_FR_IPAEF(v)                    FIELD_PREP(QSPI_FR_IPAEF_MASK, (v))
#define QSPI_FR_ABOF_MASK                   BIT(12)
#define QSPI_FR_ABOF(v)                     FIELD_PREP(QSPI_FR_ABOF_MASK, (v))
#define QSPI_FR_AIBSEF_MASK                 BIT(13)
#define QSPI_FR_AIBSEF(v)                   FIELD_PREP(QSPI_FR_AIBSEF_MASK, (v))
#define QSPI_FR_AITEF_MASK                  BIT(14)
#define QSPI_FR_AITEF(v)                    FIELD_PREP(QSPI_FR_AITEF_MASK, (v))
#define QSPI_FR_RBDF_MASK                   BIT(16)
#define QSPI_FR_RBDF(v)                     FIELD_PREP(QSPI_FR_RBDF_MASK, (v))
#define QSPI_FR_RBOF_MASK                   BIT(17)
#define QSPI_FR_RBOF(v)                     FIELD_PREP(QSPI_FR_RBOF_MASK, (v))
#define QSPI_FR_ILLINE_MASK                 BIT(23)
#define QSPI_FR_ILLINE(v)                   FIELD_PREP(QSPI_FR_ILLINE_MASK, (v))
#define QSPI_FR_TBUF_MASK                   BIT(26)
#define QSPI_FR_TBUF(v)                     FIELD_PREP(QSPI_FR_TBUF_MASK, (v))
#define QSPI_FR_TBFF_MASK                   BIT(27)
#define QSPI_FR_TBFF(v)                     FIELD_PREP(QSPI_FR_TBFF_MASK, (v))
/* Interrupt and DMA Request Select and Enable Register */
#define QSPI_RSER                           0x164
#define QSPI_RSER_TFIE_MASK                 BIT(0)
#define QSPI_RSER_TFIE(v)                   FIELD_PREP(QSPI_RSER_TFIE_MASK, (v))
#define QSPI_RSER_IPIEIE_MASK               BIT(6)
#define QSPI_RSER_IPIEIE(v)                 FIELD_PREP(QSPI_RSER_IPIEIE_MASK, (v))
#define QSPI_RSER_IPAEIE_MASK               BIT(7)
#define QSPI_RSER_IPAEIE(v)                 FIELD_PREP(QSPI_RSER_IPAEIE_MASK, (v))
#define QSPI_RSER_ABOIE_MASK                BIT(12)
#define QSPI_RSER_ABOIE(v)                  FIELD_PREP(QSPI_RSER_ABOIE_MASK, (v))
#define QSPI_RSER_AIBSIE_MASK               BIT(13)
#define QSPI_RSER_AIBSIE(v)                 FIELD_PREP(QSPI_RSER_AIBSIE_MASK, (v))
#define QSPI_RSER_AITIE_MASK                BIT(14)
#define QSPI_RSER_AITIE(v)                  FIELD_PREP(QSPI_RSER_AITIE_MASK, (v))
#define QSPI_RSER_RBDIE_MASK                BIT(16)
#define QSPI_RSER_RBDIE(v)                  FIELD_PREP(QSPI_RSER_RBDIE_MASK, (v))
#define QSPI_RSER_RBOIE_MASK                BIT(17)
#define QSPI_RSER_RBOIE(v)                  FIELD_PREP(QSPI_RSER_RBOIE_MASK, (v))
#define QSPI_RSER_RBDDE_MASK                BIT(21)
#define QSPI_RSER_RBDDE(v)                  FIELD_PREP(QSPI_RSER_RBDDE_MASK, (v))
#define QSPI_RSER_ILLINIE_MASK              BIT(23)
#define QSPI_RSER_ILLINIE(v)                FIELD_PREP(QSPI_RSER_ILLINIE_MASK, (v))
#define QSPI_RSER_TBFDE_MASK                BIT(25)
#define QSPI_RSER_TBFDE(v)                  FIELD_PREP(QSPI_RSER_TBFDE_MASK, (v))
#define QSPI_RSER_TBUIE_MASK                BIT(26)
#define QSPI_RSER_TBUIE(v)                  FIELD_PREP(QSPI_RSER_TBUIE_MASK, (v))
#define QSPI_RSER_TBFIE_MASK                BIT(27)
#define QSPI_RSER_TBFIE(v)                  FIELD_PREP(QSPI_RSER_TBFIE_MASK, (v))
/* Sequence Pointer Clear Register */
#define QSPI_SPTRCLR                        0x16c
#define QSPI_SPTRCLR_BFPTRC_MASK            BIT(0)
#define QSPI_SPTRCLR_BFPTRC(v)              FIELD_PREP(QSPI_SPTRCLR_BFPTRC_MASK, (v))
#define QSPI_SPTRCLR_IPPTRC_MASK            BIT(8)
#define QSPI_SPTRCLR_IPPTRC(v)              FIELD_PREP(QSPI_SPTRCLR_IPPTRC_MASK, (v))
/* Serial Flash Memory Top Address Register */
#define QSPI_SFAD(n)                        (0x180 + 0x4 * (n))
#define QSPI_SFAD_TPAD_MASK                 GENMASK(31, 10)
#define QSPI_SFAD_TPAD(v)                   FIELD_PREP(QSPI_SFAD_TPAD_MASK, (v))
/* RX Buffer Data Register */
#define QSPI_RBDR(n)                        (0x200 + 4 * (n))
#define QSPI_RBDR_RXDATA_MASK               GENMASK(31, 0)
#define QSPI_RBDR_RXDATA(v)                 FIELD_PREP(QSPI_RBDR_RXDATA_MASK, (v))
/* LUT Key Register */
#define QSPI_LUTKEY                         0x300
#define QSPI_LUTKEY_KEY_MASK                GENMASK(31, 0)
#define QSPI_LUTKEY_KEY(v)                  FIELD_PREP(QSPI_LUTKEY_KEY_MASK, (v))
/* LUT Lock Configuration Register */
#define QSPI_LCKCR                          0x304
#define QSPI_LCKCR_LOCK_MASK                BIT(0)
#define QSPI_LCKCR_LOCK(v)                  FIELD_PREP(QSPI_LCKCR_LOCK_MASK, (v))
#define QSPI_LCKCR_UNLOCK_MASK              BIT(1)
#define QSPI_LCKCR_UNLOCK(v)                FIELD_PREP(QSPI_LCKCR_UNLOCK_MASK, (v))
/* LUT Register */
#define QSPI_LUT(n)                         (0x310 + 4 * (n))
#define QSPI_LUT_KEY_KEY_MASK               GENMASK(31, 0)
#define QSPI_LUT_KEY_KEY(v)                 FIELD_PREP(QSPI_LUT_KEY_KEY_MASK, (v))
#define QSPI_LUT_OPRND0_MASK                GENMASK(7, 0)
#define QSPI_LUT_OPRND0(v)                  FIELD_PREP(QSPI_LUT_OPRND0_MASK, (v))
#define QSPI_LUT_PAD0_MASK                  GENMASK(9, 8)
#define QSPI_LUT_PAD0(v)                    FIELD_PREP(QSPI_LUT_PAD0_MASK, (v))
#define QSPI_LUT_INSTR0_MASK                GENMASK(15, 10)
#define QSPI_LUT_INSTR0(v)                  FIELD_PREP(QSPI_LUT_INSTR0_MASK, (v))
#define QSPI_LUT_OPRND1_MASK                GENMASK(23, 16)
#define QSPI_LUT_OPRND1(v)                  FIELD_PREP(QSPI_LUT_OPRND1_MASK, (v))
#define QSPI_LUT_PAD1_MASK                  GENMASK(25, 24)
#define QSPI_LUT_PAD1(v)                    FIELD_PREP(QSPI_LUT_PAD1_MASK, (v))
#define QSPI_LUT_INSTR1_MASK                GENMASK(31, 26)
#define QSPI_LUT_INSTR1(v)                  FIELD_PREP(QSPI_LUT_INSTR1_MASK, (v))

/* Handy accessors */
#define REG_READ(r)     sys_read32(config->base + (r))
#define REG_WRITE(r, v) sys_write32((v), config->base + (r))

// /* Mapping between QSPI chip select signals and devicetree chip select identifiers */
// #define QSPI_PCSFA1 0
// #define QSPI_PCSFA2 1
// #define QSPI_PCSFB1 2
// #define QSPI_PCSFB2 3

// -------------------------------------------------------------------------------------------------

// k1, ze
// #define CONFIG_NXP_QSPI_HAS_EXTRA_IDLE 0

// k1, ze
// #define CONFIG_NXP_QSPI_HAS_CAS 0

// ze
// #define CONFIG_NXP_QSPI_HAS_BYTE_SWAP 0

// k1, k3
#define CONFIG_NXP_QSPI_HAS_RBCT_RXBRD 1

// k1, ze
// #define CONFIG_NXP_QSPI_HAS_FLSHCR_TDH

// k1, ze
// #define CONFIG_NXP_QSPI_HAS_MCR_DQS_EN

// k1, ze
// #define CONFIG_NXP_QSPI_HAS_QSPI_MCR_DDR_EN

// k3
#define CONFIG_NXP_QSPI_HAS_SOCCR_SOCCFG 1

// ze
// #define CONFIG_NXP_QSPI_HAS_DLLCRA_DLLEN

// ze
// #define CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT

// k3, ze
#define CONFIG_NXP_QSPI_HAS_DLLSR_SLVA_LOCK 1

// k3, ze
#define CONFIG_NXP_QSPI_HAS_DLLSR_DLLA_LOCK 1

// k3, ze
#define CONFIG_NXP_QSPI_HAS_DLLSR_DLLA_RANGE_ERR 1

// ze
// #define CONFIG_NXP_QSPI_HAS_DLLCRA_DLLRES

// others?
// #define CONFIG_NXP_QSPI_HAS_INTERNAL_DQS

// k3, others?
#define CONFIG_NXP_QSPI_HAS_LOOPBACK

// others?
// #define CONFIG_NXP_QSPI_HAS_LOOPBACK_DQS

// k3, others?
#define CONFIG_NXP_QSPI_HAS_EXTERNAL_DQS

// -------------------------------------------------------------------------------------------------

struct memc_nxp_s32_qspi_data {
	// uint8_t instance;
};

struct memc_nxp_s32_qspi_config {
	mem_addr_t base;
	const struct pinctrl_dev_config *pincfg;
	const struct nxp_qspi_controller *controller;
};

enum nxp_qspi_read_mode {
	QSPI_READ_MODE_INTERNAL_DQS = 0U,
	QSPI_READ_MODE_LOOPBACK = 1U,
	QSPI_READ_MODE_LOOPBACK_DQS = 2U,
	QSPI_READ_MODE_EXTERNAL_DQS = 3U,
};

enum nxp_qspi_dll_mode {
	QSPI_DLL_BYPASSED = 0U,
	QSPI_DLL_MANUAL_UPDATE = 1U,
	QSPI_DLL_AUTO_UPDATE = 2U,
};

struct nxp_qspi_dll {
	enum nxp_qspi_dll_mode dllMode;
	bool freqEnable;
	uint8_t referenceCounter;
	uint8_t resolution;
	uint8_t coarseDelay;
	uint8_t fineDelay;
	uint8_t tapSelect;
};

// number of ahb rx buffers available
#define CONFIG_NXP_QSPI_RX_BUFFERS 4

struct nxp_qspi_buffer {
	uint8_t master;
	uint16_t size;
};

struct nxp_qspi_rx_buffers {
	struct nxp_qspi_buffer buffers[CONFIG_NXP_QSPI_RX_BUFFERS];
	bool all_masters;
};

#define NXP_QSPI_MAX_TPAD 2

struct nxp_qspi_port {
	uint32_t memory_size[NXP_QSPI_MAX_TPAD];
	uint8_t io2_idle;
	uint8_t io3_idle;
	enum nxp_qspi_read_mode readMode;
	struct nxp_qspi_dll dllSettings;
};

struct nxp_qspi_address {
	uint8_t column_space;
	bool word_addressable;
	bool byte_swap;
};

struct nxp_qspi_sampling {
	bool delay_half_cycle;
	bool phase_inverted;
};

struct nxp_qspi_cs {
	uint8_t hold_time;
	uint8_t setup_time;
};

struct nxp_qspi_timing {
	bool aligned_2x_refclk;
	struct nxp_qspi_cs cs;
	struct nxp_qspi_sampling sampling;
};

// TODO: number of ports, range 1..2 (A, B)
#define CONFIG_NXP_QSPI_MAX_PORTS 1

struct nxp_qspi_controller {
	mem_addr_t ahb_base;
	uint32_t chip_options;
	bool ddr;
	struct nxp_qspi_timing timing;
	struct nxp_qspi_rx_buffers rx_buffers;
	struct nxp_qspi_address address;
	struct nxp_qspi_port ports[CONFIG_NXP_QSPI_MAX_PORTS];
};

// -------------------------------------------------------------------------------------------------
// Qspi_Ip_ConfigureController()

static void nxp_qspi_config_ports(const struct memc_nxp_s32_qspi_config *config)
{
	const struct nxp_qspi_controller *controller = config->controller;
	const struct nxp_qspi_port *port;
	uint32_t reg_val;

	reg_val = controller->ahb_base;
	for (int i = 0; i < CONFIG_NXP_QSPI_MAX_PORTS; i++) {
		port = &controller->ports[i];

		/* Configure external flash memory map sizes */
		for (int j = 0; j < NXP_QSPI_MAX_TPAD; j++) {
			reg_val += port->memory_size[j];
			REG_WRITE(QSPI_SFAD((i * 2) + j), QSPI_SFAD_TPAD(reg_val));
		}

#if defined(CONFIG_NXP_QSPI_HAS_EXTRA_IDLE)
		/* Configure idle line values */
		reg_val = REG_READ(QSPI_MCR);
		if (i == 0) {
			reg_val & ~(QSPI_MCR_ISD2FA_MASK | QSPI_MCR_ISD3FA_MASK);
			reg_val |=
				QSPI_MCR_ISD2FA(port->io2_idle) | QSPI_MCR_ISD3FA(port->io3_idle);
		} else {
			reg_val & ~(QSPI_MCR_ISD2FB_MASK | QSPI_MCR_ISD3FB_MASK);
			reg_val |=
				QSPI_MCR_ISD2FB(port->io2_idle) | QSPI_MCR_ISD3FB(port->io3_idle);
		}
		REG_WRITE(QSPI_MCR, reg_val);
#endif /* CONFIG_NXP_QSPI_HAS_EXTRA_IDLE */
	}
}

static void nxp_qspi_config_flash_address(const struct memc_nxp_s32_qspi_config *config)
{
	const struct nxp_qspi_address *address = &config->controller->address;

	ARG_UNUSED(address); /* avoid compiler warning */

#if defined(CONFIG_NXP_QSPI_HAS_CAS)
	/* Set Column address and Word addressable */
	REG_WRITE(QSPI_SFACR, (QSPI_SFACR_CAS(address->column_space) |
			       QSPI_SFACR_WA(address->word_addressable ? 1U : 0U)));
#endif /* CONFIG_NXP_QSPI_HAS_CAS */

#if defined(CONFIG_NXP_QSPI_HAS_BYTE_SWAP)
	uint32_t reg_val = REG_READ(QSPI_SFACR) & ~QSPI_SFACR_BYTE_SWAP_MASK;
	REG_WRITE(QSPI_SFACR, reg_val | QSPI_SFACR_BYTE_SWAP(address->byte_swap ? 1U : 0U));
#endif /* CONFIG_NXP_QSPI_HAS_BYTE_SWAP */
}

static void nxp_qspi_config_cs(const struct memc_nxp_s32_qspi_config *config)
{
	const struct nxp_qspi_cs *cs = &config->controller->timing.cs;
	uint32_t reg_val;

	/* Configure CS holdtime and setup time */
	reg_val = REG_READ(QSPI_FLSHCR) & ~(QSPI_FLSHCR_TCSH_MASK | QSPI_FLSHCR_TCSS_MASK);
	reg_val |= QSPI_FLSHCR_TCSH(cs->hold_time) | QSPI_FLSHCR_TCSS(cs->setup_time);
	REG_WRITE(QSPI_FLSHCR, reg_val);
}

static void nxp_qspi_config_buffers(const struct memc_nxp_s32_qspi_config *config)
{
	const struct nxp_qspi_rx_buffers *rx_buffers = &config->controller->rx_buffers;
	uint32_t reg_val;
	uint8_t i;

	/* Set watermarks */
	reg_val = REG_READ(QSPI_TBCT) & ~QSPI_TBCT_WMRK_MASK;
	REG_WRITE(QSPI_TBCT, reg_val | QSPI_TBCT_WMRK(1U));
	reg_val = REG_READ(QSPI_RBCT) & ~QSPI_RBCT_WMRK_MASK;
	REG_WRITE(QSPI_RBCT, reg_val | QSPI_RBCT_WMRK(0U));

#if defined(CONFIG_NXP_QSPI_HAS_RBCT_RXBRD)
	/* Read Rx buffer through RBDR registers */
	reg_val = REG_READ(QSPI_RBCT) & ~QSPI_RBCT_RXBRD_MASK;
	REG_WRITE(QSPI_RBCT, reg_val | QSPI_RBCT_RXBRD(1U));
#endif /* CONFIG_NXP_QSPI_HAS_RBCT_RXBRD */

	reg_val = 0;
	for (i = 0; i < CONFIG_NXP_QSPI_RX_BUFFERS; i++) {
		/* Set AHB transfer sizes to match the buffer sizes */
		REG_WRITE(QSPI_BUFCR(i), (QSPI_BUFCR_ADATSZ(rx_buffers->buffers[i].size >> 3U) |
					  QSPI_BUFCR_MSTRID(rx_buffers->buffers[i].master)));
		/* Set AHB buffer index */
		if (i < (CONFIG_NXP_QSPI_RX_BUFFERS - 1)) {
			reg_val += rx_buffers->buffers[i].size;
			REG_WRITE(QSPI_BUFIND(i), QSPI_BUFIND_TPINDX(reg_val));
		}
	}
	REG_WRITE(QSPI_BUFCR(CONFIG_NXP_QSPI_RX_BUFFERS - 1),
		  QSPI_BUFCR_ALLMST(rx_buffers->all_masters ? 1U : 0U));
}

// TODO: extend to multiple ports
static void nxp_qspi_config_dqs_source(const struct memc_nxp_s32_qspi_config *config,
				       enum nxp_qspi_read_mode mode)
{
	uint32_t reg_val;

	reg_val = REG_READ(QSPI_MCR) & ~QSPI_MCR_DQS_FA_SEL_MASK;
	switch (mode) {
#if defined(CONFIG_NXP_QSPI_HAS_INTERNAL_DQS)
	case QSPI_READ_MODE_INTERNAL_DQS:
		reg_val |= QSPI_MCR_DQS_FA_SEL(0U);
		break;
#endif
#if defined(CONFIG_NXP_QSPI_HAS_LOOPBACK)
	case QSPI_READ_MODE_LOOPBACK:
		reg_val |= QSPI_MCR_DQS_FA_SEL(1U);
		break;
#endif
#if defined(CONFIG_NXP_QSPI_HAS_LOOPBACK_DQS)
	case QSPI_READ_MODE_LOOPBACK_DQS:
		reg_val |= QSPI_MCR_DQS_FA_SEL(2U);
		break;
#endif
	case QSPI_READ_MODE_EXTERNAL_DQS:
		reg_val |= QSPI_MCR_DQS_FA_SEL(3U);
		break;
	default:
		// TODO: error?
		break;
	}
	REG_WRITE(QSPI_MCR, reg_val);
}

static void nxp_qspi_config_read_options(const struct memc_nxp_s32_qspi_config *config)
{
	const struct nxp_qspi_controller *controller = config->controller;
	uint32_t reg_val;

#if defined(CONFIG_NXP_QSPI_HAS_MCR_DQS_EN)
	/* Enable DQS */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) | QSPI_MCR_DQS_EN(1U));
#endif /* CONFIG_NXP_QSPI_HAS_MCR_DQS_EN */

	if (controller->ddr) {
#if defined(CONFIG_NXP_QSPI_HAS_QSPI_MCR_DDR_EN)
		/* Enable DDR */
		REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) | QSPI_MCR_DDR_EN(1U));
#endif /* CONFIG_NXP_QSPI_HAS_QSPI_MCR_DDR_EN */

#if defined(CONFIG_NXP_QSPI_HAS_FLSHCR_TDH)
		reg_val = REG_READ(QSPI_FLSHCR) & ~QSPI_FLSHCR_TDH_MASK;
		reg_val |= QSPI_FLSHCR_TDH(controller->timing.aligned_2x_refclk ? 1U : 0U);
		REG_WRITE(QSPI_FLSHCR, reg_val);
#endif /* CONFIG_NXP_QSPI_HAS_FLSHCR_TDH */
	} else {
#if defined(CONFIG_NXP_QSPI_HAS_QSPI_MCR_DDR_EN)
		/* Disable DDR to use SDR */
		REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) & ~QSPI_MCR_DDR_EN_MASK);
#endif /* CONFIG_NXP_QSPI_HAS_QSPI_MCR_DDR_EN */

#if defined(CONFIG_NXP_QSPI_HAS_FLSHCR_TDH)
		/* Override data align configuration when in SDR mode */
		reg_val = REG_READ(QSPI_FLSHCR) & ~QSPI_FLSHCR_TDH_MASK;
		REG_WRITE(QSPI_FLSHCR, reg_val | QSPI_FLSHCR_TDH(QSPI_FLASH_DATA_ALIGN_REFCLK));
#endif /* CONFIG_NXP_QSPI_HAS_FLSHCR_TDH */
	}

	// TODO: extend to multiple ports
	/* select DQS source (internal/loopback/external) */
	nxp_qspi_config_dqs_source(config, controller->ports[0].readMode);

	// TODO: extend to multiple ports
	/* Select DLL tap in SMPR register */
	reg_val = REG_READ(QSPI_SMPR) & ~QSPI_SMPR_DLLFSMPFA_MASK;
	REG_WRITE(QSPI_SMPR,
		  reg_val | QSPI_SMPR_DLLFSMPFA(controller->ports[0].dllSettings.tapSelect));
}

static void nxp_qspi_config_chip_options(const struct memc_nxp_s32_qspi_config *config)
{
#if defined(CONFIG_NXP_QSPI_HAS_SOCCR_SOCCFG)
	const struct nxp_qspi_controller *controller = config->controller;

	REG_WRITE(QSPI_SOCCR, controller->chip_options);
#endif /* CONFIG_NXP_QSPI_HAS_SOCCR_SOCCFG */
}

static void nxp_qspi_config_sampling(const struct memc_nxp_s32_qspi_config *config)
{
	const struct nxp_qspi_sampling *sampling = &config->controller->timing.sampling;
	uint32_t reg_val;

	reg_val = REG_READ(QSPI_SMPR) & ~(QSPI_SMPR_FSPHS_MASK | QSPI_SMPR_FSDLY_MASK);
	reg_val |= QSPI_SMPR_FSDLY(sampling->delay_half_cycle ? 1U : 0U) |
		   QSPI_SMPR_FSPHS(sampling->phase_inverted ? 1U : 0U);
	REG_WRITE(QSPI_SMPR, reg_val);
}

// -------------------------------------------------------------------------------------------------

/* Delay after changing the value of the QSPI software reset bits */
#define QSPI_SOFTWARE_RESET_DELAY 276U

static void nxp_qspi_sw_reset(const struct memc_nxp_s32_qspi_config *config)
{
	volatile uint32_t delay_cnt;

	/* Software reset AHB domain and Serial Flash domain at the same time */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) | QSPI_MCR_SWRSTHD(1U) | QSPI_MCR_SWRSTSD(1U));

	/* Delay after changing the value of the reset bits */
	delay_cnt = QSPI_SOFTWARE_RESET_DELAY;
	while (delay_cnt-- > 0U)
		;

	/* Disable QSPI module before de-asserting the reset bits */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) | QSPI_MCR_MDIS(1U));

	/* De-asset Software reset AHB domain and Serial Flash domain bits */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) & ~(QSPI_MCR_SWRSTHD_MASK | QSPI_MCR_SWRSTSD_MASK));

	/* Re-enable QSPI module after reset */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) & ~QSPI_MCR_MDIS_MASK);

	/* Delay after changing the value of the reset bits */
	delay_cnt = QSPI_SOFTWARE_RESET_DELAY;
	while (delay_cnt-- > 0U)
		;
}

// -------------------------------------------------------------------------------------------------
// nxp_qspi_init_dll()

// static inline void Qspi_Ip_DLLSlaveUpdateA(struct memc_nxp_s32_qspi_config *config, bool enable)
// {
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_SLV_UPD_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_SLV_UPD(enable ? 1U : 0U));
// }

// static inline void Qspi_Ip_DLLenableA(struct memc_nxp_s32_qspi_config *config, bool enable)
// {
// #if defined(CONFIG_NXP_QSPI_HAS_DLLCRA_DLLEN)
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_DLLEN_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_DLLEN(enable ? 1U : 0U));
// #endif /* CONFIG_NXP_QSPI_HAS_DLLCRA_DLLEN */
// }

// static inline void Qspi_Ip_DLLSlaveEnA(struct memc_nxp_s32_qspi_config *config, bool enable)
// {
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_SLV_EN_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_SLV_EN(enable ? 1U : 0U));
// }

// static inline void Qspi_Ip_DLLSlaveBypassA(struct memc_nxp_s32_qspi_config *config, bool enable)
// {
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_SLV_DLL_BYPASS_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_SLV_DLL_BYPASS(enable ? 1U : 0U));
// }

// static inline void Qspi_Ip_DLLSlaveAutoUpdateA(struct memc_nxp_s32_qspi_config *config, bool
// enable)
// {
// #if defined(CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT)
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_SLAVE_AUTO_UPDT_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_SLAVE_AUTO_UPDT(enable ? 1U : 0U));
// #endif /* CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT */
// }

// static inline void Qspi_Ip_DLLSetCoarseDelayA(struct memc_nxp_s32_qspi_config *config,
// 					      uint8_t CoarseDelay)
// {
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_SLV_DLY_COARSE_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_SLV_DLY_COARSE(CoarseDelay));
// }

// static inline void Qspi_Ip_DLLSetFineOffsetA(struct memc_nxp_s32_qspi_config *config,
// 					     uint8_t FineDelay)
// {
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_SLV_FINE_OFFSET_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_SLV_FINE_OFFSET(FineDelay));
// }

// static inline void Qspi_Ip_DLLFreqEnA(struct memc_nxp_s32_qspi_config *config, bool enable)
// {
// 	uint32_t reg_val = REG_READ(QSPI_DLLCRA) & ~QSPI_DLLCRA_FREQEN_MASK;
// 	REG_WRITE(QSPI_DLLCRA, reg_val | QSPI_DLLCRA_FREQEN(enable ? 1U : 0U));
// }

// static inline bool Qspi_Ip_DLLGetSlaveLockStatusA(const struct memc_nxp_s32_qspi_config *config)
// {
// #if defined(CONFIG_NXP_QSPI_HAS_DLLSR_SLVA_LOCK)
// 	return FIELD_GET(QSPI_DLLSR_SLVA_LOCK_MASK, REG_READ(QSPI_DLLSR)) != 0U;
// #else
// 	ARG_UNUSED(config);
// 	return false;
// #endif /* CONFIG_NXP_QSPI_HAS_DLLSR_SLVA_LOCK */
// }

// static inline bool Qspi_Ip_DLLGetLockStatusA(const struct memc_nxp_s32_qspi_config *config)
// {
// #if defined(CONFIG_NXP_QSPI_HAS_DLLSR_DLLA_LOCK)
// 	return FIELD_GET(QSPI_DLLSR_DLLA_LOCK_MASK, REG_READ(QSPI_DLLSR)) != 0U;
// #else
// 	ARG_UNUSED(config);
// 	return false;
// #endif /* CONFIG_NXP_QSPI_HAS_DLLSR_DLLA_LOCK */
// }

// static inline bool Qspi_Ip_DLLGetErrorStatusA(const struct memc_nxp_s32_qspi_config *config)
// {
// #if defined(CONFIG_NXP_QSPI_HAS_DLLSR_DLLA_RANGE_ERR)
// 	uint32_t reg_val = REG_READ(QSPI_DLLSR);
// 	reg_val &= (QSPI_DLLSR_DLLA_RANGE_ERR_MASK | QSPI_DLLSR_DLLA_FINE_UNDERFLOW_MASK);
// 	return reg_val != 0U;
// #else
// 	ARG_UNUSED(config);
// 	return false;
// #endif /* CONFIG_NXP_QSPI_HAS_DLLSR_DLLA_RANGE_ERR */
// }

// static Qspi_Ip_StatusType Qspi_Ip_WaitDLLASlaveLock(struct memc_nxp_s32_qspi_config *config,
// 						    bool waitSlaveLock)
// {
// 	bool lock_status;

// /* Timeout for DLL lock sequence */
// #define QSPI_DLL_LOCK_TIMEOUT (2147483647U)

// 	// TODO: implement timeout on this loop
// 	do {
// 		/* Wait for slave high lock err or DLL lock err */
// 		lock_status = (waitSlaveLock) ? Qspi_Ip_DLLGetSlaveLockStatusA(config)
// 					      : Qspi_Ip_DLLGetLockStatusA(config);

// 		/* Check for errors reported by DLL */
// 		if (Qspi_Ip_DLLGetErrorStatusA(config)) {
// 			err = STATUS_QSPI_ERROR;
// 			break;
// 		}
// 	} while (!lock_status);

// 	return err;
// }

// static Qspi_Ip_StatusType Qspi_Ip_ConfigureDLLAByPass(struct memc_nxp_s32_qspi_config *config,
// 						      const struct nxp_qspi_controller *controller)
// {
// 	int err = 0;

// 	/* Set DLL in bypass mode and configure coarse and fine delays */
// 	Qspi_Ip_DLLSlaveBypassA(config, true);

// #if defined(CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT)
// 	Qspi_Ip_DLLSlaveAutoUpdateA(config, false);
// #endif /* CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT */
// 	Qspi_Ip_DLLSetCoarseDelayA(config, controller->dllSettingsA.coarseDelay);
// 	Qspi_Ip_DLLSetFineOffsetA(config, controller->dllSettingsA.fineDelay);
// 	Qspi_Ip_DLLFreqEnA(config, controller->dllSettingsA.freqEnable);

// 	/* Trigger slave chain update */
// 	Qspi_Ip_DLLSlaveUpdateA(config, true);

// 	/* Wait for slave delay chain update */
// 	err = Qspi_Ip_WaitDLLASlaveLock(config, true);

// 	/* Disable slave chain update */
// 	Qspi_Ip_DLLSlaveUpdateA(config, false);

// 	return err;
// }

// static Qspi_Ip_StatusType Qspi_Ip_ConfigureDLLAUpdate(struct memc_nxp_s32_qspi_config *config,
// 						      const struct nxp_qspi_controller *controller)
// {
// 	int err = 0;

// 	/* Set DLL in auto update mode and configure coarse and fine delays */
// 	Qspi_Ip_DLLSlaveBypassA(config, false);

// #if defined(CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT)
// 	Qspi_Ip_DLLSlaveAutoUpdateA(config,
// 				    (QSPI_DLL_AUTO_UPDATE == controller->dllSettingsA.dllMode));
// #endif /* CONFIG_NXP_QSPI_HAS_DLLCRA_SLAVE_AUTO_UPDT */

// 	Qspi_Ip_DLLSetReferenceCounterA(config, controller->dllSettingsA.referenceCounter);

// #if defined(CONFIG_NXP_QSPI_HAS_DLLCRA_DLLRES)
// 	Qspi_Ip_DLLSetResolutionA(config, controller->dllSettingsA.resolution);
// #endif /* CONFIG_NXP_QSPI_HAS_DLLCRA_DLLRES */

// 	Qspi_Ip_DLLSetCoarseOffsetA(config, controller->dllSettingsA.coarseDelay);
// 	Qspi_Ip_DLLSetFineOffsetA(config, controller->dllSettingsA.fineDelay);
// 	Qspi_Ip_DLLFreqEnA(config, controller->dllSettingsA.freqEnable);

// 	if (QSPI_DLL_AUTO_UPDATE == controller->dllSettingsA.dllMode) {
// 		/* For auto update mode, trigger slave chain update */
// 		Qspi_Ip_DLLSlaveUpdateA(config, true);
// 	}

// #if defined(CONFIG_NXP_QSPI_HAS_DLLCRA_DLLEN)
// 	/* Enable DLL */
// 	Qspi_Ip_DLLEnableA(config, true);
// #endif /* CONFIG_NXP_QSPI_HAS_DLLCRA_DLLEN */

// 	if (QSPI_DLL_MANUAL_UPDATE == controller->dllSettingsA.dllMode) {
// 		/* For manual update mode, wait for DLL lock before triggering slave chain update */
// 		err = Qspi_Ip_WaitDLLASlaveLock(config, false);
// 		Qspi_Ip_DLLSlaveUpdateA(config, true);
// 	}

// 	/* Wait for slave delay chain update */
// 	if (STATUS_QSPI_SUCCESS == err) {
// 		err = Qspi_Ip_WaitDLLASlaveLock(config, true);
// 	}

// 	/* Disable slave chain update */
// 	Qspi_Ip_DLLSlaveUpdateA(config, false);

// 	return err;
// }

// static Qspi_Ip_StatusType Qspi_Ip_ConfigureDLLA(struct memc_nxp_s32_qspi_config *config,
// 						const struct nxp_qspi_controller *controller)
// {
// 	int err;

// 	/* Ensure DLL and slave chain update are off */
// 	Qspi_Ip_DLLSlaveUpdateA(config, false);
// 	Qspi_Ip_DLLEnableA(config, false);

// 	/* Enable DQS slave delay chain before any settings take place */
// 	Qspi_Ip_DLLSlaveEnA(config, true);

// 	if (QSPI_DLL_BYPASSED == controller->dllSettingsA.dllMode) {
// 		err = Qspi_Ip_ConfigureDLLAByPass(config, controller);
// 	} else {
// 		/* QSPI_DLL_MANUAL_UPDATE or QSPI_DLL_AUTO_UPDATE */
// 		err = Qspi_Ip_ConfigureDLLAUpdate(config, controller);
// 	}

// 	return err;
// }

// static Qspi_Ip_StatusType nxp_qspi_init_dll(const struct memc_nxp_s32_qspi_config *config)
// {
// 	const struct nxp_qspi_controller *controller = config->controller;
// 	int err = 0;

// 	if ((controller->memSizeA1 + controller->memSizeA2) > 0U) {
// 		err = Qspi_Ip_ConfigureDLLA(controller);
// 	}

// 	return err;
// }

// -------------------------------------------------------------------------------------------------

static int memc_nxp_s32_qspi_init(const struct device *dev)
{
	const struct memc_nxp_s32_qspi_config *config = dev->config;
	int err;

	if (pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT)) {
		return -EIO;
	}

	/* Disable controller */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) | QSPI_MCR_MDIS(1U));

	nxp_qspi_config_ports(config);
	nxp_qspi_config_flash_address(config);
	nxp_qspi_config_sampling(config);
	nxp_qspi_config_cs(config);
	nxp_qspi_config_buffers(config);
	nxp_qspi_config_read_options(config);
	nxp_qspi_config_chip_options(config);

	/* Enable controller */
	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) & ~QSPI_MCR_MDIS_MASK);

	nxp_qspi_sw_reset(config);

	// err = nxp_qspi_init_dll(config);
	// if (err) {
	// 	REG_WRITE(QSPI_MCR, REG_READ(QSPI_MCR) | QSPI_MCR_MDIS(1U));
	// 	LOG_ERR("Fail to initialize QSPI controller (%d)", err);
	// 	return -EIO;
	// }

	/* Workaround to clear CRC and ECC errors flags */
	REG_WRITE(QSPI_FR, 0xFFFFFFFFUL);

	return err;
}

#define QSPI_DLL_CFG(n, side, side_upper)                                                          \
	IF_ENABLED(                                                                                \
		FEATURE_QSPI_HAS_DLL,                                                              \
		(.dllSettings##side_upper = {                                                      \
			 .dllMode = _CONCAT(QSPI_DLL_,                                             \
					    DT_INST_STRING_UPPER_TOKEN(n, side##_dll_mode)),       \
			 .freqEnable = DT_INST_PROP(n, side##_dll_freq_enable),                    \
			 .coarseDelay = DT_INST_PROP(n, side##_dll_coarse_delay),                  \
			 .fineDelay = DT_INST_PROP(n, side##_dll_fine_delay),                      \
			 .tapSelect = DT_INST_PROP(n, side##_dll_tap_select),                      \
			 IF_ENABLED(FEATURE_QSPI_DLL_LOOPCONTROL,                                  \
				    (.referenceCounter = DT_INST_PROP(n, side##_dll_ref_counter),  \
				     .resolution = DT_INST_PROP(n, side##_dll_resolution), ))}, ))

#define QSPI_READ_MODE(n, side, side_upper)                                                        \
	_CONCAT(QSPI_READ_MODE_, DT_INST_STRING_UPPER_TOKEN(n, side##_rx_clock_source))

#define QSPI_IDLE_SIGNAL_DRIVE(n, side, side_upper)                                                \
	IF_ENABLED(FEATURE_QSPI_CONFIGURABLE_ISD,                                                  \
		   (.io2_idle##side_upper = (uint8_t)DT_INST_PROP(n, side##_io2_idle_high),        \
		    .io3_idle##side_upper = (uint8_t)DT_INST_PROP(n, side##_io3_idle_high), ))

#define QSPI_PORT_SIZE_FN(node_id, side_upper, port)                                               \
	COND_CODE_1(IS_EQ(DT_REG_ADDR(node_id), QSPI_PCSF##side_upper##port),                      \
		    (COND_CODE_1(DT_NODE_HAS_STATUS(node_id, okay),                                \
				 (.memSize##side_upper##port = DT_PROP(node_id, size) / 8, ),      \
				 (.memSize##side_upper##port = 0, ))),                             \
		    (EMPTY))

#define QSPI_PORT_SIZE(n, side_upper)                                                              \
	DT_INST_FOREACH_CHILD_VARGS(n, QSPI_PORT_SIZE_FN, side_upper, 1)                           \
	DT_INST_FOREACH_CHILD_VARGS(n, QSPI_PORT_SIZE_FN, side_upper, 2)

#define QSPI_SIDE_CFG(n, side, side_upper)                                                         \
	QSPI_IDLE_SIGNAL_DRIVE(n, side, side_upper)                                                \
	QSPI_DLL_CFG(n, side, side_upper)                                                          \
	QSPI_PORT_SIZE(n, side_upper).readMode##side_upper = QSPI_READ_MODE(n, side, side_upper),

#define MEMC_NXP_S32_QSPI_CONTROLLER_CONFIG(n)                                                     \
	BUILD_ASSERT(_CONCAT(FEATURE_QSPI_, DT_INST_STRING_UPPER_TOKEN(n, a_rx_clock_source)) ==   \
			     1,                                                                    \
		     "a-rx-clock-source source mode selected is not supported");                   \
                                                                                                   \
	static const struct nxp_qspi_controller memc_nxp_s32_qspi_controller_cfg_##n = {           \
		.chip_options = DT_INST_PROP(n, chip_options),                                     \
		.ddr = (bool)DT_INST_ENUM_IDX(n, data_rate),                                       \
		.ahb_base = (mem_addr_t)DT_INST_REG_ADDR_BY_NAME(n, qspi_ahb),                     \
		.timing =                                                                          \
			{                                                                          \
				.aligned_2x_refclk = DT_INST_PROP(n, hold_time_2x),                \
				.cs =                                                              \
					{                                                          \
						.hold_time = DT_INST_PROP(n, cs_hold_time),        \
						.setup_time = DT_INST_PROP(n, cs_setup_time),      \
					},                                                         \
				.sampling =                                                        \
					{                                                          \
						.delay_half_cycle =                                \
							DT_INST_PROP(n, sample_delay_half_cycle),  \
						.phase_inverted =                                  \
							DT_INST_PROP(n, sample_phase_inverted),    \
					},                                                         \
			},                                                                         \
		.rx_buffers =                                                                      \
			{                                                                          \
				.buffers = DT_INST_PROP_OR(n, ahb_buffers, {0}),                   \
				.all_masters = (bool)DT_INST_PROP(n, ahb_buffers_all_masters),     \
			},                                                                         \
		.address =                                                                         \
			{                                                                          \
				.column_space = DT_INST_PROP_OR(n, column_space),                  \
				.word_addressable = DT_INST_PROP(n, word_addressable),             \
				.byte_swap = DT_INST_PROP(n, byte_swapping),                       \
			},                                                                         \
		.ports =                                                                           \
			[                                                                          \
				{},                                                                \
			],                                                                         \
		QSPI_SIDE_CFG(n, a, A),                                                            \
	}

#define MEMC_NXP_S32_QSPI_INIT_DEVICE(n)                                                           \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	MEMC_NXP_S32_QSPI_CONTROLLER_CONFIG(n);                                                    \
	static struct memc_nxp_s32_qspi_data memc_nxp_s32_qspi_data_##n;                           \
	static const struct memc_nxp_s32_qspi_config memc_nxp_s32_qspi_config_##n = {              \
		.base = (mem_addr_t)DT_INST_REG_ADDR_BY_NAME(n, qspi),                             \
		.controller = &memc_nxp_s32_qspi_controller_cfg_##n,                               \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, memc_nxp_s32_qspi_init, NULL, &memc_nxp_s32_qspi_data_##n,        \
			      &memc_nxp_s32_qspi_config_##n, POST_KERNEL,                          \
			      CONFIG_MEMC_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MEMC_NXP_S32_QSPI_INIT_DEVICE)
