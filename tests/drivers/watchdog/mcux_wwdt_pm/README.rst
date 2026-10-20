.. SPDX-FileCopyrightText: Copyright 2026 NXP
.. SPDX-License-Identifier: Apache-2.0

MCUX WWDT power-management tests
###############################

These tests compile the production MCUX WWDT implementation with RAM registers and
controlled HAL/clock operations. The PM core, runtime reference counting and
devicetree-based policy constraints are real Zephyr implementations. Two test devices
exercise independent ownership of overlapping power-state constraints.

The task-watchdog tests exercise its production implementation with a failing watchdog
backend. Hardware oscillator retention, counting accuracy and reset behavior are not
emulated and require board validation. The MCXN compile definition selects the driver's
MCX path only in the test application; it does not turn QEMU into an MCX SoC.
