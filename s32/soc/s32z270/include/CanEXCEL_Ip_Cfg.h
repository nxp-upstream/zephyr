/*
 * Copyright 2022-2024 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CANEXCEL_IP_CFG_H_
#define CANEXCEL_IP_CFG_H_
/**
*   @file CanEXCEL_Ip_Cfg.h
*
*   @addtogroup CanEXCEL
*   @{
*/

#ifdef __cplusplus
extern "C"{
#endif
/*==================================================================================================
*                                        INCLUDE FILES
* 1) system and project includes
* 2) needed interfaces from external units
* 3) internal and external interfaces from this unit
==================================================================================================*/
#include "CanEXCEL_Ip_Sa_Init_PBcfg.h"
#include "OsIf.h"
#include "Reg_eSys.h"
/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define CANEXCEL_IP_CFG_VENDOR_ID_H                      43
#define CANEXCEL_IP_CFG_AR_RELEASE_MAJOR_VERSION_H       4
#define CANEXCEL_IP_CFG_AR_RELEASE_MINOR_VERSION_H       7
#define CANEXCEL_IP_CFG_AR_RELEASE_REVISION_VERSION_H    0
#define CANEXCEL_IP_CFG_SW_MAJOR_VERSION_H               2
#define CANEXCEL_IP_CFG_SW_MINOR_VERSION_H               0
#define CANEXCEL_IP_CFG_SW_PATCH_VERSION_H               0
/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* External Structures generated by CanEXCEL_Ip_PBCfg */
#define CANEXCEL_IP_CONFIG_EXT \
    CANEXCEL_IP_INIT_SA_PB_CFG
#define CANEXCEL_IP_STATE_EXT    CANEXCEL_IP_INIT_SA_STATE_PB_CFG
/* Time out value in uS */
#define CANEXCEL_IP_TIMEOUT_DURATION (10000)
/* This this will set the timer source for osif that will be used for timeout */
#define CANEXCEL_IP_SERVICE_TIMEOUT_TYPE (OSIF_COUNTER_DUMMY)
#define CANEXCEL_IP_DEV_ERROR_DETECT (STD_OFF)
#define CANEXCEL_IP_HAS_TS_ENABLE (STD_ON)
#define CANEXCEL_IP_MSGDESC_MAX_DEPTH (16U)
#define CANEXCEL_IP_MAX_FILTER_BANK (32U)
#define CANEXCEL_IP_ERROR_INTERRUPT_SUPPORT (STD_ON)
#define CANEXCEL_IP_USE_FRZ_IRQ (STD_ON)
#define CANEXCEL_IP_FEATURE_HAS_MSGDESC_RXCTRL_MODE (STD_ON)
#define CANEXCEL_IP_HAS_SYSLOCK01 (STD_OFF)

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* CANEXCEL_IP_CFG_H_ */