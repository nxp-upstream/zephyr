/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_structure.h>

ZTEST_SUITE(mpipe_buffer_api, NULL, NULL, NULL, NULL, NULL);

static struct mpipe_buffer_pool pool;

ZTEST(mpipe_buffer_api, test_sanity)
{
	struct mpipe_structure config;

	zassert_ok(mpipe_structure_init(&config, MPIPE_MEDIA_AUDIO_PCM), "config init failed");

	mpipe_buffer_pool_init(&pool);
	zassert_false(pool.started, "pool.started != false after init");

	zassert_true(mpipe_buffer_pool_configure(&pool, &config) < 0,
		     "configure(no callback) did not fail");

	pool.start = NULL;
	zassert_true(mpipe_buffer_pool_start(&pool) < 0, "start no callback did not fail");

	pool.stop = NULL;
	zassert_true(mpipe_buffer_pool_stop(&pool) < 0, "stop no callback did not fail");
}
