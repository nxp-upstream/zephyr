/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <zephyr/mp/core/mp_thread.h>

K_THREAD_STACK_ARRAY_DEFINE(thread_stack, CONFIG_MP_THREADS_NUM, CONFIG_MP_THREAD_STACK_SIZE);

uint8_t mp_thread_pool[CONFIG_MP_THREADS_NUM] = {0};
struct mp_thread_wrapper_args {
	struct mp_thread *thread;
	k_thread_entry_t func;
	void *p1;
	void *p2;
	void *p3;
	struct k_work cleanup_work;
};
static struct mp_thread_wrapper_args thread_wrapper_args_pool[CONFIG_MP_THREADS_NUM];

static void mp_thread_cleanup_work_handler(struct k_work *work)
{
	struct mp_thread_wrapper_args *args =
		CONTAINER_OF(work, struct mp_thread_wrapper_args, cleanup_work);

	k_thread_join(&args->thread->thread_data, K_FOREVER);

	mp_thread_pool[args->thread->stack_id] = 0;
}

static int mp_thread_stack_acquire(void)
{
	int stack_id = 0;

	for (stack_id = 0; stack_id < CONFIG_MP_THREADS_NUM; stack_id++) {
		if (mp_thread_pool[stack_id] == 0) {
			break;
		}
	}

	if (stack_id == CONFIG_MP_THREADS_NUM) {
		return -1;
	}

	mp_thread_pool[stack_id] = 1;

	return stack_id;
}

static void mp_thread_entry_func_exec(void *p1, void *p2, void *p3)
{
	struct mp_thread_wrapper_args *args = (struct mp_thread_wrapper_args *)p1;

	/* Call the actual user entry function */
	args->func(args->p1, args->p2, args->p3);

	/* Thread has been terminated, schedule cleanup via workqueue */
	k_work_submit(&args->cleanup_work);
}

k_tid_t mp_thread_create(struct mp_thread *thread, k_thread_entry_t func, void *p1, void *p2,
			 void *p3, int priority)
{
	thread->stack_id = mp_thread_stack_acquire();
	if (thread->stack_id < 0) {
		printk("No more thread stacks available\n");
		return NULL;
	}

	/* Store wrapper arguments in the pool */
	thread_wrapper_args_pool[thread->stack_id].thread = thread;
	thread_wrapper_args_pool[thread->stack_id].func = func;
	thread_wrapper_args_pool[thread->stack_id].p1 = p1;
	thread_wrapper_args_pool[thread->stack_id].p2 = p2;
	thread_wrapper_args_pool[thread->stack_id].p3 = p3;
	k_work_init(&thread_wrapper_args_pool[thread->stack_id].cleanup_work,
		    mp_thread_cleanup_work_handler);

	return k_thread_create(&thread->thread_data, thread_stack[thread->stack_id],
			       K_THREAD_STACK_SIZEOF(thread_stack[thread->stack_id]),
			       mp_thread_entry_func_exec,
			       &thread_wrapper_args_pool[thread->stack_id], NULL, NULL, priority, 0,
			       K_NO_WAIT);
}

void mp_thread_release(struct mp_thread *thread)
{
	thread->running = false;
}
