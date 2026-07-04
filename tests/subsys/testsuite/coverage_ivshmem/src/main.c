/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

/*
 * A few branchy helpers so that, when built with coverage, there is real gcov
 * data to carry out of the guest through the ivshmem region.
 */
static int classify(int value)
{
	if (value < 0) {
		return -1;
	} else if (value == 0) {
		return 0;
	}
	return 1;
}

static int sum_evens(const int *values, size_t count)
{
	int sum = 0;

	for (size_t i = 0; i < count; i++) {
		if ((values[i] % 2) == 0) {
			sum += values[i];
		}
	}

	return sum;
}

ZTEST(coverage_ivshmem, test_classify)
{
	zassert_equal(classify(-5), -1);
	zassert_equal(classify(0), 0);
	zassert_equal(classify(7), 1);
}

ZTEST(coverage_ivshmem, test_sum_evens)
{
	const int values[] = {1, 2, 3, 4, 5, 6};

	zassert_equal(sum_evens(values, ARRAY_SIZE(values)), 12);
	zassert_equal(sum_evens(NULL, 0), 0);
}

ZTEST_SUITE(coverage_ivshmem, NULL, NULL, NULL, NULL, NULL);
