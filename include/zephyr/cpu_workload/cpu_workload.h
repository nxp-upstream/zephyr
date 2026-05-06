/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_CPU_WORKLOAD_H_
#define ZEPHYR_INCLUDE_CPU_WORKLOAD_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CPU Workload
 * @defgroup subsys_cpu_workload CPU Workload
 * @since 4.4
 * @version 0.1.0
 * @ingroup os_services
 * @{
 */

enum cpu_workload_source {
	/** CPU workload signal used per-thread burst profiles. */
	CPU_WORKLOAD_SOURCE_THREAD_BURST_PROFILE = BIT(0),
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_CPU_WORKLOAD_H_ */
