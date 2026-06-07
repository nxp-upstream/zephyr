/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp
 * @brief Simple wrapper of k_thread to reuse a thread's stack after its termination
 */

#ifndef ZEPHYR_INCLUDE_MP_CORE_MP_THREAD_H_
#define ZEPHYR_INCLUDE_MP_CORE_MP_THREAD_H_

#include <zephyr/kernel/thread.h>

/**
 *
 * @{
 */

/**
 * @brief Simple wrapper around k_thread
 */
struct mp_thread {
	/** k_thread struct */
	struct k_thread thread;
	/** Thread stack ID */
	uint8_t stack_id;
	/** Flag to get the thread status or to exit the thread's function loop */
	bool running;
};

/**
 * @brief Create a new thread reusing the stack from the thread pool
 *
 * @param thread Pointer to an uninitialized struct @ref mp_thread
 * @param func Entry function of the thread
 * @param p1 First parameter to pass to the thread entry function
 * @param p2 Second parameter to pass to the thread entry function
 * @param p3 Third parameter to pass to the thread entry function
 * @param priority Priority of the thread
 * @return ID of the newly created thread on success or NULL on failure
 */
k_tid_t mp_thread_create(struct mp_thread *thread, k_thread_entry_t func, void *p1, void *p2,
			 void *p3, int priority);

/**
 * @brief Join a thread and release its stack back to the thread stack pool
 *
 * @param thread Pointer to a struct @ref mp_thread to join
 * @param timeout Maximum time to wait for the thread to join
 * @return 0 on success or a negative errno on failure
 */
int mp_thread_join(struct mp_thread *thread, k_timeout_t timeout);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_CORE_MP_THREAD_H_ */
