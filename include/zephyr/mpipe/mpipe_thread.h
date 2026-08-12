/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Simple wrapper of k_thread to reuse a thread's stack after its termination
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_THREAD_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_THREAD_H_

/**
 * @defgroup mpipe_thread Threads
 * @ingroup mpipe_framework
 * @brief Thread helpers used by runtime components.
 *
 * @{
 */

#include <zephyr/kernel/thread.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Thread state
 */
enum mpipe_thread_state {
	/** Thread is paused and can be resumed */
	MPIPE_THREAD_PAUSED = 0,
	/** Thread is running */
	MPIPE_THREAD_RUNNING,
	/** Thread is terminated */
	MPIPE_THREAD_TERMINATED,
};

/**
 * @brief Simple wrapper around k_thread
 */
struct mpipe_thread {
	/** k_thread struct */
	struct k_thread thread;
	/** Semaphore used to gate run / pause transitions */
	struct k_sem sem;
	/** Thread stack ID */
	uint8_t stack_id;
	/** Current thread state */
	atomic_t state;
};

/**
 * @brief Create a new thread reusing the stack from the thread pool
 *
 * The thread is created in a sleeping state (K_FOREVER delay).  Call
 * mpipe_thread_resume() to actually start execution.
 *
 * @param thread Pointer to an uninitialized struct @ref mpipe_thread
 * @param func Entry function of the thread
 * @param p1 First parameter to pass to the thread entry function
 * @param p2 Second parameter to pass to the thread entry function
 * @param p3 Third parameter to pass to the thread entry function
 * @param priority Priority of the thread
 * @param delay Scheduling delay, or K_NO_WAIT
 * @return ID of the newly created thread on success or NULL on failure
 */
k_tid_t mpipe_thread_create(struct mpipe_thread *thread, k_thread_entry_t func, void *p1, void *p2,
			    void *p3, int priority, k_timeout_t delay);

/**
 * @brief Block the thread until it is allowed to run or must exit
 *
 * Thread entry functions should call this at the top of their processing loop.
 * The function blocks while the thread is paused and returns immediately
 * when running.
 *
 * @param thread Pointer to a struct @ref mpipe_thread
 * @retval 0 Thread should proceed (running).
 * @retval -ECANCELED Thread must exit (join requested).
 */
int mpipe_thread_wait(struct mpipe_thread *thread);

/**
 * @brief Resume a paused thread
 *
 * @param thread Pointer to a struct @ref mpipe_thread to resume
 */
void mpipe_thread_resume(struct mpipe_thread *thread);

/**
 * @brief Pause a running thread
 *
 * The thread will block at its next call to mpipe_thread_wait().
 *
 * @param thread Pointer to a struct @ref mpipe_thread to pause
 */
void mpipe_thread_pause(struct mpipe_thread *thread);

/**
 * @brief Join a thread and release its stack back to the thread stack pool
 *
 * Signals the thread to exit, waits for it to terminate, and releases
 * the stack back to the pool.  If the thread was never woken, it is
 * woken so it can observe the termination request and exit.
 *
 * @param thread Pointer to a struct @ref mpipe_thread to join
 * @param timeout Maximum time to wait for the thread to join
 * @return 0 on success or a negative errno on failure
 */
int mpipe_thread_join(struct mpipe_thread *thread, k_timeout_t timeout);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_THREAD_H_ */
