/*
 *  Copyright 2020-2022 NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _WIFI_CONFIG_H_
#define _WIFI_CONFIG_H_

#define CONFIG_WIFI_MAX_PRIO (configMAX_PRIORITIES - 1)

#ifndef RW610
#define CONFIG_MAX_AP_ENTRIES 10
#endif

#if defined(SD8978) || defined(SD8987) || defined(RW610)
#define CONFIG_MAX_AP_ENTRIES 30
#endif

#if defined(SD8977) || defined(SD8978) || defined(SD8987) || defined(IW61x) || defined(RW610)
#define CONFIG_5GHz_SUPPORT 1
#endif

#ifndef RW610
#define CONFIG_SDIO_MULTI_PORT_RX_AGGR 1
#endif

#if defined(SD8987) || defined(RW610) || defined(IW61x)
#define CONFIG_11AC
#undef CONFIG_WMM
#endif

#if defined(RW610)
#define PRINTF_FLOAT_ENABLE 1
#define CONFIG_11AX
#undef CONFIG_IMU_GDMA
/* WMM options */
#define CONFIG_WMM
#define CONFIG_WMM_ENH
#undef CONFIG_WMM_CERT
#undef AMSDU_IN_AMPDU
/* OWE mode */
#undef CONFIG_OWE
/* WLAN SCAN OPT */
#define CONFIG_SCAN_WITH_RSSIFILTER
/* WLAN white/black list opt */
#define CONFIG_UAP_STA_MAC_ADDR_FILTER
#define CONFIG_COMBO_SCAN
#define CONFIG_WIFI_DTIM_PERIOD
#define CONFIG_UART_INTERRUPT
#define CONFIG_WIFI_CAPA
#define CONFIG_WIFI_RTS_THRESHOLD
#define CONFIG_WIFI_FRAG_THRESHOLD
#define CONFIG_WIFI_11D_ENABLE
#define CONFIG_WIFI_HIDDEN_SSID
#define CONFIG_WIFI_MAX_CLIENTS_CNT
#define CONFIG_WMM_UAPSD
#define CONFIG_WIFI_GET_LOG
#define CONFIG_WIFI_TX_PER_TRACK
#define CONFIG_ROAMING
#define CONFIG_HOST_SLEEP
#ifdef CONFIG_POWER_MANAGER
#undef CONFIG_POWER_MANAGER
#endif
#define CONFIG_CSI
#define CONFIG_WIFI_RESET
#define CONFIG_NET_MONITOR
#define CONFIG_WIFI_MEM_ACCESS
#define CONFIG_WIFI_REG_ACCESS
#define CONFIG_ECSA
#define CONFIG_WIFI_EU_CRYPTO
#define CONFIG_EXT_SCAN_SUPPORT
#define CONFIG_EVENT_MEM_ACCESS
#define CONFIG_11K
#define CONFIG_11V
#define CONFIG_COMPRESS_TX_PWTBL
#define CONFIG_RX_ABORT_CFG
#define CONFIG_CCK_DESENSE_CFG
#define CONFIG_11AX_TWT
#define CONFIG_IPS
#define CONFIG_MBO
#define WIFI_ADD_ON 1
#define CONFIG_UNII4_BAND_SUPPORT
#endif

// #define CONFIG_IPV6               1
// #define CONFIG_MAX_IPV6_ADDRESSES 5

#if defined(IW61x)
#define CONFIG_11AX             1
#define CONFIG_EXT_SCAN_SUPPORT 1
#define CONFIG_OWE              1
#undef CONFIG_WIFI_CAPA
#undef CONFIG_UNII4_BAND_SUPPORT
#endif

/* Logs */
#define CONFIG_ENABLE_ERROR_LOGS   1
#define CONFIG_ENABLE_WARNING_LOGS 1

/* WLCMGR debug */
#undef CONFIG_WLCMGR_DEBUG

/*
 * Wifi extra debug options
 */
#undef CONFIG_WIFI_EXTRA_DEBUG
#undef CONFIG_WIFI_EVENTS_DEBUG
#undef CONFIG_WIFI_CMD_RESP_DEBUG
#undef CONFIG_WIFI_PKT_DEBUG
#undef CONFIG_WIFI_SCAN_DEBUG
#undef CONFIG_WIFI_IO_INFO_DUMP
#undef CONFIG_WIFI_IO_DEBUG
#undef CONFIG_WIFI_IO_DUMP
#undef CONFIG_WIFI_MEM_DEBUG
#undef CONFIG_WIFI_AMPDU_DEBUG
#undef CONFIG_WIFI_TIMER_DEBUG
#undef CONFIG_WIFI_SDIO_DEBUG
#undef CONFIG_WIFI_FW_DEBUG
#undef CONFIG_WPS_DEBUG

/*
 * Heap debug options
 */
#undef CONFIG_HEAP_DEBUG
#undef CONFIG_HEAP_STAT

/*
 * Heap debug options
 */
#undef CONFIG_HEAP_DEBUG
#undef CONFIG_HEAP_STAT

#endif /* _WIFI_CONFIG_H_ */