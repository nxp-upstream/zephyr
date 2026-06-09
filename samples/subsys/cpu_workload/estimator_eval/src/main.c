/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/cpu_workload/cpu_workload.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define SAMPLE_CPU_ID 0

#define WORKER_STACK_SIZE 1536
#define WORKQ_STACK_SIZE 1536
#define CASE_SETTLE_MS 20
#define PROFILE_REPEATS 5
#define CASE_TIMEOUT_MS 2000

#define SMALL_WORK_ITERATIONS 20000U
#define MEDIUM_WORK_ITERATIONS 80000U
#define LARGE_WORK_ITERATIONS 240000U
#define IRQ_ISR_ITERATIONS 1000U
#define PERIPHERAL_WAIT_MS 15U
#define BURST_EVENTS 4U
#define CONTRIBUTOR_CAPACITY 12U

enum worker_id {
	WORKER_PERIODIC,
	WORKER_EVENT,
	WORKER_EXTERNAL,
	WORKER_BACKGROUND,
	WORKER_FOREGROUND,
	WORKER_BACKLOG_A,
	WORKER_BACKLOG_B,
	WORKER_PHASE,
	WORKER_UNPROFILED,
	WORKER_PERIPHERAL,
	WORKER_COUNT,
};

struct worker_control {
	const char *name;
	struct k_thread thread;
	k_tid_t tid;
	struct k_sem start;
	struct k_sem done;
	uint32_t iterations;
	uint32_t wait_ms;
};

struct actual_scope {
	k_tid_t tids[4];
	uint64_t before_cycles[4];
	size_t count;
	uint64_t isr_before_cycles;
};

static struct worker_control workers[WORKER_COUNT] = {
	[WORKER_PERIODIC] = { .name = "periodic", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_EVENT] = { .name = "event", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_EXTERNAL] = { .name = "external", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_BACKGROUND] = { .name = "background", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_FOREGROUND] = { .name = "foreground", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_BACKLOG_A] = { .name = "backlog-a", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_BACKLOG_B] = { .name = "backlog-b", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_PHASE] = { .name = "phase", .iterations = SMALL_WORK_ITERATIONS },
	[WORKER_UNPROFILED] = { .name = "unprofiled", .iterations = MEDIUM_WORK_ITERATIONS },
	[WORKER_PERIPHERAL] = {
		.name = "peripheral", .iterations = SMALL_WORK_ITERATIONS,
		.wait_ms = PERIPHERAL_WAIT_MS,
	},
};

K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, WORKER_COUNT, WORKER_STACK_SIZE);
K_THREAD_STACK_DEFINE(workq_stack, WORKQ_STACK_SIZE);

static struct k_work_q eval_workq;
static struct k_work eval_work;
static struct k_sem workq_done;
static uint32_t workq_iterations = MEDIUM_WORK_ITERATIONS;

static struct k_sem unprofiled_done;
static struct k_thread unprofiled_thread;
K_THREAD_STACK_DEFINE(unprofiled_stack, WORKER_STACK_SIZE);

static volatile uint32_t workload_sink;
static volatile uint64_t irq_isr_cycles;
static volatile bool irq_probe_mode;
static uint32_t case_index;
static struct cpu_workload_contributor last_contributors[CONTRIBUTOR_CAPACITY];
static size_t last_contributor_count;
static int last_contributor_ret;

K_SEM_DEFINE(irq_probe_sem, 0, 1);

#if DT_NODE_HAS_STATUS(DT_ALIAS(workload_irq_in), okay) && \
	DT_NODE_HAS_STATUS(DT_ALIAS(workload_irq_out), okay)
#define HAVE_WORKLOAD_IRQ_GPIO 1
static const struct gpio_dt_spec irq_in = GPIO_DT_SPEC_GET(DT_ALIAS(workload_irq_in), gpios);
static const struct gpio_dt_spec irq_out = GPIO_DT_SPEC_GET(DT_ALIAS(workload_irq_out), gpios);
static struct gpio_callback irq_cb;
#else
#define HAVE_WORKLOAD_IRQ_GPIO 0
#endif

static void burn_cycles(uint32_t iterations)
{
	uint32_t value = workload_sink;

	for (uint32_t i = 0U; i < iterations; i++) {
		value = (value * 1664525U) + 1013904223U + i;
		value ^= value >> 13;
	}

	workload_sink = value;
}

static uint64_t thread_cycles_get(k_tid_t tid)
{
	k_thread_runtime_stats_t stats;

	if ((tid == NULL) || (k_thread_runtime_stats_get(tid, &stats) != 0)) {
		return 0U;
	}

	return stats.total_cycles;
}

static void worker_entry(void *p1, void *p2, void *p3)
{
	struct worker_control *worker = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&worker->start, K_FOREVER);
		if (worker->wait_ms != 0U) {
			k_sleep(K_MSEC(worker->wait_ms));
		}
		burn_cycles(worker->iterations);
		k_sem_give(&worker->done);
	}
}

static void workq_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	burn_cycles(workq_iterations);
	k_sem_give(&workq_done);
}

static void unprofiled_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	burn_cycles(MEDIUM_WORK_ITERATIONS);
	k_sem_give(&unprofiled_done);
}

static void workers_start(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(workers); i++) {
		k_sem_init(&workers[i].start, 0, 16);
		k_sem_init(&workers[i].done, 0, 16);
		workers[i].tid = k_thread_create(&workers[i].thread, worker_stacks[i],
						 K_THREAD_STACK_SIZEOF(worker_stacks[i]), worker_entry,
						 &workers[i], NULL, NULL, K_PRIO_PREEMPT(1), 0,
						 K_NO_WAIT);
		k_thread_name_set(workers[i].tid, workers[i].name);
	}

	k_sem_init(&workq_done, 0, 16);
	k_sem_init(&unprofiled_done, 0, 1);
	k_work_init(&eval_work, workq_handler);
	k_work_queue_init(&eval_workq);
	k_work_queue_start(&eval_workq, workq_stack, K_THREAD_STACK_SIZEOF(workq_stack),
			   K_PRIO_PREEMPT(1), NULL);
	k_thread_name_set(k_work_queue_thread_get(&eval_workq), "workload-workq");
}

static void trigger_worker(enum worker_id id)
{
	k_sem_give(&workers[id].start);
}

static void wait_worker(enum worker_id id)
{
	int ret = k_sem_take(&workers[id].done, K_MSEC(CASE_TIMEOUT_MS));

	if (ret != 0) {
		printk("WAIT_TIMEOUT,worker=%s,ret=%d\n", workers[id].name, ret);
	}
}

static void wait_worker_events(enum worker_id id, uint32_t events)
{
	for (uint32_t i = 0U; i < events; i++) {
		wait_worker(id);
	}
}

static void actual_begin(struct actual_scope *scope, const k_tid_t *tids, size_t count)
{
	scope->count = MIN(count, ARRAY_SIZE(scope->tids));
	scope->isr_before_cycles = irq_isr_cycles;

	for (size_t i = 0U; i < scope->count; i++) {
		scope->tids[i] = tids[i];
		scope->before_cycles[i] = thread_cycles_get(tids[i]);
	}
}

static uint64_t actual_end(const struct actual_scope *scope)
{
	uint64_t cycles = irq_isr_cycles - scope->isr_before_cycles;

	for (size_t i = 0U; i < scope->count; i++) {
		cycles += thread_cycles_get(scope->tids[i]) - scope->before_cycles[i];
	}

	return cycles;
}

static int estimate_get(struct cpu_workload_estimate *estimate)
{
	int ret;

	last_contributor_count = ARRAY_SIZE(last_contributors);
	last_contributor_ret = cpu_workload_contributors_get(SAMPLE_CPU_ID, last_contributors,
							   &last_contributor_count);

	ret = cpu_workload_estimate_get(SAMPLE_CPU_ID, estimate);

	if (ret != 0) {
		printk("EST_ERROR,ret=%d\n", ret);
	}

	return ret;
}

static void flush_estimator(void)
{
	struct cpu_workload_estimate estimate;

	(void)estimate_get(&estimate);
	k_sleep(K_MSEC(CASE_SETTLE_MS));
}

static void clear_arrivals(void)
{
	struct cpu_workload_arrival arrival;

	(void)cpu_workload_arrival_get(SAMPLE_CPU_ID, &arrival);
}

static void prime_worker(enum worker_id id, uint32_t repeats)
{
	for (uint32_t i = 0U; i < repeats; i++) {
		trigger_worker(id);
		wait_worker(id);
		k_sleep(K_MSEC(2));
	}

	flush_estimator();
}

static void prime_workqueue(uint32_t repeats)
{
	for (uint32_t i = 0U; i < repeats; i++) {
		k_work_submit_to_queue(&eval_workq, &eval_work);
		(void)k_sem_take(&workq_done, K_MSEC(CASE_TIMEOUT_MS));
		k_sleep(K_MSEC(2));
	}

	flush_estimator();
}

static const char *classify_error(int64_t error_cycles)
{
	if (error_cycles < 0) {
		return "underestimate";
	}

	if (error_cycles > 0) {
		return "overestimate";
	}

	return "exact";
}

static int64_t error_percent_milli_get(int64_t error_cycles, uint64_t target_actual_cycles)
{
	int64_t scaled_error;
	int64_t denominator;

	if (target_actual_cycles == 0U) {
		return 0;
	}

	denominator = (int64_t)target_actual_cycles;
	scaled_error = error_cycles * 100000LL;
	if (scaled_error >= 0) {
		scaled_error += denominator / 2;
	} else {
		scaled_error -= denominator / 2;
	}

	return scaled_error / denominator;
}

static void print_source_name(uint32_t source_mask, enum cpu_workload_source source,
			      const char *name, bool *printed)
{
	if ((source_mask & source) == 0U) {
		return;
	}

	printk("%s%s", *printed ? "," : "", name);
	*printed = true;
}

static void print_source_mask_names(uint32_t source_mask)
{
	bool printed = false;

	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_RUNTIME_HISTORY, "history", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_THREAD_BURST_PROFILE,
			  "thread-profile", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_THREAD_ACTIVATION_PROFILE,
			  "activation-profile", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_READY_BACKLOG, "ready-backlog", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL, "arrival", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL_TIMEOUT,
			  "arrival-timeout", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL_SYNC, "arrival-sync", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL_EXPLICIT,
			  "arrival-explicit", &printed);

	if (!printed) {
		printk("none");
	}
}

static void print_arrival_source_names(uint32_t source_mask)
{
	bool printed = false;

	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL_TIMEOUT, "timeout", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL_SYNC, "sync", &printed);
	print_source_name(source_mask, CPU_WORKLOAD_SOURCE_ARRIVAL_EXPLICIT, "explicit", &printed);

	if (!printed) {
		printk("none");
	}
}

static const char *contributor_name(const struct cpu_workload_contributor *contributor)
{
	if ((contributor->thread_name != NULL) && (contributor->thread_name[0] != '\0')) {
		return contributor->thread_name;
	}

	return "<unnamed>";
}

static void print_contributor_raw_stats(k_tid_t tid)
{
	k_thread_activation_stats_t activation;

	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		return;
	}

	if (k_thread_activation_stats_get(tid, &activation, false) == 0) {
		printk("      activation stats   : completed=%llu count=%u events=%u active=%llu active_events=%u\n",
		       (unsigned long long)activation.completed_cycles,
		       activation.completed_count, activation.completed_events,
		       (unsigned long long)activation.active_cycles,
		       activation.active_events);
	}
}

static void print_contributor_flags(uint32_t flags)
{
	bool printed = false;

	if ((flags & CPU_WORKLOAD_CONTRIBUTOR_RUNNABLE) != 0U) {
		printk("runnable");
		printed = true;
	}

	if ((flags & CPU_WORKLOAD_CONTRIBUTOR_PROFILED) != 0U) {
		printk("%sprofiled", printed ? "," : "");
		printed = true;
	}

	if ((flags & CPU_WORKLOAD_CONTRIBUTOR_READY_BACKLOG) != 0U) {
		printk("%sready-backlog", printed ? "," : "");
		printed = true;
	}

	if ((flags & CPU_WORKLOAD_CONTRIBUTOR_ARRIVAL) != 0U) {
		printk("%sarrival", printed ? "," : "");
		printed = true;
	}

	if ((flags & CPU_WORKLOAD_CONTRIBUTOR_CURRENT_THREAD) != 0U) {
		printk("%scurrent/test-harness", printed ? "," : "");
		printed = true;
	}

	if ((flags & CPU_WORKLOAD_CONTRIBUTOR_ACTIVATION_PROFILE) != 0U) {
		printk("%sactivation-profile", printed ? "," : "");
		printed = true;
	}

	if (!printed) {
		printk("none");
	}
}

static void print_contributors(void)
{
	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		return;
	}

	if (last_contributor_ret == -ENOTSUP) {
		printk("  contributors:\n");
		printk("    unavailable          : CONFIG_CPU_WORKLOAD_DIAGNOSTIC is disabled\n");
		return;
	}

	if ((last_contributor_ret != 0) && (last_contributor_ret != -ENOMEM)) {
		printk("  contributors:\n");
		printk("    unavailable          : ret=%d\n", last_contributor_ret);
		return;
	}

	printk("  contributors:\n");
	if (last_contributor_count == 0U) {
		printk("    none                 : no runnable or arrived profiled threads\n");
		return;
	}

	for (size_t i = 0U; i < last_contributor_count; i++) {
		const struct cpu_workload_contributor *contributor = &last_contributors[i];

		printk("    - thread             : %s id=0x%lx\n", contributor_name(contributor),
		       (unsigned long)contributor->thread_id);
		printk("      flags              : ");
		print_contributor_flags(contributor->flags);
		printk("\n");
		printk("      burst profile      : avg=%u samples=%u events=%u confidence=%u/100\n",
		       contributor->burst_avg_cycles, contributor->sample_count,
		       contributor->event_count, contributor->confidence);
		printk("      backlog cycles     : %llu\n",
		       (unsigned long long)contributor->backlog_cycles);
		printk("      arrival events     : count=%u cycles=%llu sources=",
		       contributor->arrival_count,
		       (unsigned long long)contributor->arrival_cycles);
		print_arrival_source_names(contributor->arrival_source_mask);
		printk("\n");
		printk("      merged cycles      : %llu\n",
		       (unsigned long long)contributor->merged_cycles);
		print_contributor_raw_stats((k_tid_t)contributor->thread_id);
		if (((contributor->flags & CPU_WORKLOAD_CONTRIBUTOR_CURRENT_THREAD) != 0U) &&
		    (((contributor->flags & CPU_WORKLOAD_CONTRIBUTOR_READY_BACKLOG) != 0U) ||
		     ((contributor->flags & CPU_WORKLOAD_CONTRIBUTOR_ARRIVAL) != 0U))) {
			printk("      note               : current thread is the sample control path\n");
		}
	}

	if (last_contributor_ret == -ENOMEM) {
		printk("    warning              : contributor list truncated\n");
	}
}

static void print_estimate(const struct cpu_workload_estimate *estimate)
{
	uint64_t raw_forward_sum = estimate->ready_backlog_cycles +
				   estimate->expected_arrival_cycles;

	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		return;
	}

	printk("  estimate:\n");
	printk("    estimated cycles      : %llu\n",
	       (unsigned long long)estimate->estimated_cycles);
	printk("    history cycles        : %llu\n",
	       (unsigned long long)estimate->history_cycles);
	printk("    ready backlog cycles  : %llu\n",
	       (unsigned long long)estimate->ready_backlog_cycles);
	printk("    arrival cycles        : %llu\n",
	       (unsigned long long)estimate->expected_arrival_cycles);
	printk("    raw forward sum       : %llu\n", (unsigned long long)raw_forward_sum);
	printk("    merged forward cycles : %llu\n",
	       (unsigned long long)estimate->forward_cycles);
	printk("    ready threads         : runnable=%u profiled=%u\n",
	       estimate->runnable_threads, estimate->profiled_runnable_threads);
	printk("    arrival events        : total=%u profiled=%u\n",
	       estimate->arrivals, estimate->profiled_arrivals);
	printk("    history window        : %u us\n", estimate->history_window_us);
	printk("    history load          : %u%%\n", estimate->history_load);
	printk("    confidence            : %u/100\n", estimate->confidence);
	printk("    source mask           : 0x%x\n", estimate->source_mask);
	printk("    source names          : ");
	print_source_mask_names(estimate->source_mask);
	printk("\n");
	print_contributors();
}

static bool contributor_is_target(const struct cpu_workload_contributor *contributor,
					 const k_tid_t *tids, size_t count)
{
	for (size_t i = 0U; i < count; i++) {
		if (contributor->thread_id == (uintptr_t)tids[i]) {
			return true;
		}
	}

	return false;
}

static uint64_t target_forward_cycles_get(const k_tid_t *tids, size_t count)
{
	uint64_t cycles = 0U;

	if ((last_contributor_ret != 0) && (last_contributor_ret != -ENOMEM)) {
		return 0U;
	}

	for (size_t i = 0U; i < last_contributor_count; i++) {
		if (contributor_is_target(&last_contributors[i], tids, count)) {
			cycles += last_contributors[i].merged_cycles;
		}
	}

	return cycles;
}

static void print_result(const char *case_name, const struct cpu_workload_estimate *estimate,
			 const char *metric_key, const char *metric_name, const char *metric_scope,
			 uint64_t metric_cycles, uint64_t target_actual_cycles)
{
	int64_t error_cycles = (int64_t)metric_cycles - (int64_t)target_actual_cycles;
	int64_t error_percent_milli = error_percent_milli_get(error_cycles, target_actual_cycles);
	uint64_t error_percent_abs = error_percent_milli < 0 ?
		(uint64_t)-error_percent_milli : (uint64_t)error_percent_milli;
	const char *error_percent_sign = error_percent_milli < 0 ? "-" : "+";
	const char *classification = classify_error(error_cycles);

	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		printk("\n[%02u] %s\n", case_index, case_name);
		printk("  compare : metric=%s predicted=%llu actual=%llu\n", metric_key,
		       (unsigned long long)metric_cycles,
		       (unsigned long long)target_actual_cycles);
		printk("  error   : cycles=%lld percent=%s%llu.%03llu%% class=%s\n",
		       (long long)error_cycles, error_percent_sign,
		       (unsigned long long)(error_percent_abs / 1000U),
		       (unsigned long long)(error_percent_abs % 1000U), classification);
		printk("  cpu diag: estimated=%llu forward=%llu\n",
		       (unsigned long long)estimate->estimated_cycles,
		       (unsigned long long)estimate->forward_cycles);
		return;
	}

	printk("  target actual:\n");
	printk("    target cycles         : %llu\n", (unsigned long long)target_actual_cycles);
	printk("  primary result:\n");
	printk("    comparison metric     : %s\n", metric_name);
	printk("    comparison scope      : %s\n", metric_scope);
	printk("    comparison cycles     : %llu\n", (unsigned long long)metric_cycles);
	printk("    error cycles          : %lld\n", (long long)error_cycles);
	printk("    error percent         : %s%llu.%03llu%%\n", error_percent_sign,
	       (unsigned long long)(error_percent_abs / 1000U),
	       (unsigned long long)(error_percent_abs % 1000U));
	printk("    classification        : %s\n", classification);
	printk("  unified diagnostic:\n");
	printk("    cpu-wide estimated    : %llu\n",
	       (unsigned long long)estimate->estimated_cycles);
	printk("    cpu-wide forward      : %llu\n",
	       (unsigned long long)estimate->forward_cycles);
	printk("    scope note            : contributors list shows CPU-wide estimate scope\n");
	printk("  record: case=%s metric=%s metric_cycles=%llu target_actual=%llu error=%lld error_percent=%s%llu.%03llu class=%s estimated=%llu forward=%llu confidence=%u source=0x%x runnable=%u profiled_runnable=%u arrivals=%u profiled_arrivals=%u\n",
	       case_name, metric_key, (unsigned long long)metric_cycles,
	       (unsigned long long)target_actual_cycles, (long long)error_cycles,
	       error_percent_sign, (unsigned long long)(error_percent_abs / 1000U),
	       (unsigned long long)(error_percent_abs % 1000U), classification,
	       (unsigned long long)estimate->estimated_cycles,
	       (unsigned long long)estimate->forward_cycles, estimate->confidence, estimate->source_mask,
	       estimate->runnable_threads, estimate->profiled_runnable_threads,
	       estimate->arrivals, estimate->profiled_arrivals);
}

static void print_skip(const char *case_name, const char *reason)
{
	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		printk("\n[%02u] %s\n", case_index, case_name);
		printk("  status  : skipped\n");
		printk("  reason  : %s\n", reason);
		return;
	}

	printk("  status:\n");
	printk("    skipped               : %s\n", reason);
	printk("  record: case=%s status=skip reason=%s\n", case_name, reason);
}

static void case_begin(const char *case_name, const char *description,
		       const char *primary_comparison)
{
	case_index++;
	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		return;
	}

	printk("\n[%02u] %s\n", case_index, case_name);
	printk("  purpose                 : %s\n", description);
	printk("  primary comparison      : %s\n", primary_comparison);
}

static void run_periodic_case(void)
{
	const char *case_name = "periodic-thread";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_PERIODIC].tid };

	case_begin(case_name, "stable periodic work predicted mainly from runtime history",
		   "history cycles vs target periodic worker cycles");
	prime_worker(WORKER_PERIODIC, PROFILE_REPEATS);
	trigger_worker(WORKER_PERIODIC);
	wait_worker(WORKER_PERIODIC);
	clear_arrivals();
	k_sleep(K_MSEC(CASE_SETTLE_MS));
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	trigger_worker(WORKER_PERIODIC);
	wait_worker(WORKER_PERIODIC);
	print_result(case_name, &estimate, "history", "history cycles", "runtime-history component",
		     estimate.history_cycles, actual_end(&actual));
}

static void run_event_case(void)
{
	const char *case_name = "event-driven-thread";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_EVENT].tid };

	case_begin(case_name, "semaphore wakes a profiled event thread",
		   "target merged forward cycles vs target event thread cycles");
	prime_worker(WORKER_EVENT, PROFILE_REPEATS);
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	trigger_worker(WORKER_EVENT);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	wait_worker(WORKER_EVENT);
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

static void run_workqueue_case(void)
{
	const char *case_name = "workqueue-deferred-work";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { k_work_queue_thread_get(&eval_workq) };

	case_begin(case_name, "deferred work submitted to a dedicated workqueue",
		   "target merged forward cycles vs target workqueue cycles");
	prime_workqueue(PROFILE_REPEATS);
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	k_work_submit_to_queue(&eval_workq, &eval_work);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	(void)k_sem_take(&workq_done, K_MSEC(CASE_TIMEOUT_MS));
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

#if HAVE_WORKLOAD_IRQ_GPIO
static void fire_irq_gpio(uint32_t count);

static void irq_callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	uint64_t start_cycles = k_cycle_get_64();

	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	burn_cycles(IRQ_ISR_ITERATIONS);
	irq_isr_cycles += k_cycle_get_64() - start_cycles;
	if (irq_probe_mode) {
		k_sem_give(&irq_probe_sem);
		return;
	}

	k_sem_give(&workers[WORKER_EXTERNAL].start);
}

static bool setup_irq_gpio(void)
{
	if (!gpio_is_ready_dt(&irq_out) || !gpio_is_ready_dt(&irq_in)) {
		return false;
	}

	if (gpio_pin_configure_dt(&irq_out, GPIO_OUTPUT_INACTIVE) != 0) {
		return false;
	}

	if (gpio_pin_configure_dt(&irq_in, GPIO_INPUT) != 0) {
		return false;
	}

	if (gpio_pin_interrupt_configure_dt(&irq_in, GPIO_INT_EDGE_RISING) != 0) {
		return false;
	}

	gpio_init_callback(&irq_cb, irq_callback, BIT(irq_in.pin));
	if (gpio_add_callback(irq_in.port, &irq_cb) != 0) {
		return false;
	}

	irq_probe_mode = true;
	fire_irq_gpio(1U);
	irq_probe_mode = false;

	return k_sem_take(&irq_probe_sem, K_MSEC(50)) == 0;
}

static void fire_irq_gpio(uint32_t count)
{
	for (uint32_t i = 0U; i < count; i++) {
		(void)gpio_pin_set_dt(&irq_out, 1);
		k_busy_wait(50);
		(void)gpio_pin_set_dt(&irq_out, 0);
		k_busy_wait(200);
	}
}
#else
static bool setup_irq_gpio(void)
{
	return false;
}

static void fire_irq_gpio(uint32_t count)
{
	ARG_UNUSED(count);
}
#endif

static void run_external_irq_case(bool irq_ready)
{
	const char *case_name = "sporadic-external-interrupt";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_EXTERNAL].tid };

	case_begin(case_name, "single GPIO interrupt wakes a profiled bottom-half thread",
		   "target merged forward cycles vs target interrupt bottom-half cycles");
	if (!irq_ready) {
		print_skip(case_name, "no workload IRQ GPIO loopback");
		return;
	}

	prime_worker(WORKER_EXTERNAL, PROFILE_REPEATS);
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	fire_irq_gpio(1U);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	wait_worker(WORKER_EXTERNAL);
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

static void run_interrupt_burst_case(bool irq_ready)
{
	const char *case_name = "interrupt-burst";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_EXTERNAL].tid };

	case_begin(case_name, "multiple GPIO interrupts arrive before bottom-half processing",
		   "target merged forward cycles vs target interrupt burst cycles");
	if (!irq_ready) {
		print_skip(case_name, "no workload IRQ GPIO loopback");
		return;
	}

	prime_worker(WORKER_EXTERNAL, PROFILE_REPEATS);
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	fire_irq_gpio(BURST_EVENTS);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	wait_worker_events(WORKER_EXTERNAL, BURST_EVENTS);
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

static void run_mixed_case(void)
{
	const char *case_name = "mixed-background-foreground";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_BACKGROUND].tid, workers[WORKER_FOREGROUND].tid };
	k_tid_t foreground_tids[] = { workers[WORKER_FOREGROUND].tid };

	case_begin(case_name, "history-like background work plus newly arrived foreground work",
		   "history plus target merged forward cycles vs background plus foreground target cycles");
	prime_worker(WORKER_BACKGROUND, PROFILE_REPEATS);
	prime_worker(WORKER_FOREGROUND, PROFILE_REPEATS);
	trigger_worker(WORKER_BACKGROUND);
	wait_worker(WORKER_BACKGROUND);
	clear_arrivals();
	k_sleep(K_MSEC(CASE_SETTLE_MS));
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	trigger_worker(WORKER_FOREGROUND);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	trigger_worker(WORKER_BACKGROUND);
	wait_worker(WORKER_FOREGROUND);
	wait_worker(WORKER_BACKGROUND);
	print_result(case_name, &estimate, "history-target-forward",
		     "history plus target merged forward cycles",
		     "runtime-history plus target foreground contributor",
		     estimate.history_cycles + target_forward_cycles_get(foreground_tids,
								    ARRAY_SIZE(foreground_tids)),
		     actual_end(&actual));
}

static void run_ready_backlog_case(void)
{
	const char *case_name = "ready-backlog";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_BACKLOG_A].tid, workers[WORKER_BACKLOG_B].tid };

	case_begin(case_name, "two profiled threads are runnable at the query point",
		   "target merged forward cycles vs target ready-backlog worker cycles");
	prime_worker(WORKER_BACKLOG_A, PROFILE_REPEATS);
	prime_worker(WORKER_BACKLOG_B, PROFILE_REPEATS);
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	trigger_worker(WORKER_BACKLOG_A);
	trigger_worker(WORKER_BACKLOG_B);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	wait_worker(WORKER_BACKLOG_A);
	wait_worker(WORKER_BACKLOG_B);
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

static void run_phase_change_case(void)
{
	const char *case_name = "workload-phase-change";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_PHASE].tid };

	case_begin(case_name, "thread profile is trained on small work then switches to large work",
		   "target merged forward cycles vs target phase-change worker cycles");
	workers[WORKER_PHASE].iterations = SMALL_WORK_ITERATIONS;
	prime_worker(WORKER_PHASE, PROFILE_REPEATS);
	workers[WORKER_PHASE].iterations = LARGE_WORK_ITERATIONS;
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	trigger_worker(WORKER_PHASE);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	wait_worker(WORKER_PHASE);
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

static void run_unprofiled_case(void)
{
	const char *case_name = "unprofiled-new-work";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tid;
	k_tid_t tids[1];

	case_begin(case_name, "new runnable work has no mature burst profile",
		   "target merged forward cycles and confidence vs target unprofiled worker cycles");
	flush_estimator();
	tid = k_thread_create(&unprofiled_thread, unprofiled_stack,
			      K_THREAD_STACK_SIZEOF(unprofiled_stack), unprofiled_entry,
			      NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_FOREVER);
	k_thread_name_set(tid, "unprofiled-new");
	tids[0] = tid;
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	k_thread_start(tid);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	(void)k_sem_take(&unprofiled_done, K_MSEC(CASE_TIMEOUT_MS));
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

static void run_peripheral_wait_case(void)
{
	const char *case_name = "peripheral-wait";
	struct cpu_workload_estimate estimate;
	struct actual_scope actual;
	k_tid_t tids[] = { workers[WORKER_PERIPHERAL].tid };

	case_begin(case_name, "thread waits for a DMA-like delay before doing small CPU work",
		   "target merged forward cycles vs target CPU-only peripheral worker cycles");
	prime_worker(WORKER_PERIPHERAL, PROFILE_REPEATS);
	actual_begin(&actual, tids, ARRAY_SIZE(tids));
	k_sched_lock();
	trigger_worker(WORKER_PERIPHERAL);
	if (estimate_get(&estimate) == 0) {
		print_estimate(&estimate);
	}
	k_sched_unlock();
	wait_worker(WORKER_PERIPHERAL);
	print_result(case_name, &estimate, "target-forward", "target merged forward cycles",
		     "target contributor(s) only", target_forward_cycles_get(tids, ARRAY_SIZE(tids)),
		     actual_end(&actual));
}

int main(void)
{
	bool irq_ready;

	k_thread_name_set(k_current_get(), "eval-main");
	workers_start();
	irq_ready = setup_irq_gpio();

	printk("\nCPU workload estimator evaluation\n");
	printk("=================================\n");
	printk("cpu id       : %d\n", SAMPLE_CPU_ID);
	printk("case count   : 10\n");
	printk("irq loopback : GPIO5_2 -> GPIO5_3 (%s)\n", irq_ready ? "ready" : "not detected");
	printk("error sign   : predicted - actual\n");
	printk("output       : %s\n",
	       IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE) ? "verbose" : "summary");
	if (!IS_ENABLED(CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE)) {
		printk("format       : one compact block per case\n");
	} else {
		printk("scope note   : primary result uses the case-specific comparison metric\n");
		printk("record lines : compact machine-readable summary per case\n");
		printk("contributors : captured before each estimate; current/test-harness is sample control\n");
	}
	flush_estimator();

	run_periodic_case();
	run_event_case();
	run_workqueue_case();
	run_external_irq_case(irq_ready);
	run_interrupt_burst_case(irq_ready);
	run_mixed_case();
	run_ready_backlog_case();
	run_phase_change_case();
	run_unprofiled_case();
	run_peripheral_wait_case();

	printk("\nCPU workload estimator evaluation complete\n");

	return 0;
}
