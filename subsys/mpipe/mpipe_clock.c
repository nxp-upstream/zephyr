/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <zephyr/mpipe/mpipe_clock.h>

static uint64_t mpipe_clock_monotonic_get_time(struct mpipe_clock *clock)
{
	ARG_UNUSED(clock);

	return k_ticks_to_us_floor64((uint64_t)k_uptime_ticks());
}

static const struct mpipe_clock_ops mpipe_clock_monotonic_ops = {
	.get_time = mpipe_clock_monotonic_get_time,
};

static struct mpipe_clock mpipe_clock_monotonic_instance = {
	.ops = &mpipe_clock_monotonic_ops,
	.priv = NULL,
};

uint64_t mpipe_clock_get_time(struct mpipe_clock *clock)
{
	if (clock == NULL || clock->ops == NULL || clock->ops->get_time == NULL) {
		return 0;
	}

	return clock->ops->get_time(clock);
}

struct mpipe_clock *mpipe_clock_monotonic(void)
{
	return &mpipe_clock_monotonic_instance;
}
