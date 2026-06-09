.. zephyr:code-sample:: cpu-workload-estimator-eval
   :name: CPU workload estimator evaluation
   :relevant-api: subsys_cpu_workload

   评估 cpu_workload 对常见嵌入式 workload 模式的估计结果。

概述
****

这个 sample 用常见嵌入式 workload 模式评估 ``cpu_workload`` 的估计组件。
每个 case 都会先在释放 workload 前调用 ``cpu_workload_estimate_get()``，
然后把 case 专属的主比较指标与目标 workload 实际消耗的 CPU cycles 做对比。
事件驱动类 case 用来验证 activation burst profile：它学习的是一次完整的
blocked-to-ready-to-blocked activation 所消耗的 CPU cycles，而不是单个
scheduler runtime window。统一估计结果仍然作为 CPU-wide 诊断上下文输出。

sample 覆盖以下场景：

* 周期性计算；
* 事件驱动线程；
* workqueue 延迟执行；
* 零散外部中断；
* 中断突发；
* background 与 foreground 混合 workload；
* ready backlog / 已排队 runnable 线程；
* workload phase change；
* 尚未 profile 的新 workload；
* peripheral wait / 类 DMA 行为。

外部中断 case 使用可选 GPIO loopback aliases：``workload-irq-out`` 和
``workload-irq-in``。FRDM-MCXN236 overlay 会把这些 aliases 映射到
GPIO5_2 和 GPIO5_3。连接 GPIO5_2 到 GPIO5_3 后，可以启用可重复的外部中断
case。如果 aliases 不存在、输入中断无法配置，或者 loopback 自检没有观察到
边沿，这些 case 会输出为 ``SKIP``。

构建与运行
**********

使用支持所需 kernel runtime statistics 的 board 构建并运行：

.. code-block:: console

   west build -b frdm_mcxn236/mcxn236 samples/subsys/cpu_workload/estimator_eval
   west flash

输出
****

默认情况下，sample 输出简洁 summary，让串口日志始终聚焦在主验证结果上。
每个 case 是一个紧凑 block，包含选定的主比较指标、目标实际 cycles、有符号误差、
分类，以及作为上下文的 CPU-wide estimated/forward cycles。

.. code-block:: text

   CPU workload estimator evaluation
   =================================
   cpu id       : 0
   case count   : 10
   irq loopback : GPIO5_2 -> GPIO5_3 (ready)
   error sign   : predicted - actual
   output       : summary
   format       : one compact block per case

   [01] periodic-thread
     compare : metric=history predicted=746336 actual=721086
       error   : cycles=25250 percent=+3.502% class=overestimate
     cpu diag: estimated=746336 forward=594318

   [02] event-driven-thread
     compare : metric=target-forward predicted=721489 actual=721123
       error   : cycles=366 percent=+0.051% class=overestimate
     cpu diag: estimated=2075907 forward=2075907

正误差表示主比较指标高于目标实际 workload；负误差表示低估目标 workload。
``cpu diag`` 行显示 CPU-wide 统一诊断结果，包括 estimated cycles 和 forward cycles。

详细诊断
********

启用 ``CONFIG_CPU_WORKLOAD_ESTIMATOR_EVAL_VERBOSE`` 后，sample 会输出完整的
per-case 诊断 block。verbose 输出包含每个 case 的目的、estimate 组件、在 estimate
消费 arrival window 之前捕获的 per-thread contributor 诊断，以及便于脚本解析的紧凑
``record:`` 行。contributor block 还包含 raw runtime 与 activation counters，便于把
profile 估计值和 kernel accounting 源数据对齐检查。

解释统一估计时，contributor list 很重要。estimator 是 CPU-wide 的，所以如果 sample
自己的控制线程正处于 runnable 状态，或者刚刚从 timeout 唤醒，它也可能出现在统一估计中。
这类 entry 会标记为 ``current/test-harness``。主结果使用 case 的目标 workload 指标，
以避免这种 scope mismatch；统一诊断则保留完整 CPU-wide estimate。

``raw forward sum`` 是 ready-backlog cycles 与 arrival cycles 的简单相加。
统一估计使用 ``merged forward cycles``，它会按线程合并这两个组件，避免刚被唤醒且
已经 runnable 的线程被重复计数。

对于周期性 workload，主比较指标是 runtime history。对于 event、workqueue、IRQ、
backlog、phase-change、unprofiled 和 peripheral-wait case，主比较指标是目标线程的
merged forward cycles，即只对目标 contributor 的 ``merged cycles`` 求和。成熟的事件驱动
case 中，这些 merged cycles 来自 activation-profile samples。mixed background/foreground
case 使用 runtime history 加目标 foreground forward cycles，因为它刻意同时覆盖重复 background
work 与新到达 foreground work。

activation profile 预期会显著改善 event、workqueue、IRQ 和 ready-backlog case，相比
scheduler-window profile 更接近完整事件 workload。phase-change case 刻意先用小 workload
训练，再切换到大 workload，用来暴露 profile adaptation lag。unprofiled case 刻意没有成熟
activation sample，因此应该报告低 confidence，并且可能低估第一次 activation。peripheral-wait
case 代表 multi-activation semantic event：worker 先被唤醒，然后 sleep 模拟 DMA-like delay，
之后再次被 timeout 唤醒并执行 CPU work。因此它预期会展示 single-arrival forward estimate
的边界，除非更高层 event model 能提供 follow-on timeout work。

main.c 设计拆解
****************

整体结构
========

``main.c`` 的核心目标不是跑一个真实业务应用，而是构造一组可重复、可测量、可解释的
workload 场景，用来验证 ``cpu_workload`` estimator 的行为。它把测试分成三层：

* workload 生成层：worker threads、workqueue、GPIO IRQ、unprofiled thread；
* measurement 层：在释放 workload 前获取 estimate，并在 workload 完成后统计实际 cycles；
* reporting 层：输出 summary 或 verbose diagnostics，并把目标 scope 与 CPU-wide diagnostic 分开。

基础 workload 模型
==================

``workers`` 数组定义了大多数 case 使用的长驻 worker thread。每个 worker 都有：

* ``start`` semaphore：sample 控制线程通过 ``k_sem_give()`` 释放一次工作；
* ``done`` semaphore：worker 完成后通知控制线程；
* ``iterations``：控制 ``burn_cycles()`` 的 CPU work 大小；
* ``wait_ms``：可选等待时间，用来模拟 peripheral/DMA-like delay。

worker 主循环是固定模式：等待 ``start``，可选 sleep，执行 ``burn_cycles()``，然后 give
``done``。这种模式让一次业务事件天然对应一次 blocked-to-ready-to-blocked activation，
非常适合验证 activation profile。

``burn_cycles()`` 使用 deterministic integer 运算消耗 CPU cycles，并把结果写入
``workload_sink``，避免编译器把计算优化掉。sample 关心的是相对稳定的 CPU cycle 成本，
而不是某个算法本身。

估计与实际值测量
================

``estimate_get()`` 做两件事：

* 先调用 ``cpu_workload_contributors_get()`` 保存 contributor 快照；
* 再调用 ``cpu_workload_estimate_get()`` 获取统一估计。

这个顺序是刻意设计的，因为 estimate 可能会消费 arrival window。先保存 contributor，verbose
输出就能解释这次 estimate 到底由哪些线程贡献。

``actual_begin()`` 和 ``actual_end()`` 负责目标实际 cycles 的测量。它们读取目标线程的
``k_thread_runtime_stats_get()``，并用前后差值计算本次 case 的目标 CPU 消耗。IRQ case 还会把
``irq_isr_cycles`` 的增量计入实际值。

``target_forward_cycles_get()`` 只对目标线程的 contributor ``merged_cycles`` 求和。这样主比较
不会被 sample 控制线程、CPU-wide background contributor 或其他非目标线程污染。

profile 预热与窗口清理
======================

``prime_worker()`` 和 ``prime_workqueue()`` 会先运行多次目标 workload，让 estimator 收集成熟的
thread profile 或 activation profile。预热结束后调用 ``flush_estimator()``，让 estimator 有机会
读取并吸收已完成样本，同时通过短暂 sleep 让系统状态稳定。

``clear_arrivals()`` 用来清理 arrival window，避免前一个 case 的 wakeup 信息影响当前 case。

k_sched_lock() 的作用
=====================

多数 event-driven case 都在释放目标 workload 前调用 ``k_sched_lock()``，然后：

1. 触发目标事件，例如 ``trigger_worker()`` 或 ``k_work_submit_to_queue()``；
2. 立即读取 estimator；
3. 再 ``k_sched_unlock()`` 让目标 workload 真正运行。

这样 estimator 看到的是一个严格定义的状态：目标线程已经被唤醒/排队，但还没有开始消耗本次
CPU work。这个设计可以避免线程抢跑导致 estimate 与 actual 的比较不稳定。

输出设计
========

默认 summary 输出只保留最关键的三行：

* ``compare``：主比较指标、预测值、实际值；
* ``error``：cycles 误差、百分比误差、分类；
* ``cpu diag``：CPU-wide estimated/forward cycles。

verbose 输出用于调试 estimator 内部行为，会打印 estimate components、source mask、
contributor flags、burst profile、arrival events、merged cycles，以及 raw runtime/activation stats。

10 个 case 的设计意图
=====================

``periodic-thread``
   验证 runtime history 对稳定周期性 CPU work 的描述能力。主指标使用 ``history``，因为这里重点
   不是新到达 workload，而是历史窗口中的稳定负载。

``event-driven-thread``
   验证 semaphore 唤醒的普通事件线程。预热后，目标 forward estimate 应接近一次完整 activation
   的 CPU cycles。

``workqueue-deferred-work``
   验证 workqueue notify/waitq 唤醒路径是否被 estimator 正确归因。这个 case 能覆盖不同于普通
   semaphore worker 的 kernel wake path。

``sporadic-external-interrupt``
   验证单次 GPIO interrupt 触发 bottom-half worker 后，estimator 是否能把 IRQ 驱动的同步 wake
   归入目标 forward workload。没有 GPIO loopback 时会 skip。

``interrupt-burst``
   验证多个 IRQ 在 bottom-half 处理前到达时，arrival count 与 activation/event 口径是否仍然可解释。
   没有 GPIO loopback 时会 skip。

``mixed-background-foreground``
   同时覆盖历史 background work 和新到达 foreground work。主指标使用 ``history + target-forward``，
   因为它故意混合了 history-driven 与 arrival-driven 两类估计。

``ready-backlog``
   在 estimator 查询点同时释放两个已 profile worker，验证 ready backlog 对多个 runnable 目标线程的
   汇总能力，并验证 per-thread merge 能避免 ready 与 arrival 双重计数。

``workload-phase-change``
   先用小 workload 训练 profile，再切换到大 workload。这个 case 是刻意的负例，用来显示 profile
   面对 workload 突变时会有 adaptation lag。

``unprofiled-new-work``
   动态创建一个之前没有成熟 profile 的新线程。这个 case 验证 estimator 在缺少 profile 时会保持低
   confidence，并避免伪造看似精确的预测。

``peripheral-wait``
   worker 被唤醒后先 sleep，再执行少量 CPU work。它模拟 DMA-like peripheral wait，是一个
   multi-activation semantic event。当前 single-arrival forward estimate 只能预测可见 activation，
   因此这个 case 用来展示模型边界，而不是期待完美命中。
