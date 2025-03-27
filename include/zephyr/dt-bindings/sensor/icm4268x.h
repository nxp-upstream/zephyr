/*
 * Copyright (c) 2024 Intel Corporation
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_TDK_ICM4268XP_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_TDK_ICM4268XP_H_

/**
 * @defgroup ICM4268X Invensense (TDK) ICM4268X DT Options
 * @{
 */

/**
 * @defgroup ICM4268X_FIFO_MODES Fifo Modes
 * @{
 */
#define ICM4268X_DT_FIFO_MODE_BYPASS		0
#define ICM4268X_DT_FIFO_MODE_STREAM		1
#define ICM4268X_DT_FIFO_MODE_STOP_ON_FULL	3
/** @} */

/**
 * @defgroup ICM4268X_INT_MODES Interrupt Modes
 * @{
 */
#define ICM4268X_DT_INT1_MODE_PULSED	0
#define ICM4268X_DT_INT1_MODE_LATCHED	1
/** @} */

/**
 * @defgroup ICM4268X_INT_MODES Interrupt Modes
 * @{
 */
#define ICM4268X_DT_INT1_POLARITY_ACTIVE_LOW	0
#define ICM4268X_DT_INT1_POLARITY_ACTIVE_HIGH	1
/** @} */

/**
 * @defgroup ICM4268X_INT_FIFO_THS_CLR FIFO Threshold Intterupt
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_FIFO_THS_INT_CLR_STSRD	1
#define ICM4268X_DT_FIFO_THS_INT_CLR_DATARD	2
#define ICM4268X_DT_FIFO_THS_INT_CLR_BOTH	3
/** @} */

/**
 * @defgroup ICM4268X_INT_FIFO_FULL_CLR FIFO Full Intterupt
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_FIFO_FULL_INT_CLR_STSRD	1
#define ICM4268X_DT_FIFO_FULL_INT_CLR_DATARD	2
#define ICM4268X_DT_FIFO_FULL_INT_CLR_BOTH	3
/** @} */

/**
 * @defgroup ICM4268X_TMST_EN Timestamp enable
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_TMST_DIS	0
#define ICM4268X_DT_TMST_EN	1
/** @} */

/**
 * @defgroup ICM4268X_TMST_FSYNC_EN Timestamp fsync enable
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_TMST_FSYNC_DIS	0
#define ICM4268X_DT_TMST_FSYNC_EN	1
/** @} */

/**
 * @defgroup ICM4268X_TMST_DELTA_EN Timestamp delta enable
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_TMST_DELTA_DIS	0
#define ICM4268X_DT_TMST_DELTA_EN	1
/** @} */

/**
 * @defgroup ICM4268X_TMST_RES Timestamp resolution
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_TMST_RES_1US	0
#define ICM4268X_DT_TMST_RES_RTC	1
/** @} */

/**
 * @defgroup ICM4268X_TMST_TO_REGS_EN Timestamp register
 * Clearing conditions
 * @{
 */
#define ICM4268X_DT_TMST_REGS_DIS	0
#define ICM4268X_DT_TMST_REGS_EN	1
/** @} */

/**
 * @defgroup ICM4268X_ACCEL_POWER_MODES Accelerometer power modes
 * @{
 */
#define ICM4268X_DT_ACCEL_OFF		0
#define ICM4268X_DT_ACCEL_LP		2
#define ICM4268X_DT_ACCEL_LN		3
/** @} */

/**
 * @defgroup ICM4268X_GYRO_POWER_MODES Gyroscope power modes
 * @{
 */
#define ICM4268X_DT_GYRO_OFF		0
#define ICM4268X_DT_GYRO_STANDBY	1
#define ICM4268X_DT_GYRO_LN		3
/** @} */

/**
 * @defgroup ICM42686_ACCEL_SCALE Accelerometer scale options
 * @{
 */
#define ICM42686_DT_ACCEL_FS_32		0
#define ICM42686_DT_ACCEL_FS_16		1
#define ICM42686_DT_ACCEL_FS_8		2
#define ICM42686_DT_ACCEL_FS_4		3
#define ICM42686_DT_ACCEL_FS_2		4
/** @} */

/**
 * @defgroup ICM42688_ACCEL_SCALE Accelerometer scale options
 * @{
 */
#define ICM42688_DT_ACCEL_FS_16	0
#define ICM42688_DT_ACCEL_FS_8		1
#define ICM42688_DT_ACCEL_FS_4		2
#define ICM42688_DT_ACCEL_FS_2		3
/** @} */

/**
 * @defgroup ICM42686_GYRO_SCALE Gyroscope scale options
 * @{
 */
#define ICM42686_DT_GYRO_FS_4000	0
#define ICM42686_DT_GYRO_FS_2000	1
#define ICM42686_DT_GYRO_FS_1000	2
#define ICM42686_DT_GYRO_FS_500		3
#define ICM42686_DT_GYRO_FS_250		4
#define ICM42686_DT_GYRO_FS_125		5
#define ICM42686_DT_GYRO_FS_62_5	6
#define ICM42686_DT_GYRO_FS_31_25	7
/** @} */

/**
 * @defgroup ICM42688_GYRO_SCALE Gyroscope scale options
 * @{
 */
#define ICM42688_DT_GYRO_FS_2000		0
#define ICM42688_DT_GYRO_FS_1000		1
#define ICM42688_DT_GYRO_FS_500		2
#define ICM42688_DT_GYRO_FS_250		3
#define ICM42688_DT_GYRO_FS_125		4
#define ICM42688_DT_GYRO_FS_62_5		5
#define ICM42688_DT_GYRO_FS_31_25		6
#define ICM42688_DT_GYRO_FS_15_625		7
/** @} */

/**
 * @defgroup ICM4268X_ACCEL_DATA_RATE Accelerometer data rate options
 * @{
 */
#define ICM4268X_DT_ACCEL_ODR_32000	1
#define ICM4268X_DT_ACCEL_ODR_16000	2
#define ICM4268X_DT_ACCEL_ODR_8000	3
#define ICM4268X_DT_ACCEL_ODR_4000	4
#define ICM4268X_DT_ACCEL_ODR_2000	5
#define ICM4268X_DT_ACCEL_ODR_1000	6
#define ICM4268X_DT_ACCEL_ODR_200	7
#define ICM4268X_DT_ACCEL_ODR_100	8
#define ICM4268X_DT_ACCEL_ODR_50	9
#define ICM4268X_DT_ACCEL_ODR_25	10
#define ICM4268X_DT_ACCEL_ODR_12_5	11
#define ICM4268X_DT_ACCEL_ODR_6_25	12
#define ICM4268X_DT_ACCEL_ODR_3_125	13
#define ICM4268X_DT_ACCEL_ODR_1_5625	14
#define ICM4268X_DT_ACCEL_ODR_500	15
/** @} */

/**
 * @defgroup ICM4268X_GYRO_DATA_RATE Gyroscope data rate options
 * @{
 */
#define ICM4268X_DT_GYRO_ODR_32000	1
#define ICM4268X_DT_GYRO_ODR_16000	2
#define ICM4268X_DT_GYRO_ODR_8000	3
#define ICM4268X_DT_GYRO_ODR_4000	4
#define ICM4268X_DT_GYRO_ODR_2000	5
#define ICM4268X_DT_GYRO_ODR_1000	6
#define ICM4268X_DT_GYRO_ODR_200	7
#define ICM4268X_DT_GYRO_ODR_100	8
#define ICM4268X_DT_GYRO_ODR_50		9
#define ICM4268X_DT_GYRO_ODR_25		10
#define ICM4268X_DT_GYRO_ODR_12_5	11
#define ICM4268X_DT_GYRO_ODR_500	15
/** @} */

/**
 * @defgroup ICM4268X_ACCEL_AAF_BW Accelerometer Anti Aliasing Filter Bandwidth options
 * @{
 */
#define ICM4268X_DT_ACCEL_AAF_42HZ       1
#define ICM4268X_DT_ACCEL_AAF_84HZ       2
#define ICM4268X_DT_ACCEL_AAF_126HZ      3
#define ICM4268X_DT_ACCEL_AAF_170HZ      4
#define ICM4268X_DT_ACCEL_AAF_213HZ      5
#define ICM4268X_DT_ACCEL_AAF_258HZ      6
#define ICM4268X_DT_ACCEL_AAF_303HZ      7
#define ICM4268X_DT_ACCEL_AAF_348HZ      8
#define ICM4268X_DT_ACCEL_AAF_394HZ      9
#define ICM4268X_DT_ACCEL_AAF_441HZ      10
#define ICM4268X_DT_ACCEL_AAF_488HZ      11
#define ICM4268X_DT_ACCEL_AAF_536HZ      12
#define ICM4268X_DT_ACCEL_AAF_585HZ      13
#define ICM4268X_DT_ACCEL_AAF_634HZ      14
#define ICM4268X_DT_ACCEL_AAF_684HZ      15
#define ICM4268X_DT_ACCEL_AAF_734HZ      16
#define ICM4268X_DT_ACCEL_AAF_785HZ      17
#define ICM4268X_DT_ACCEL_AAF_837HZ      18
#define ICM4268X_DT_ACCEL_AAF_890HZ      19
#define ICM4268X_DT_ACCEL_AAF_943HZ      20
#define ICM4268X_DT_ACCEL_AAF_997HZ      21
#define ICM4268X_DT_ACCEL_AAF_1051HZ     22
#define ICM4268X_DT_ACCEL_AAF_1107HZ     23
#define ICM4268X_DT_ACCEL_AAF_1163HZ     24
#define ICM4268X_DT_ACCEL_AAF_1220HZ     25
#define ICM4268X_DT_ACCEL_AAF_1277HZ     26
#define ICM4268X_DT_ACCEL_AAF_1336HZ     27
#define ICM4268X_DT_ACCEL_AAF_1395HZ     28
#define ICM4268X_DT_ACCEL_AAF_1454HZ     29
#define ICM4268X_DT_ACCEL_AAF_1515HZ     30
#define ICM4268X_DT_ACCEL_AAF_1577HZ     31
#define ICM4268X_DT_ACCEL_AAF_1639HZ     32
#define ICM4268X_DT_ACCEL_AAF_1702HZ     33
#define ICM4268X_DT_ACCEL_AAF_1766HZ     34
#define ICM4268X_DT_ACCEL_AAF_1830HZ     35
#define ICM4268X_DT_ACCEL_AAF_1896HZ     36
#define ICM4268X_DT_ACCEL_AAF_1962HZ     37
#define ICM4268X_DT_ACCEL_AAF_2029HZ     38
#define ICM4268X_DT_ACCEL_AAF_2097HZ     39
#define ICM4268X_DT_ACCEL_AAF_2166HZ     40
#define ICM4268X_DT_ACCEL_AAF_2235HZ     41
#define ICM4268X_DT_ACCEL_AAF_2306HZ     42
#define ICM4268X_DT_ACCEL_AAF_2377HZ     43
#define ICM4268X_DT_ACCEL_AAF_2449HZ     44
#define ICM4268X_DT_ACCEL_AAF_2522HZ     45
#define ICM4268X_DT_ACCEL_AAF_2596HZ     46
#define ICM4268X_DT_ACCEL_AAF_2671HZ     47
#define ICM4268X_DT_ACCEL_AAF_2746HZ     48
#define ICM4268X_DT_ACCEL_AAF_2823HZ     49
#define ICM4268X_DT_ACCEL_AAF_2900HZ     50
#define ICM4268X_DT_ACCEL_AAF_2978HZ     51
#define ICM4268X_DT_ACCEL_AAF_3057HZ     52
#define ICM4268X_DT_ACCEL_AAF_3137HZ     53
#define ICM4268X_DT_ACCEL_AAF_3217HZ     54
#define ICM4268X_DT_ACCEL_AAF_3299HZ     55
#define ICM4268X_DT_ACCEL_AAF_3381HZ     56
#define ICM4268X_DT_ACCEL_AAF_3464HZ     57
#define ICM4268X_DT_ACCEL_AAF_3548HZ     58
#define ICM4268X_DT_ACCEL_AAF_3633HZ     59
#define ICM4268X_DT_ACCEL_AAF_3718HZ     60
#define ICM4268X_DT_ACCEL_AAF_3805HZ     61
#define ICM4268X_DT_ACCEL_AAF_3892HZ     62
#define ICM4268X_DT_ACCEL_AAF_3979HZ     63
/** @} */

/**
 * @defgroup ICM4268X_GYRO_AAF_BW Gyroscope Anti Aliasing Filter Bandwidth options
 * @{
 */
#define ICM4268X_DT_GYRO_AAF_42HZ       1
#define ICM4268X_DT_GYRO_AAF_84HZ       2
#define ICM4268X_DT_GYRO_AAF_126HZ      3
#define ICM4268X_DT_GYRO_AAF_170HZ      4
#define ICM4268X_DT_GYRO_AAF_213HZ      5
#define ICM4268X_DT_GYRO_AAF_258HZ      6
#define ICM4268X_DT_GYRO_AAF_303HZ      7
#define ICM4268X_DT_GYRO_AAF_348HZ      8
#define ICM4268X_DT_GYRO_AAF_394HZ      9
#define ICM4268X_DT_GYRO_AAF_441HZ      10
#define ICM4268X_DT_GYRO_AAF_488HZ      11
#define ICM4268X_DT_GYRO_AAF_536HZ      12
#define ICM4268X_DT_GYRO_AAF_585HZ      13
#define ICM4268X_DT_GYRO_AAF_634HZ      14
#define ICM4268X_DT_GYRO_AAF_684HZ      15
#define ICM4268X_DT_GYRO_AAF_734HZ      16
#define ICM4268X_DT_GYRO_AAF_785HZ      17
#define ICM4268X_DT_GYRO_AAF_837HZ      18
#define ICM4268X_DT_GYRO_AAF_890HZ      19
#define ICM4268X_DT_GYRO_AAF_943HZ      20
#define ICM4268X_DT_GYRO_AAF_997HZ      21
#define ICM4268X_DT_GYRO_AAF_1051HZ     22
#define ICM4268X_DT_GYRO_AAF_1107HZ     23
#define ICM4268X_DT_GYRO_AAF_1163HZ     24
#define ICM4268X_DT_GYRO_AAF_1220HZ     25
#define ICM4268X_DT_GYRO_AAF_1277HZ     26
#define ICM4268X_DT_GYRO_AAF_1336HZ     27
#define ICM4268X_DT_GYRO_AAF_1395HZ     28
#define ICM4268X_DT_GYRO_AAF_1454HZ     29
#define ICM4268X_DT_GYRO_AAF_1515HZ     30
#define ICM4268X_DT_GYRO_AAF_1577HZ     31
#define ICM4268X_DT_GYRO_AAF_1639HZ     32
#define ICM4268X_DT_GYRO_AAF_1702HZ     33
#define ICM4268X_DT_GYRO_AAF_1766HZ     34
#define ICM4268X_DT_GYRO_AAF_1830HZ     35
#define ICM4268X_DT_GYRO_AAF_1896HZ     36
#define ICM4268X_DT_GYRO_AAF_1962HZ     37
#define ICM4268X_DT_GYRO_AAF_2029HZ     38
#define ICM4268X_DT_GYRO_AAF_2097HZ     39
#define ICM4268X_DT_GYRO_AAF_2166HZ     40
#define ICM4268X_DT_GYRO_AAF_2235HZ     41
#define ICM4268X_DT_GYRO_AAF_2306HZ     42
#define ICM4268X_DT_GYRO_AAF_2377HZ     43
#define ICM4268X_DT_GYRO_AAF_2449HZ     44
#define ICM4268X_DT_GYRO_AAF_2522HZ     45
#define ICM4268X_DT_GYRO_AAF_2596HZ     46
#define ICM4268X_DT_GYRO_AAF_2671HZ     47
#define ICM4268X_DT_GYRO_AAF_2746HZ     48
#define ICM4268X_DT_GYRO_AAF_2823HZ     49
#define ICM4268X_DT_GYRO_AAF_2900HZ     50
#define ICM4268X_DT_GYRO_AAF_2978HZ     51
#define ICM4268X_DT_GYRO_AAF_3057HZ     52
#define ICM4268X_DT_GYRO_AAF_3137HZ     53
#define ICM4268X_DT_GYRO_AAF_3217HZ     54
#define ICM4268X_DT_GYRO_AAF_3299HZ     55
#define ICM4268X_DT_GYRO_AAF_3381HZ     56
#define ICM4268X_DT_GYRO_AAF_3464HZ     57
#define ICM4268X_DT_GYRO_AAF_3548HZ     58
#define ICM4268X_DT_GYRO_AAF_3633HZ     59
#define ICM4268X_DT_GYRO_AAF_3718HZ     60
#define ICM4268X_DT_GYRO_AAF_3805HZ     61
#define ICM4268X_DT_GYRO_AAF_3892HZ     62
#define ICM4268X_DT_GYRO_AAF_3979HZ     63
/** @} */

/** @} */
#endif /*ZEPHYR_INCLUDE_DT_BINDINGS_TDK_ICM4268XP_H_ */
