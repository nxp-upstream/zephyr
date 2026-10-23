/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef POWER_QUERIES_STAY_AWAKE_H_
#define POWER_QUERIES_STAY_AWAKE_H_

/* ZTEST_SUITE() setup: keeps the CPU out of every power state until the teardown. */
void *stay_awake_setup(void);

/* ZTEST_SUITE() teardown: lets the policy select power states again. */
void stay_awake_teardown(void *fixture);

#endif /* POWER_QUERIES_STAY_AWAKE_H_ */
