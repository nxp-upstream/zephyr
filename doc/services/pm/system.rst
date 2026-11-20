.. _pm-system:

System Power Management
#######################

Introduction
************

The kernel enters the idle state when it has nothing to schedule.
Enabling :kconfig:option:`CONFIG_PM` allows the kernel to call upon the
power management subsystem to put an idle system into one of the supported power states.
The kernel requests an amount of time it would like to suspend, then the PM subsystem decides
the appropriate power state to transition to based on the configured power management policy.

It is the application's responsibility to set up a wake-up event.
A wake-up event will typically be an interrupt triggered by an SoC peripheral module.
Examples include a SysTick, RTC, counter, or GPIO.
Keep in mind that depending on the SoC and the power mode in question,
not all peripherals may be active, and therefore
some wake-up sources may not be usable in all power modes.

The following diagram describes system power management:

.. graphviz::
   :caption: System power management

   digraph G {
       compound=true
       node [height=1.2 style=rounded]

       lock [label="Lock interrupts"]
       config_pm [label="CONFIG_PM" shape=diamond style="rounded,dashed"]
       forced_state [label="state forced ?", shape=diamond style="rounded,dashed"]
       config_system_managed_device_pm [label="CONFIG_PM_DEVICE" shape=diamond style="rounded,dashed"]
       config_system_managed_device_pm2 [label="CONFIG_PM_DEVICE" shape=diamond style="rounded,dashed"]
       pm_policy [label="Check policy manager\nfor a power state "]
       pm_suspend_devices [label="Suspend\ndevices"]
       pm_resume_devices [label="Resume\ndevices"]
       pm_state_set [label="Enter power state\n(SoC API)" style="rounded,bold"]
       pm_system_resume [label="Resume bookkeeping\n(post ops, notify, clock)" style="rounded,bold"]
       unlock_irq [label="Unlock interrupts"]
       handle_interrupts [label="Handle interrupts\nand schedule threads"]
       k_cpu_idle [label="k_cpu_idle()"]

       subgraph cluster_idle {
           style=dashed
           label = "idle()"

           lock -> config_pm
           config_pm -> k_cpu_idle [label="no"]
           k_cpu_idle -> unlock_irq
       }

       subgraph cluster_pm_system_suspend {
           style=dashed
           label = "pm_system_suspend()"

           forced_state -> config_system_managed_device_pm [label="yes"]
           forced_state -> pm_policy [label="no"]
           pm_policy -> config_system_managed_device_pm
           config_system_managed_device_pm -> pm_state_set [label="no"]
           config_system_managed_device_pm -> pm_suspend_devices [label="yes"]
           pm_suspend_devices -> pm_state_set
           pm_state_set -> config_system_managed_device_pm2
           config_system_managed_device_pm2 -> pm_resume_devices [label="yes"]
           config_system_managed_device_pm2 -> pm_system_resume [label="no"]
           pm_resume_devices -> pm_system_resume
       }

       {rankdir=LR k_cpu_idle; forced_state}
       config_pm -> forced_state [label="yes"]
       pm_policy -> k_cpu_idle [label="PM_STATE_ACTIVE\n(no power state meets requirements)"]
       pm_system_resume -> unlock_irq [constraint=false]
       unlock_irq -> handle_interrupts
       handle_interrupts -> lock:n [label="no runnable thread"]
   }

The idle thread locks interrupts before calling :c:func:`pm_system_suspend`
and keeps ownership of the original architecture interrupt key. If the PM
subsystem enters a low-power state, PM resume bookkeeping runs with interrupts
still locked. By default, system-managed devices are resumed first, followed
by :c:func:`pm_state_exit_post_ops`, PM exit notifications, and system clock
idle-exit accounting. A device resume callback cannot rely on a timer-based
:c:func:`k_busy_wait` if the low-power state stopped that timer.
After that bookkeeping is complete and
:c:func:`pm_system_suspend` returns, the idle thread restores the original
interrupt key.

.. note::

   The default-off experimental option
   :kconfig:option:`CONFIG_PM_EARLY_SYSTEM_TIMER_RESUME` changes the sequence to
   :c:func:`pm_state_exit_post_ops`, system clock idle-exit accounting, device
   resume, then PM exit notifications. This can make a timer-based
   :c:func:`k_busy_wait` usable in device resume callbacks, but requires
   platform-specific validation. SoC post-ops must not depend on resumed
   devices, and timer idle-exit must not depend on still-suspended devices.
   Timer idle-exit may also invoke :c:struct:`k_timer` expiry functions before
   devices are resumed.

Architectures and SoCs that do not select ``CONFIG_PM_STATE_SET_IRQ_UNLOCKED``
use the architecture hooks immediately around the low-power instruction so that
it can still observe a wake event without dispatching the wake-source ISR first.
In that mode, :c:func:`pm_state_set` and
:c:func:`pm_state_exit_post_ops` must be used for SoC-specific hardware work
only, not as final interrupt unmask points.

When this default contract is used, the interrupt ownership sequence is:

.. mermaid::
   :caption: System PM interrupt restore ownership
   :alt: Sequence diagram showing idle locking interrupts, PM entering the SoC
       power state, architecture PM state hooks allowing wake without ISR
       dispatch, PM resume bookkeeping, and idle unlocking interrupts.

   sequenceDiagram
        participant Workers as Other threads
        participant Idle as idle()<br/>(idle thread context)
        participant PM as pm_system_suspend()
        participant SoC as pm_state_set()<br/>(SoC hook)
        participant HW as Hardware

        loop Idle cycle
            Note over Workers: Thread enters SLEEP/PENDING state<br/>e.g. k_sleep() / k_sem_take()
            alt Runnable thread(s) remaining
                Workers-->>Workers: Schedule another thread
            else No runnable thread remaining
                Workers-->>Idle: Switch to idle thread
            end
            Idle->>Idle: Lock interrupts
            Note left of Idle: Interrupts are locked<br/>during PM sequence
            Idle->>PM: Suspend idle CPU
            PM->>PM: Suspend devices<br/>(if applicable)
            PM->>PM: Set idle timeout
            PM->>PM: Notify PM state entry
            PM->>SoC: Enter power state
            SoC->>SoC: SoC-specific pre-entry operations
            SoC->>SoC: Run arch PM prepare hook
            SoC->>HW: Trigger low-power state entry
            HW-->>SoC: Wake-up event occurs
            SoC->>SoC: Run arch PM finish hook
            SoC->>SoC: SoC-specific post-wakeup operations
            SoC-->>PM: Power state exit complete
            PM->>PM: Resume devices<br/>(if applicable)
            PM->>PM: Run post ops, notify exit, clock idle exit
            PM-->>Idle: Resume complete
            Idle->>Idle: Unlock interrupts
            HW-->>Idle: Pending interrupts are handled
            Idle-->>Workers: Idle thread yields<br/>or is preempted
            Note over Workers: Non-idle thread starts executing
        end

SoC implementations that use this default contract must not unmask interrupts
from :c:func:`pm_state_set` or :c:func:`pm_state_exit_post_ops`. Legacy
implementations that still call ``irq_unlock(0)``, ``arch_irq_unlock(0)``,
``__enable_irq()``, or equivalent operations from those hooks must select
``CONFIG_PM_STATE_SET_IRQ_UNLOCKED`` until they are migrated.

.. note::

    The ordering guarantee above applies only to interrupts that
    :c:func:`arch_irq_lock` can mask. Zero-latency interrupts are outside this
    ordering. See :ref:`zlis`.


Power States
============

The power management subsystem defines a set of states described by the
power consumption and context retention associated with each of them.

The set of power states is defined by :c:enum:`pm_state`. In general, lower power states
(higher index in the enum) will offer greater power savings and have higher wake latencies.

.. _pm-state-model:

State scope and the Linux model
-------------------------------

A power state describes a transition, not a hardware device or a power domain.
Distinguish the caller requesting the transition from the hardware affected by it.
An idle CPU can request a state that affects only that CPU, or a shared state that
also affects a cluster, memory, clocks, or peripherals.

Linux separates these responsibilities:

* `CPU idle time management`_ selects a state for an idle logical CPU. A request
  can affect a larger shared unit when the other CPUs and the platform satisfy
  its entry conditions. Its devicetree `CPU idle states binding`_ places
  ``idle-states`` under ``/cpus`` and associates states with CPUs through
  ``cpu-idle-states`` phandles. Thus, not every child of ``/cpus`` is a CPU.
* `System sleep`_ is a coordinated transition of the whole system, rather than
  an individual CPU's idle request. It includes preparation of devices and
  wake-up sources and, depending on the state, additional platform operations.
* `Generic PM domains`_ describe independently managed power domains and their
  dependencies. A device's ``power-domains`` property identifies its provider;
  it is not a list of CPU idle or system sleep states.

Zephyr uses :c:enum:`pm_state` and per-CPU ``cpu-power-states`` lists for its
idle-driven system PM path. State selection occurs in the idle thread, and
the SoC implements entry through :c:func:`pm_state_set`. Device suspension can
also be part of this path. This is not a one-to-one implementation of Linux's
separate CPUIdle and system sleep subsystems. In particular, a Zephyr state
named ``suspend-to-idle`` must not be interpreted as implementing all of the
Linux system suspend-to-idle procedure.

For each platform state, document:

* Whether entry affects one CPU, a shared CPU cluster, or the whole system, and
  what execution context, memory, clocks, and peripheral state is retained.
* Which CPUs can request it, what other CPUs must do first, and whether hardware,
  firmware, or SoC code coordinates entry into shared resources.
* Which devices must be suspended or held active, which wake-up sources remain
  usable, and how timekeeping and lost context are restored.
* The platform-specific meaning of ``substate-id`` and the characterized
  residency and exit latency used by the PM policy.

Neither a CPU phandle reference nor the location of a state definition supplies
this coordination. Sharing one state node between CPUs does not by itself make
entry into a shared hardware state safe.

.. _pm-state-devicetree:

Devicetree authoring
--------------------

Keep state definitions, CPU state references, and power-domain membership
separate:

* For new Zephyr descriptions, prefer one root-level ``/power-states`` container
  for ``zephyr,power-state`` definitions. This gives the state catalog a location
  that does not imply CPU-only hardware effects. It is a Zephyr authoring
  convention, not the Linux ``idle-states`` binding.
* Reference the supported states from each CPU's ``cpu-power-states`` property.
  The reference, not the container's location, determines that CPU's available
  state list. Reuse a state node only when its entry contract and timing apply
  to all referencing CPUs.
* Existing ``/cpus/power-states`` layouts remain supported; this guidance does
  not deprecate them or require a bulk migration. Do not split the catalog
  between the two locations: lookup of disabled states prefers
  ``/cpus/power-states`` when both containers exist.
* Use ``power-domains`` to describe a device's relationship to its power-domain
  provider, as explained in :ref:`pm-power-domain`. A ``zephyr,power-state`` node
  is not a provider and must not be used as a power-domain phandle target.
* Retain the existing Zephyr binding names. Linux's ``cpu-idle-states``,
  ``arm,idle-state``, and ``domain-idle-states`` are not replacements for Zephyr
  properties or compatibles merely because their concepts are related.
  Follow :ref:`dt-writing-bindings` when introducing or extending bindings.

The following fragment illustrates a shared catalog referenced by an existing
``cpu0`` node. The timings and state identifiers are illustrative; a platform
must provide matching SoC entry code and measured timing values.

.. code-block:: devicetree

   / {
           power-states {
                   core_idle: core-idle {
                           compatible = "zephyr,power-state";
                           power-state-name = "runtime-idle";
                           substate-id = <1>;
                           min-residency-us = <5000>;
                           exit-latency-us = <100>;
                   };

                   system_sleep: system-sleep {
                           compatible = "zephyr,power-state";
                           power-state-name = "suspend-to-ram";
                           substate-id = <2>;
                           min-residency-us = <20000>;
                           exit-latency-us = <500>;
                   };
           };
   };

   &cpu0 {
           cpu-power-states = <&core_idle &system_sleep>;
   };

With the default residency policy, list states from shallower to deeper, with
nondecreasing ``min-residency-us + exit-latency-us`` thresholds. The policy uses
the phandle list order; it does not sort states by enum value or ``substate-id``.
Disabled state nodes are excluded from normal policy selection, but can be
looked up for :c:func:`pm_state_force`. Disabling a state is not disabling a CPU.

For :ref:`pm-device-system-pm`, setting ``zephyr,pm-device-disabled`` skips
system-managed device suspension for that state; it does not turn devices off
or declare that they lose power. ``PM_STATE_RUNTIME_IDLE`` also skips this
device suspension. Device power loss dependencies and policy locks are described
in :ref:`pm-device-constraint`.

Implementation and review checks
--------------------------------

When changing PM state handling or platform descriptions:

* Enumerate CPU nodes with ``DT_FOREACH_CPU_STATUS_OKAY`` and its variants, not
  all enabled children of ``/cpus``. Auxiliary nodes must not occupy CPU slots.
  CPU enumeration preserves declaration order; filtering is not sorting by CPU
  ``reg`` value or remapping kernel CPU IDs.
* Test auxiliary nodes before, between, and after CPU nodes, as well as disabled
  CPUs. Check each valid CPU's state count and state list, not just compilation.
* Check that enabled and disabled state handling preserves the supported
  container locations, CPU references, and forced-state lookup.
* Validate the entry and wake-up contract on the target platform, including
  shared-state coordination, device context loss, and timer restoration.
  Devicetree validation alone cannot establish those runtime properties.

.. _CPU idle time management:
   https://docs.kernel.org/admin-guide/pm/cpuidle.html
.. _CPU idle states binding:
   https://github.com/torvalds/linux/blob/v6.12/Documentation/devicetree/bindings/cpu/idle-states.yaml
.. _System sleep:
   https://docs.kernel.org/admin-guide/pm/sleep-states.html
.. _Generic PM domains:
   https://github.com/torvalds/linux/blob/v6.12/Documentation/devicetree/bindings/power/power-domain.yaml

Power Management Policies
=========================

The power management subsystem supports the following power management policies:

* Residency based
* Application defined

The policy manager is the component of the power management subsystem responsible
for deciding which power state the system should transition to.
The policy manager can only choose between states that have been defined for the platform.
Other constraints placed upon the decision may include locks disallowing certain power states,
or various kinds of minimum and maximum latency values, depending on the policy.

More details on the states definition can be found in the
:dtcompatible:`zephyr,power-state` binding documentation.

Residency
---------

Under the residency policy, the system will enter the power state which offers the highest
power savings, with the constraint that the sum of the minimum residency value (see
:dtcompatible:`zephyr,power-state`) and the latency to exit the mode must be
less than or equal to the system idle time duration scheduled by the kernel.

Thus the core logic can be summarized with the following expression:

.. code-block:: c

   if (time_to_next_scheduled_event >= (state.min_residency_us + state.exit_latency)) {
      return state
   }

Application
-----------

The application defines the power management policy by implementing the
:c:func:`pm_policy_next_state` function. In this policy, the application is free
to decide which power state the system should transition to based on the
remaining time until the next scheduled timeout.

An example of an application that defines its own policy can be found in
:zephyr_file:`tests/subsys/pm/power_mgmt/`.

Custom ticks hook
-----------------

In addition to the kernel tick expiration and the PM policy event list,
applications and SoC-specific code may need to derive the next wake-up time
from proprietary or hardware-specific data sources. Examples include hardware
registers, binary-only third-party modules, or complex/legacy data structures
that are not practical to model as standard PM policy events.

When :kconfig:option:`CONFIG_PM_CUSTOM_TICKS_HOOK` is enabled, the PM core calls the
optional function :c:func:`pm_policy_next_custom_ticks()` during
:c:func:`pm_system_suspend()`. The hook returns the ticks to the next custom
event, or ``K_TICKS_FOREVER`` if no custom event needs to be considered.

The PM core then selects the earliest expiration among:

* kernel tick expiration,
* PM policy event list ticks (from :c:func:`pm_policy_next_event_ticks()`),
* the custom ticks value (from :c:func:`pm_policy_next_custom_ticks()`).

This mechanism allows boards and applications to integrate additional timing
sources into the system power management decisions without modifying the PM
event list infrastructure.

.. _pm-policy-power-states:

Policy and Power States
------------------------

The power management subsystem allows different Zephyr components and
applications to configure the policy manager to block the system from transitioning
into certain power states. This can be used by devices when executing tasks in
background to prevent the system from going to a specific state where it would
lose context. See :c:func:`pm_policy_state_lock_get`.

Examples
========

Some helpful examples showing different power management features:

* :zephyr_file:`samples/boards/st/power_mgmt/blinky/`
* :zephyr_file:`samples/boards/espressif/deep_sleep/`
* :zephyr_file:`samples/subsys/pm/device_pm/`
* :zephyr_file:`tests/subsys/pm/power_mgmt/`
* :zephyr_file:`tests/subsys/pm/power_mgmt_soc/`
* :zephyr_file:`tests/subsys/pm/power_states_api/`
