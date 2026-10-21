:orphan:

..
  See
  https://docs.zephyrproject.org/latest/releases/index.html#migration-guides
  for details of what is supposed to go into this document.

.. _migration_4.6:

Migration guide to Zephyr v4.6.0 (Working Draft)
################################################

This document describes the changes required when migrating your application from Zephyr v4.5.0 to
Zephyr v4.6.0.

Any other changes (not directly related to migrating applications) can be found in
the :ref:`release notes<zephyr_4.6>`.

.. contents::
    :local:
    :depth: 2

Common
******

Build System
************

Kernel
******

Boards
******

Device Drivers and Devicetree
*****************************

.. Only place contents common to all device drivers here. Contents specific to one driver subsystem
   goes into its own subsection, below.

.. Group contents in this section by subsystem, e.g.:
..
.. ADC
.. ===
..
.. ...

.. zephyr-keep-sorted-start re(^\w) ignorecase

Watchdog
========

* On MCXN/MCXA, :dtcompatible:`nxp,lpc-wwdt` accepts setup with no pause options.
  Sleep-pause, debugger-pause and unknown options return ``-ENOTSUP`` instead of
  being ignored. Repeated setup returns ``-EBUSY`` without feeding the watchdog.
  Disabling a running instance halts its functional clock and uninstalls its timeout.
  The driver does not support restarting that instance in the same software boot:
  subsequent install and setup return ``-ENOTSUP``, feed returns ``-EINVAL``, and
  repeated disable returns ``-EFAULT``. A power-domain cycle does not permit restart.
  These API checks also apply with :kconfig:option:`CONFIG_PM_DEVICE` disabled.
* With system PM enabled, the MCX WWDT driver selects
  :kconfig:option:`CONFIG_PM_POLICY_DEVICE_CONSTRAINTS` and holds the states listed
  in ``zephyr,disabling-power-states`` while the watchdog runs. The conservative
  defaults exclude Deep Sleep, Power Down and Deep Power Down. A board may relax
  conditional entries only after configuring and verifying the required low-power
  functional clock. The driver does not configure SCG or oscillator retention.
  Successful disable releases the running-state constraints. PM resume preserves
  the disabled state rather than restarting the old countdown.
* Consumers of a runtime-enabled MCX WWDT must hold a runtime reference before setup
  and retain it while the watchdog runs. Running-device suspend is rejected with
  ``-ENOTSUP``; an attempted final synchronous runtime put therefore retains its
  reference. Do not use a final asynchronous put for a running watchdog.
  After successful disable, the consumer can release its runtime reference.
  An unexpected loss of watchdog state is reported rather than silently starting
  a new timeout. The MCX-only behavior does not change other WWDT SoC backends.

.. zephyr-keep-sorted-stop

Bluetooth
*********

Networking
**********

Other subsystems
****************

* Task watchdog now retains the hardware backend's runtime reference for its service
  lifetime, rejects repeated initialization with ``-EALREADY``, and propagates hardware
  setup failures from ``task_wdt_add()`` without allocating a channel. Applications
  must check the returned errors and avoid requesting pause options their backend
  does not support. The MCX task-watchdog sample disables its unsupported
  debugger-pause option without changing the global default for other backends.

Modules
*******

Architectures
*************
