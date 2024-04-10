/**
 * @file
 * @brief Clock Management Devicetree macro public API header file.
 */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DEVICETREE_CLOCK_MGMT_H_
#define ZEPHYR_INCLUDE_DEVICETREE_CLOCK_MGMT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/devicetree.h>

/**
 * @defgroup devicetree-clock-mgmt Devicetree Clock Management API
 * @ingroup devicetree
 * @{
 */

/**
 * @brief Number of clock management states for a node identifier
 * Gets the number of clock management states for a given node identifier
 * @param node_id Node identifier to get the number of states for
 */
#define DT_NUM_CLOCK_MGMT_STATES(node_id) \
	DT_CAT(node_id, _CLOCK_STATE_NUM)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif


#endif  /* ZEPHYR_INCLUDE_DEVICETREE_CLOCK_MGMT_H_ */
