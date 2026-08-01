/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_structure.h>

ZTEST_SUITE(mp_buffer_api, NULL, NULL, NULL, NULL, NULL);

static struct mp_buffer_pool pool;

ZTEST(mp_buffer_api, test_sanity)
{
	struct mp_structure config;

	zassert_ok(mp_structure_init(&config, MP_MEDIA_AUDIO_PCM), "config init failed");

	mp_buffer_pool_init(&pool);
	zassert_false(pool.started, "pool.started != false after init");

	zassert_true(mp_buffer_pool_configure(NULL, &config) < 0,
		     "configure NULL pool did not fail");

	zassert_true(mp_buffer_pool_configure(&pool, &config) < 0,
		     "configure(no callback) did not fail");

	zassert_true(mp_buffer_pool_start(NULL) < 0, "start NULL pool did not fail");

	pool.start = NULL;
	zassert_true(mp_buffer_pool_start(&pool) < 0, "start no callback did not fail");

	zassert_true(mp_buffer_pool_stop(NULL) < 0, "stop NULL pool did not fail");

	pool.stop = NULL;
	zassert_true(mp_buffer_pool_stop(&pool) < 0, "stop no callback did not fail");
}
