/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp
 * @brief Thread Management header file.
 */

#ifndef ZEPHYR_INCLUDE_MP_CORE_MP_THREAD_H_
#define ZEPHYR_INCLUDE_MP_CORE_MP_THREAD_H_

#include <stdbool.h>

#include <zephyr/kernel/thread.h>

/**
 *
 * @{
 */

/**
 * @struct mp_thread
 * @brief Structure that represents a thread in the system
 */
struct mp_thread {
	/** Thread data */
	struct k_thread thread_data;
	/** Flag to indicate thread status */
	bool running;
	/** Thread stack ID */
	int8_t stack_id;
};

/**
 * Create a new thread
 *
 * @param thread: pointer to thread structure
 * @param func: entry function for the thread
 * @param p1: first additional parameter to pass to the thread entry function
 * @param p2: second additional parameter to pass to the thread entry function
 * @param p3: third additional parameter to pass to the thread entry function
 * @param priority: priority of the thread
 * @return k_tid_t which is the pointer to the k_thread structure
 */
k_tid_t mp_thread_create(struct mp_thread *thread, k_thread_entry_t func, void *p1, void *p2,
			 void *p3, int priority);

/**
 * Release a thread
 *
 * @param thread: pointer to thread structure
 */
void mp_thread_release(struct mp_thread *thread);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_CORE_MP_THREAD_H_ */
