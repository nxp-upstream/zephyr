/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Matches definition of Z_DEVICE_DT_DEV_ID */
#define CLOCK_DEV_ID(node_id) _CONCAT(dts_ord_, DT_DEP_ORD(node_id))

/* Defines required symbols for a clock callback section */
#define CLOCK_clock_cb_LD_SECTION(node_id) \
	_CONCAT(CLOCK_DEV_ID(node_id), _clock_cb_start) = .; \
	KEEP(*(_CONCAT(CLOCK_DEV_ID(node_id), _clock_cb))) \
	_CONCAT(CLOCK_DEV_ID(node_id), _clock_cb_end) = .; \
	_CONCAT(CLOCK_DEV_ID(node_id), _clock_cb_size) = \
	_CONCAT(CLOCK_DEV_ID(node_id), _clock_cb_end) - \
	_CONCAT(CLOCK_DEV_ID(node_id), _clock_cb_start); \


clock_callbacks_start = .;
/* Link all used clock management callback structures here */
DT_FOREACH_CLOCK_USED(CLOCK_clock_cb_LD_SECTION)
clock_callbacks_end = .;
