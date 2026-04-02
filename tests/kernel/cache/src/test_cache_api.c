/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/cache.h>

#define SIZE	(4096)

static ZTEST_BMEM uint8_t user_buffer[SIZE];

static void assert_cache_query_result(int ret)
{
	zassert_true((ret == 0) || (ret == 1) || (ret == -ENOTSUP));
}

ZTEST(cache_api, test_instr_cache_api)
{
	int ret;

#ifdef CONFIG_XTENSA_MMU
	/* With MMU enabled, user_buffer is not marked as executable.
	 * Invalidating the i-cache by region will cause an instruction
	 * fetch prohibited exception. So skip all i-cache tests,
	 * instead of just the range ones to avoid confusions of
	 * only running the test partially.
	 */
	ztest_test_skip();
#endif

	ret = sys_cache_instr_flush_all();
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_instr_invd_all();
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_instr_flush_and_invd_all();
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_instr_flush_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_instr_invd_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_instr_flush_and_invd_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));
}

ZTEST(cache_api, test_instr_cache_state_query_api)
{
	int ret;

	ret = sys_cache_instr_is_enabled();
	assert_cache_query_result(ret);

	if (ret == -ENOTSUP) {
		return;
	}

	sys_cache_instr_disable();
	zassert_equal(sys_cache_instr_is_enabled(), 0);

	sys_cache_instr_enable();
	zassert_equal(sys_cache_instr_is_enabled(), 1);
}

ZTEST(cache_api, test_data_cache_api)
{
	int ret;

	ret = sys_cache_data_flush_all();
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_data_flush_and_invd_all();
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_data_flush_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_data_invd_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_data_flush_and_invd_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

}

ZTEST(cache_api, test_data_cache_state_query_api)
{
	int ret;

	ret = sys_cache_data_is_enabled();
	assert_cache_query_result(ret);

	if (ret == -ENOTSUP) {
		return;
	}

	sys_cache_data_disable();
	zassert_equal(sys_cache_data_is_enabled(), 0);

	sys_cache_data_enable();
	zassert_equal(sys_cache_data_is_enabled(), 1);
}

ZTEST_USER(cache_api, test_data_cache_api_user)
{
	int ret;

	ret = sys_cache_data_flush_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_data_invd_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));

	ret = sys_cache_data_flush_and_invd_range(user_buffer, SIZE);
	zassert_true((ret == 0) || (ret == -ENOTSUP));
}

static void *cache_api_setup(void)
{
	sys_cache_data_disable();
	sys_cache_data_flush_all();
	sys_cache_data_enable();
	sys_cache_instr_enable();

	return NULL;
}

static void cache_api_teardown(void *unused)
{
	sys_cache_data_disable();
	sys_cache_instr_disable();
}

ZTEST_SUITE(cache_api, NULL, cache_api_setup, NULL, NULL, cache_api_teardown);
