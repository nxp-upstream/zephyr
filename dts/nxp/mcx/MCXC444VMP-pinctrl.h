/*
 * NOTE: Autogenerated file by gen_soc_headers.py
 * for MCXC444VMP/signal_configuration.xml
 *
 * Copyright 2024 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ZEPHYR_DTS_BINDING_MCXC444VMP_
#define _ZEPHYR_DTS_BINDING_MCXC444VMP_

#define KINETIS_MUX(port, pin, mux)		\
	(((((port) - 'A') & 0xF) << 28) |	\
	(((pin) & 0x3F) << 22) |		\
	(((mux) & 0x7) << 8))

#define PTA0 KINETIS_MUX('A',0,1) /* PTA_0 */
#define TPM0_CH5_PTA0 KINETIS_MUX('A',0,3) /* PTA_0 */
#define SWD_CLK_PTA0 KINETIS_MUX('A',0,7) /* PTA_0 */
#define PTA1 KINETIS_MUX('A',1,1) /* PTA_1 */
#define LPUART0_RX_PTA1 KINETIS_MUX('A',1,2) /* PTA_1 */
#define TPM2_CH0_PTA1 KINETIS_MUX('A',1,3) /* PTA_1 */
#define PTA2 KINETIS_MUX('A',2,1) /* PTA_2 */
#define LPUART0_TX_PTA2 KINETIS_MUX('A',2,2) /* PTA_2 */
#define TPM2_CH1_PTA2 KINETIS_MUX('A',2,3) /* PTA_2 */
#define PTA3 KINETIS_MUX('A',3,1) /* PTA_3 */
#define I2C1_SCL_PTA3 KINETIS_MUX('A',3,2) /* PTA_3 */
#define TPM0_CH0_PTA3 KINETIS_MUX('A',3,3) /* PTA_3 */
#define SWD_DIO_PTA3 KINETIS_MUX('A',3,7) /* PTA_3 */
#define PTA4 KINETIS_MUX('A',4,1) /* PTA_4 */
#define I2C1_SDA_PTA4 KINETIS_MUX('A',4,2) /* PTA_4 */
#define TPM0_CH1_PTA4 KINETIS_MUX('A',4,3) /* PTA_4 */
#define NMI_b_PTA4 KINETIS_MUX('A',4,7) /* PTA_4 */
#define PTA5 KINETIS_MUX('A',5,1) /* PTA_5 */
#define USB_CLKIN_PTA5 KINETIS_MUX('A',5,2) /* PTA_5 */
#define TPM0_CH2_PTA5 KINETIS_MUX('A',5,3) /* PTA_5 */
#define I2S0_TX_BCLK_PTA5 KINETIS_MUX('A',5,6) /* PTA_5 */
#define PTA12 KINETIS_MUX('A',12,1) /* PTA_12 */
#define TPM1_CH0_PTA12 KINETIS_MUX('A',12,3) /* PTA_12 */
#define I2S0_TXD0_PTA12 KINETIS_MUX('A',12,6) /* PTA_12 */
#define PTA13 KINETIS_MUX('A',13,1) /* PTA_13 */
#define TPM1_CH1_PTA13 KINETIS_MUX('A',13,3) /* PTA_13 */
#define I2S0_TX_FS_PTA13 KINETIS_MUX('A',13,6) /* PTA_13 */
#define EXTAL0_PTA18 KINETIS_MUX('A',18,0) /* PTA_18 */
#define PTA18 KINETIS_MUX('A',18,1) /* PTA_18 */
#define LPUART1_RX_PTA18 KINETIS_MUX('A',18,3) /* PTA_18 */
#define TPM_CLKIN0_PTA18 KINETIS_MUX('A',18,4) /* PTA_18 */
#define XTAL0_PTA19 KINETIS_MUX('A',19,0) /* PTA_19 */
#define PTA19 KINETIS_MUX('A',19,1) /* PTA_19 */
#define LPUART1_TX_PTA19 KINETIS_MUX('A',19,3) /* PTA_19 */
#define TPM_CLKIN1_PTA19 KINETIS_MUX('A',19,4) /* PTA_19 */
#define LPTMR0_ALT1_PTA19 KINETIS_MUX('A',19,6) /* PTA_19 */
#define PTA20 KINETIS_MUX('A',20,1) /* PTA_20 */
#define RESET_b_PTA20 KINETIS_MUX('A',20,7) /* PTA_20 */
#define ADC0_SE8_PTB0 KINETIS_MUX('B',0,0) /* PTB_0 */
#define LCD_P0_PTB0 KINETIS_MUX('B',0,0) /* PTB_0 */
#define LLWU_P5_PTB0 KINETIS_MUX('B',0,1) /* PTB_0 */
#define PTB0 KINETIS_MUX('B',0,1) /* PTB_0 */
#define I2C0_SCL_PTB0 KINETIS_MUX('B',0,2) /* PTB_0 */
#define TPM1_CH0_PTB0 KINETIS_MUX('B',0,3) /* PTB_0 */
#define LCD_P0_Fault_PTB0 KINETIS_MUX('B',0,7) /* PTB_0 */
#define ADC0_SE9_PTB1 KINETIS_MUX('B',1,0) /* PTB_1 */
#define LCD_P1_PTB1 KINETIS_MUX('B',1,0) /* PTB_1 */
#define PTB1 KINETIS_MUX('B',1,1) /* PTB_1 */
#define I2C0_SDA_PTB1 KINETIS_MUX('B',1,2) /* PTB_1 */
#define TPM1_CH1_PTB1 KINETIS_MUX('B',1,3) /* PTB_1 */
#define LCD_P1_Fault_PTB1 KINETIS_MUX('B',1,7) /* PTB_1 */
#define ADC0_SE12_PTB2 KINETIS_MUX('B',2,0) /* PTB_2 */
#define LCD_P2_PTB2 KINETIS_MUX('B',2,0) /* PTB_2 */
#define PTB2 KINETIS_MUX('B',2,1) /* PTB_2 */
#define I2C0_SCL_PTB2 KINETIS_MUX('B',2,2) /* PTB_2 */
#define TPM2_CH0_PTB2 KINETIS_MUX('B',2,3) /* PTB_2 */
#define LCD_P2_Fault_PTB2 KINETIS_MUX('B',2,7) /* PTB_2 */
#define ADC0_SE13_PTB3 KINETIS_MUX('B',3,0) /* PTB_3 */
#define LCD_P3_PTB3 KINETIS_MUX('B',3,0) /* PTB_3 */
#define PTB3 KINETIS_MUX('B',3,1) /* PTB_3 */
#define I2C0_SDA_PTB3 KINETIS_MUX('B',3,2) /* PTB_3 */
#define TPM2_CH1_PTB3 KINETIS_MUX('B',3,3) /* PTB_3 */
#define LCD_P3_Fault_PTB3 KINETIS_MUX('B',3,7) /* PTB_3 */
#define LCD_P12_PTB16 KINETIS_MUX('B',16,0) /* PTB_16 */
#define PTB16 KINETIS_MUX('B',16,1) /* PTB_16 */
#define SPI1_MOSI_PTB16 KINETIS_MUX('B',16,2) /* PTB_16 */
#define LPUART0_RX_PTB16 KINETIS_MUX('B',16,3) /* PTB_16 */
#define TPM_CLKIN0_PTB16 KINETIS_MUX('B',16,4) /* PTB_16 */
#define SPI1_MISO_PTB16 KINETIS_MUX('B',16,5) /* PTB_16 */
#define LCD_P12_Fault_PTB16 KINETIS_MUX('B',16,7) /* PTB_16 */
#define LCD_P13_PTB17 KINETIS_MUX('B',17,0) /* PTB_17 */
#define PTB17 KINETIS_MUX('B',17,1) /* PTB_17 */
#define SPI1_MISO_PTB17 KINETIS_MUX('B',17,2) /* PTB_17 */
#define LPUART0_TX_PTB17 KINETIS_MUX('B',17,3) /* PTB_17 */
#define TPM_CLKIN1_PTB17 KINETIS_MUX('B',17,4) /* PTB_17 */
#define SPI1_MOSI_PTB17 KINETIS_MUX('B',17,5) /* PTB_17 */
#define LCD_P13_Fault_PTB17 KINETIS_MUX('B',17,7) /* PTB_17 */
#define LCD_P14_PTB18 KINETIS_MUX('B',18,0) /* PTB_18 */
#define PTB18 KINETIS_MUX('B',18,1) /* PTB_18 */
#define TPM2_CH0_PTB18 KINETIS_MUX('B',18,3) /* PTB_18 */
#define I2S0_TX_BCLK_PTB18 KINETIS_MUX('B',18,4) /* PTB_18 */
#define LCD_P14_Fault_PTB18 KINETIS_MUX('B',18,7) /* PTB_18 */
#define LCD_P15_PTB19 KINETIS_MUX('B',19,0) /* PTB_19 */
#define PTB19 KINETIS_MUX('B',19,1) /* PTB_19 */
#define TPM2_CH1_PTB19 KINETIS_MUX('B',19,3) /* PTB_19 */
#define I2S0_TX_FS_PTB19 KINETIS_MUX('B',19,4) /* PTB_19 */
#define LCD_P15_Fault_PTB19 KINETIS_MUX('B',19,7) /* PTB_19 */
#define ADC0_SE14_PTC0 KINETIS_MUX('C',0,0) /* PTC_0 */
#define LCD_P20_PTC0 KINETIS_MUX('C',0,0) /* PTC_0 */
#define PTC0 KINETIS_MUX('C',0,1) /* PTC_0 */
#define EXTRG_IN_PTC0 KINETIS_MUX('C',0,3) /* PTC_0 */
#define USB_SOF_OUT_PTC0 KINETIS_MUX('C',0,4) /* PTC_0 */
#define CMP0_OUT_PTC0 KINETIS_MUX('C',0,5) /* PTC_0 */
#define I2S0_TXD0_PTC0 KINETIS_MUX('C',0,6) /* PTC_0 */
#define LCD_P20_Fault_PTC0 KINETIS_MUX('C',0,7) /* PTC_0 */
#define LCD_P21_PTC1 KINETIS_MUX('C',1,0) /* PTC_1 */
#define ADC0_SE15_PTC1 KINETIS_MUX('C',1,0) /* PTC_1 */
#define RTC_CLKIN_PTC1 KINETIS_MUX('C',1,1) /* PTC_1 */
#define PTC1 KINETIS_MUX('C',1,1) /* PTC_1 */
#define LLWU_P6_PTC1 KINETIS_MUX('C',1,1) /* PTC_1 */
#define I2C1_SCL_PTC1 KINETIS_MUX('C',1,2) /* PTC_1 */
#define TPM0_CH0_PTC1 KINETIS_MUX('C',1,4) /* PTC_1 */
#define I2S0_TXD0_PTC1 KINETIS_MUX('C',1,6) /* PTC_1 */
#define LCD_P21_Fault_PTC1 KINETIS_MUX('C',1,7) /* PTC_1 */
#define ADC0_SE11_PTC2 KINETIS_MUX('C',2,0) /* PTC_2 */
#define LCD_P22_PTC2 KINETIS_MUX('C',2,0) /* PTC_2 */
#define PTC2 KINETIS_MUX('C',2,1) /* PTC_2 */
#define I2C1_SDA_PTC2 KINETIS_MUX('C',2,2) /* PTC_2 */
#define TPM0_CH1_PTC2 KINETIS_MUX('C',2,4) /* PTC_2 */
#define I2S0_TX_FS_PTC2 KINETIS_MUX('C',2,6) /* PTC_2 */
#define LCD_P22_Fault_PTC2 KINETIS_MUX('C',2,7) /* PTC_2 */
#define LCD_P23_PTC3 KINETIS_MUX('C',3,0) /* PTC_3 */
#define PTC3 KINETIS_MUX('C',3,1) /* PTC_3 */
#define LLWU_P7_PTC3 KINETIS_MUX('C',3,1) /* PTC_3 */
#define SPI1_SCK_PTC3 KINETIS_MUX('C',3,2) /* PTC_3 */
#define LPUART1_RX_PTC3 KINETIS_MUX('C',3,3) /* PTC_3 */
#define TPM0_CH2_PTC3 KINETIS_MUX('C',3,4) /* PTC_3 */
#define CLKOUT_PTC3 KINETIS_MUX('C',3,5) /* PTC_3 */
#define I2S0_TX_BCLK_PTC3 KINETIS_MUX('C',3,6) /* PTC_3 */
#define LCD_P23_Fault_PTC3 KINETIS_MUX('C',3,7) /* PTC_3 */
#define LCD_P24_PTC4 KINETIS_MUX('C',4,0) /* PTC_4 */
#define PTC4 KINETIS_MUX('C',4,1) /* PTC_4 */
#define LLWU_P8_PTC4 KINETIS_MUX('C',4,1) /* PTC_4 */
#define SPI0_SS_PTC4 KINETIS_MUX('C',4,2) /* PTC_4 */
#define LPUART1_TX_PTC4 KINETIS_MUX('C',4,3) /* PTC_4 */
#define TPM0_CH3_PTC4 KINETIS_MUX('C',4,4) /* PTC_4 */
#define I2S0_MCLK_PTC4 KINETIS_MUX('C',4,5) /* PTC_4 */
#define LCD_P24_Fault_PTC4 KINETIS_MUX('C',4,7) /* PTC_4 */
#define LCD_P25_PTC5 KINETIS_MUX('C',5,0) /* PTC_5 */
#define PTC5 KINETIS_MUX('C',5,1) /* PTC_5 */
#define LLWU_P9_PTC5 KINETIS_MUX('C',5,1) /* PTC_5 */
#define SPI0_SCK_PTC5 KINETIS_MUX('C',5,2) /* PTC_5 */
#define LPTMR0_ALT2_PTC5 KINETIS_MUX('C',5,3) /* PTC_5 */
#define I2S0_RXD0_PTC5 KINETIS_MUX('C',5,4) /* PTC_5 */
#define CMP0_OUT_PTC5 KINETIS_MUX('C',5,6) /* PTC_5 */
#define LCD_P25_Fault_PTC5 KINETIS_MUX('C',5,7) /* PTC_5 */
#define LCD_P26_PTC6 KINETIS_MUX('C',6,0) /* PTC_6 */
#define CMP0_IN0_PTC6 KINETIS_MUX('C',6,0) /* PTC_6 */
#define LLWU_P10_PTC6 KINETIS_MUX('C',6,1) /* PTC_6 */
#define PTC6 KINETIS_MUX('C',6,1) /* PTC_6 */
#define SPI0_MOSI_PTC6 KINETIS_MUX('C',6,2) /* PTC_6 */
#define EXTRG_IN_PTC6 KINETIS_MUX('C',6,3) /* PTC_6 */
#define I2S0_RX_BCLK_PTC6 KINETIS_MUX('C',6,4) /* PTC_6 */
#define SPI0_MISO_PTC6 KINETIS_MUX('C',6,5) /* PTC_6 */
#define I2S0_MCLK_PTC6 KINETIS_MUX('C',6,6) /* PTC_6 */
#define LCD_P26_Fault_PTC6 KINETIS_MUX('C',6,7) /* PTC_6 */
#define LCD_P27_PTC7 KINETIS_MUX('C',7,0) /* PTC_7 */
#define CMP0_IN1_PTC7 KINETIS_MUX('C',7,0) /* PTC_7 */
#define PTC7 KINETIS_MUX('C',7,1) /* PTC_7 */
#define SPI0_MISO_PTC7 KINETIS_MUX('C',7,2) /* PTC_7 */
#define USB_SOF_OUT_PTC7 KINETIS_MUX('C',7,3) /* PTC_7 */
#define I2S0_RX_FS_PTC7 KINETIS_MUX('C',7,4) /* PTC_7 */
#define SPI0_MOSI_PTC7 KINETIS_MUX('C',7,5) /* PTC_7 */
#define LCD_P27_Fault_PTC7 KINETIS_MUX('C',7,7) /* PTC_7 */
#define VLL2_PTC20 KINETIS_MUX('C',20,0) /* PTC_20 */
#define LCD_P4_PTC20 KINETIS_MUX('C',20,0) /* PTC_20 */
#define PTC20 KINETIS_MUX('C',20,1) /* PTC_20 */
#define LCD_P4_Fault_PTC20 KINETIS_MUX('C',20,7) /* PTC_20 */
#define LCD_P5_PTC21 KINETIS_MUX('C',21,0) /* PTC_21 */
#define VLL1_PTC21 KINETIS_MUX('C',21,0) /* PTC_21 */
#define PTC21 KINETIS_MUX('C',21,1) /* PTC_21 */
#define LCD_P5_Fault_PTC21 KINETIS_MUX('C',21,7) /* PTC_21 */
#define VCAP2_PTC22 KINETIS_MUX('C',22,0) /* PTC_22 */
#define LCD_P6_PTC22 KINETIS_MUX('C',22,0) /* PTC_22 */
#define PTC22 KINETIS_MUX('C',22,1) /* PTC_22 */
#define LCD_P6_Fault_PTC22 KINETIS_MUX('C',22,7) /* PTC_22 */
#define LCD_P39_PTC23 KINETIS_MUX('C',23,0) /* PTC_23 */
#define VCAP1_PTC23 KINETIS_MUX('C',23,0) /* PTC_23 */
#define PTC23 KINETIS_MUX('C',23,1) /* PTC_23 */
#define LCD_P39_Fault_PTC23 KINETIS_MUX('C',23,7) /* PTC_23 */
#define LCD_P40_PTD0 KINETIS_MUX('D',0,0) /* PTD_0 */
#define PTD0 KINETIS_MUX('D',0,1) /* PTD_0 */
#define SPI0_SS_PTD0 KINETIS_MUX('D',0,2) /* PTD_0 */
#define TPM0_CH0_PTD0 KINETIS_MUX('D',0,4) /* PTD_0 */
#define FXIO0_D0_PTD0 KINETIS_MUX('D',0,6) /* PTD_0 */
#define LCD_P40_Fault_PTD0 KINETIS_MUX('D',0,7) /* PTD_0 */
#define LCD_P41_PTD1 KINETIS_MUX('D',1,0) /* PTD_1 */
#define ADC0_SE5b_PTD1 KINETIS_MUX('D',1,0) /* PTD_1 */
#define PTD1 KINETIS_MUX('D',1,1) /* PTD_1 */
#define SPI0_SCK_PTD1 KINETIS_MUX('D',1,2) /* PTD_1 */
#define TPM0_CH1_PTD1 KINETIS_MUX('D',1,4) /* PTD_1 */
#define FXIO0_D1_PTD1 KINETIS_MUX('D',1,6) /* PTD_1 */
#define LCD_P41_Fault_PTD1 KINETIS_MUX('D',1,7) /* PTD_1 */
#define LCD_P42_PTD2 KINETIS_MUX('D',2,0) /* PTD_2 */
#define PTD2 KINETIS_MUX('D',2,1) /* PTD_2 */
#define SPI0_MOSI_PTD2 KINETIS_MUX('D',2,2) /* PTD_2 */
#define UART2_RX_PTD2 KINETIS_MUX('D',2,3) /* PTD_2 */
#define TPM0_CH2_PTD2 KINETIS_MUX('D',2,4) /* PTD_2 */
#define SPI0_MISO_PTD2 KINETIS_MUX('D',2,5) /* PTD_2 */
#define FXIO0_D2_PTD2 KINETIS_MUX('D',2,6) /* PTD_2 */
#define LCD_P42_Fault_PTD2 KINETIS_MUX('D',2,7) /* PTD_2 */
#define LCD_P43_PTD3 KINETIS_MUX('D',3,0) /* PTD_3 */
#define PTD3 KINETIS_MUX('D',3,1) /* PTD_3 */
#define SPI0_MISO_PTD3 KINETIS_MUX('D',3,2) /* PTD_3 */
#define UART2_TX_PTD3 KINETIS_MUX('D',3,3) /* PTD_3 */
#define TPM0_CH3_PTD3 KINETIS_MUX('D',3,4) /* PTD_3 */
#define SPI0_MOSI_PTD3 KINETIS_MUX('D',3,5) /* PTD_3 */
#define FXIO0_D3_PTD3 KINETIS_MUX('D',3,6) /* PTD_3 */
#define LCD_P43_Fault_PTD3 KINETIS_MUX('D',3,7) /* PTD_3 */
#define LCD_P44_PTD4 KINETIS_MUX('D',4,0) /* PTD_4 */
#define PTD4 KINETIS_MUX('D',4,1) /* PTD_4 */
#define LLWU_P14_PTD4 KINETIS_MUX('D',4,1) /* PTD_4 */
#define SPI1_SS_PTD4 KINETIS_MUX('D',4,2) /* PTD_4 */
#define UART2_RX_PTD4 KINETIS_MUX('D',4,3) /* PTD_4 */
#define TPM0_CH4_PTD4 KINETIS_MUX('D',4,4) /* PTD_4 */
#define FXIO0_D4_PTD4 KINETIS_MUX('D',4,6) /* PTD_4 */
#define LCD_P44_Fault_PTD4 KINETIS_MUX('D',4,7) /* PTD_4 */
#define LCD_P45_PTD5 KINETIS_MUX('D',5,0) /* PTD_5 */
#define ADC0_SE6b_PTD5 KINETIS_MUX('D',5,0) /* PTD_5 */
#define PTD5 KINETIS_MUX('D',5,1) /* PTD_5 */
#define SPI1_SCK_PTD5 KINETIS_MUX('D',5,2) /* PTD_5 */
#define UART2_TX_PTD5 KINETIS_MUX('D',5,3) /* PTD_5 */
#define TPM0_CH5_PTD5 KINETIS_MUX('D',5,4) /* PTD_5 */
#define FXIO0_D5_PTD5 KINETIS_MUX('D',5,6) /* PTD_5 */
#define LCD_P45_Fault_PTD5 KINETIS_MUX('D',5,7) /* PTD_5 */
#define ADC0_SE7b_PTD6 KINETIS_MUX('D',6,0) /* PTD_6 */
#define LCD_P46_PTD6 KINETIS_MUX('D',6,0) /* PTD_6 */
#define LLWU_P15_PTD6 KINETIS_MUX('D',6,1) /* PTD_6 */
#define PTD6 KINETIS_MUX('D',6,1) /* PTD_6 */
#define SPI1_MOSI_PTD6 KINETIS_MUX('D',6,2) /* PTD_6 */
#define LPUART0_RX_PTD6 KINETIS_MUX('D',6,3) /* PTD_6 */
#define SPI1_MISO_PTD6 KINETIS_MUX('D',6,5) /* PTD_6 */
#define FXIO0_D6_PTD6 KINETIS_MUX('D',6,6) /* PTD_6 */
#define LCD_P46_Fault_PTD6 KINETIS_MUX('D',6,7) /* PTD_6 */
#define LCD_P47_PTD7 KINETIS_MUX('D',7,0) /* PTD_7 */
#define PTD7 KINETIS_MUX('D',7,1) /* PTD_7 */
#define SPI1_MISO_PTD7 KINETIS_MUX('D',7,2) /* PTD_7 */
#define LPUART0_TX_PTD7 KINETIS_MUX('D',7,3) /* PTD_7 */
#define SPI1_MOSI_PTD7 KINETIS_MUX('D',7,5) /* PTD_7 */
#define FXIO0_D7_PTD7 KINETIS_MUX('D',7,6) /* PTD_7 */
#define LCD_P47_Fault_PTD7 KINETIS_MUX('D',7,7) /* PTD_7 */
#define LCD_P48_PTE0 KINETIS_MUX('E',0,0) /* PTE_0 */
#define CLKOUT32K_PTE0 KINETIS_MUX('E',0,1) /* PTE_0 */
#define PTE0 KINETIS_MUX('E',0,1) /* PTE_0 */
#define SPI1_MISO_PTE0 KINETIS_MUX('E',0,2) /* PTE_0 */
#define LPUART1_TX_PTE0 KINETIS_MUX('E',0,3) /* PTE_0 */
#define RTC_CLKOUT_PTE0 KINETIS_MUX('E',0,4) /* PTE_0 */
#define CMP0_OUT_PTE0 KINETIS_MUX('E',0,5) /* PTE_0 */
#define I2C1_SDA_PTE0 KINETIS_MUX('E',0,6) /* PTE_0 */
#define LCD_P48_Fault_PTE0 KINETIS_MUX('E',0,7) /* PTE_0 */
#define LCD_P49_PTE1 KINETIS_MUX('E',1,0) /* PTE_1 */
#define PTE1 KINETIS_MUX('E',1,1) /* PTE_1 */
#define SPI1_MOSI_PTE1 KINETIS_MUX('E',1,2) /* PTE_1 */
#define LPUART1_RX_PTE1 KINETIS_MUX('E',1,3) /* PTE_1 */
#define SPI1_MISO_PTE1 KINETIS_MUX('E',1,5) /* PTE_1 */
#define I2C1_SCL_PTE1 KINETIS_MUX('E',1,6) /* PTE_1 */
#define LCD_P49_Fault_PTE1 KINETIS_MUX('E',1,7) /* PTE_1 */
#define LCD_P59_PTE20 KINETIS_MUX('E',20,0) /* PTE_20 */
#define ADC0_SE0_PTE20 KINETIS_MUX('E',20,0) /* PTE_20 */
#define ADC0_DP0_PTE20 KINETIS_MUX('E',20,0) /* PTE_20 */
#define PTE20 KINETIS_MUX('E',20,1) /* PTE_20 */
#define TPM1_CH0_PTE20 KINETIS_MUX('E',20,3) /* PTE_20 */
#define LPUART0_TX_PTE20 KINETIS_MUX('E',20,4) /* PTE_20 */
#define FXIO0_D4_PTE20 KINETIS_MUX('E',20,6) /* PTE_20 */
#define LCD_P59_Fault_PTE20 KINETIS_MUX('E',20,7) /* PTE_20 */
#define LCD_P60_PTE21 KINETIS_MUX('E',21,0) /* PTE_21 */
#define ADC0_DM0_PTE21 KINETIS_MUX('E',21,0) /* PTE_21 */
#define ADC0_SE4a_PTE21 KINETIS_MUX('E',21,0) /* PTE_21 */
#define PTE21 KINETIS_MUX('E',21,1) /* PTE_21 */
#define TPM1_CH1_PTE21 KINETIS_MUX('E',21,3) /* PTE_21 */
#define LPUART0_RX_PTE21 KINETIS_MUX('E',21,4) /* PTE_21 */
#define FXIO0_D5_PTE21 KINETIS_MUX('E',21,6) /* PTE_21 */
#define LCD_P60_Fault_PTE21 KINETIS_MUX('E',21,7) /* PTE_21 */
#define ADC0_SE3_PTE22 KINETIS_MUX('E',22,0) /* PTE_22 */
#define ADC0_DP3_PTE22 KINETIS_MUX('E',22,0) /* PTE_22 */
#define PTE22 KINETIS_MUX('E',22,1) /* PTE_22 */
#define TPM2_CH0_PTE22 KINETIS_MUX('E',22,3) /* PTE_22 */
#define UART2_TX_PTE22 KINETIS_MUX('E',22,4) /* PTE_22 */
#define FXIO0_D6_PTE22 KINETIS_MUX('E',22,6) /* PTE_22 */
#define ADC0_DM3_PTE23 KINETIS_MUX('E',23,0) /* PTE_23 */
#define ADC0_SE7a_PTE23 KINETIS_MUX('E',23,0) /* PTE_23 */
#define PTE23 KINETIS_MUX('E',23,1) /* PTE_23 */
#define TPM2_CH1_PTE23 KINETIS_MUX('E',23,3) /* PTE_23 */
#define UART2_RX_PTE23 KINETIS_MUX('E',23,4) /* PTE_23 */
#define FXIO0_D7_PTE23 KINETIS_MUX('E',23,6) /* PTE_23 */
#define PTE24 KINETIS_MUX('E',24,1) /* PTE_24 */
#define TPM0_CH0_PTE24 KINETIS_MUX('E',24,3) /* PTE_24 */
#define I2C0_SCL_PTE24 KINETIS_MUX('E',24,5) /* PTE_24 */
#define PTE25 KINETIS_MUX('E',25,1) /* PTE_25 */
#define TPM0_CH1_PTE25 KINETIS_MUX('E',25,3) /* PTE_25 */
#define I2C0_SDA_PTE25 KINETIS_MUX('E',25,5) /* PTE_25 */
#define CMP0_IN5_PTE29 KINETIS_MUX('E',29,0) /* PTE_29 */
#define ADC0_SE4b_PTE29 KINETIS_MUX('E',29,0) /* PTE_29 */
#define PTE29 KINETIS_MUX('E',29,1) /* PTE_29 */
#define TPM0_CH2_PTE29 KINETIS_MUX('E',29,3) /* PTE_29 */
#define TPM_CLKIN0_PTE29 KINETIS_MUX('E',29,4) /* PTE_29 */
#define CMP0_IN4_PTE30 KINETIS_MUX('E',30,0) /* PTE_30 */
#define ADC0_SE23_PTE30 KINETIS_MUX('E',30,0) /* PTE_30 */
#define DAC0_OUT_PTE30 KINETIS_MUX('E',30,0) /* PTE_30 */
#define PTE30 KINETIS_MUX('E',30,1) /* PTE_30 */
#define TPM0_CH3_PTE30 KINETIS_MUX('E',30,3) /* PTE_30 */
#define TPM_CLKIN1_PTE30 KINETIS_MUX('E',30,4) /* PTE_30 */
#define LPUART1_TX_PTE30 KINETIS_MUX('E',30,5) /* PTE_30 */
#define LPTMR0_ALT1_PTE30 KINETIS_MUX('E',30,6) /* PTE_30 */
#define PTE31 KINETIS_MUX('E',31,1) /* PTE_31 */
#define TPM0_CH4_PTE31 KINETIS_MUX('E',31,3) /* PTE_31 */
#endif
