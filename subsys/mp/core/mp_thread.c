/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <zephyr/mp/core/mp_thread.h>

K_THREAD_STACK_ARRAY_DEFINE(thread_stack, CONFIG_MP_THREADS_NUM, CONFIG_MP_THREAD_STACK_SIZE);

static bool mp_thread_stack_pool[CONFIG_MP_THREADS_NUM];

k_tid_t mp_thread_create(struct mp_thread *thread, k_thread_entry_t func, void *p1, void *p2,
			 void *p3, int priority)
{
	int id;

	/* Find the 1st available slot in the thread pool */
	for (id = 0; id < CONFIG_MP_THREADS_NUM; id++) {
		if (mp_thread_stack_pool[id] == false) {
			break;
		}
	}

	if (id == CONFIG_MP_THREADS_NUM) {
		return NULL;
	}

	thread->stack_id = id;
	thread->running = true;
	mp_thread_stack_pool[id] = true;

	return k_thread_create(&thread->thread, thread_stack[thread->stack_id],
			       K_THREAD_STACK_SIZEOF(thread_stack[thread->stack_id]), func, p1, p2,
			       p3, priority, 0, K_NO_WAIT);
}

int mp_thread_join(struct mp_thread *thread, k_timeout_t timeout)
{
	int ret;

	if (thread == NULL) {
		return -EINVAL;
	}

	ret = k_thread_join(&thread->thread, timeout);
	if (ret == 0) {
		mp_thread_stack_pool[thread->stack_id] = false;
		thread->running = false;
	}

	return ret;
}
