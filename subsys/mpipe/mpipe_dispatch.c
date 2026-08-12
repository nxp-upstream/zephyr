/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/check.h>

#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_structure.h>

void mpipe_dispatch_init(struct mpipe_dispatch *dispatch, uint8_t type,
			 const struct mpipe_structure *caps)
{
	if (dispatch == NULL) {
		return;
	}

	memset(dispatch, 0, sizeof(*dispatch));
	dispatch->type = type;

	if (caps == NULL) {
		mpipe_structure_init_any(&dispatch->caps);
	} else {
		mpipe_structure_duplicate(caps, &dispatch->caps);
	}
}

void mpipe_dispatch_clear(struct mpipe_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return;
	}

	memset(dispatch, 0, sizeof(*dispatch));
}

struct mpipe_structure *mpipe_dispatch_get_caps(struct mpipe_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return NULL;
	}

	return &dispatch->caps;
}

int mpipe_dispatch_set_caps(struct mpipe_dispatch *dispatch, const struct mpipe_structure *caps)
{
	if (dispatch == NULL) {
		return -EINVAL;
	}

	if (caps == &dispatch->caps) {
		return 0;
	}

	if (caps == NULL) {
		return mpipe_structure_init_any(&dispatch->caps);
	}

	return mpipe_structure_duplicate(caps, &dispatch->caps);
}

int mpipe_dispatch_set_pool(struct mpipe_dispatch *dispatch, struct mpipe_buffer_pool *pool)
{
	if (dispatch == NULL) {
		return -EINVAL;
	}

	dispatch->pool = pool;

	return 0;
}

int mpipe_dispatch_set_pool_config(struct mpipe_dispatch *dispatch,
				   struct mpipe_buffer_pool_config *config)
{
	if (dispatch == NULL || config == NULL) {
		return -EINVAL;
	}

	dispatch->pool_cfg = *config;

	return 0;
}

struct mpipe_buffer_pool *mpipe_dispatch_get_pool(struct mpipe_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return NULL;
	}

	return dispatch->pool;
}

struct mpipe_buffer_pool_config *mpipe_dispatch_get_pool_config(struct mpipe_dispatch *dispatch)
{
	if (dispatch == NULL) {
		return NULL;
	}

	return &dispatch->pool_cfg;
}
