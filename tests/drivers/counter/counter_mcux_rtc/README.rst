.. SPDX-FileCopyrightText: Copyright 2026 NXP
.. SPDX-License-Identifier: Apache-2.0

MCUX RTC startup unit tests
##########################

These tests include the counter driver implementation with RAM-backed peripheral
registers and a controllable clock. They do not require an RTC or a crystal and
do not measure physical oscillator startup time.

The same source is compiled for seven configurations: OSC32 with and without a
system timer, fallback waiting with and without a system timer, LPO, the second
SIM mux layout, and a provider-owned clock without RTC oscillator control.
Only the driver sees the simulated no-system-timer configuration; ztest retains
its normal kernel timer.

The tests cover:

* An already-stable oscillator and stability observed before the deadline.
* A missing stable flag, stability at the deadline, and a delayed wakeup that
  first observes stability after the deadline.
* Time advancing while the ready register is sampled.
* Complete fallback waiting despite early wakeups.
* Bounded busy waiting without a system timer.
* Bypassing oscillator enable and waiting for LPO, SIM-selected external inputs,
  and provider-owned clocks.

Run on ``native_sim`` or ``qemu_cortex_m3`` using the normal Zephyr test workflow,
or build and run on another supported board. For example::

   west build -b qemu_cortex_m3 tests/drivers/counter/counter_mcux_rtc
   west build -t run

Physical crystal startup and counter accuracy still require separate hardware
validation.
