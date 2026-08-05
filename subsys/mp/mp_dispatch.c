/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/check.h>

#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_structure.h>

void mp_dispatch_init(struct mp_dispatch *dispatch, uint8_t type, const struct mp_structure *caps)
{
	if (dispatch == NULL) {
		return;
	}

	memset(dispatch, 0, sizeof(*dispatch));
	dispatch->type = type;

	if (caps == NULL) {
		mp_structure_init_any(&dispatch->caps);
	} else {
		mp_structure_duplicate(caps, &dispatch->caps);
	}
}

void mp_dispatch_clear(struct mp_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return;
	}

	memset(dispatch, 0, sizeof(*dispatch));
}

struct mp_structure *mp_dispatch_get_caps(struct mp_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return NULL;
	}

	return &dispatch->caps;
}

int mp_dispatch_set_caps(struct mp_dispatch *dispatch, const struct mp_structure *caps)
{
	if (dispatch == NULL) {
		return -EINVAL;
	}

	if (caps == &dispatch->caps) {
		return 0;
	}

	if (caps == NULL) {
		return mp_structure_init_any(&dispatch->caps);
	}

	return mp_structure_duplicate(caps, &dispatch->caps);
}

int mp_dispatch_set_pool(struct mp_dispatch *dispatch, struct mp_buffer_pool *pool)
{
	if (dispatch == NULL) {
		return -EINVAL;
	}

	dispatch->pool = pool;

	return 0;
}

int mp_dispatch_set_pool_config(struct mp_dispatch *dispatch, struct mp_buffer_pool_config *config)
{
	if (dispatch == NULL || config == NULL) {
		return -EINVAL;
	}

	dispatch->pool_cfg = *config;

	return 0;
}

struct mp_buffer_pool *mp_dispatch_get_pool(struct mp_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return NULL;
	}

	return dispatch->pool;
}

struct mp_buffer_pool_config *mp_dispatch_get_pool_config(struct mp_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return NULL;
	}

	return &dispatch->pool_cfg;
}
