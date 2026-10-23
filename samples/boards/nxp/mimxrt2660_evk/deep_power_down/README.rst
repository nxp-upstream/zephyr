.. SPDX-FileCopyrightText: Copyright 2026 NXP
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: mimxrt2660_evk_deep_power_down
   :name: RT2660 deep power down
   :relevant-api: sys_poweroff subsys_pm_sys counter_interface wuc_interface hwinfo_interface

   Enter Deep Power Down 1 or 2 on MIMXRT2660-EVK and wake from the VBAT LPTMR or SW5.

Overview
********

This sample runs repeated deep power down cycles on the i.MX RT2660. A deep power down wake is
a power-on reset, so every cycle restarts the application from ``main()``. On each boot the
sample:

* Reads the reset cause through :ref:`hwinfo <hwinfo_api>` and clears it. A cause with
  ``RESET_LOW_POWER_WAKE`` is a wake; anything else is a cold boot.
* Reads the cycle count from VBATCON ``GPR[0]``. The register is in the VBAT domain, which stays
  powered in both deep power down modes. A magic value in its high half marks the count as
  valid, and a cold boot starts the count from zero.
* Stops after ``CONFIG_SAMPLE_DPD_CYCLES`` (3 by default) wakes, prints ``Done, staying awake``
  and leaves the SoC running, so the next flash or debug session can attach.
* Otherwise arms the wakeup source, stores the next cycle number and enters deep power down.

Deep power down modes
*********************

The mode is selected at build time:

* ``CONFIG_SAMPLE_DPD_MODE_DPD2`` (default, needs ``CONFIG_POWEROFF``) calls ``sys_poweroff()``,
  which enters Deep Power Down 2. The VBAT SRAM is not retained.
* ``CONFIG_SAMPLE_DPD_MODE_DPD1`` (needs ``CONFIG_PM``) forces ``PM_STATE_SUSPEND_TO_DISK`` with
  ``pm_state_force()`` and puts the main thread to sleep, so the idle thread enters Deep Power
  Down 1. DPD1 retains the VBAT SRAM; the sample does not check its contents.

Wakeup sources
**************

Both wakeup sources are in the VBAT domain and are enabled through the POWERCON
:ref:`WUC (wakeup controller) <wuc_api>` driver:

* ``CONFIG_SAMPLE_DPD_WAKEUP_TIMER`` (default) wakes on a VBAT LPTMR (``vbat_lptmr``) alarm after
  ``CONFIG_SAMPLE_DPD_WAKEUP_SECONDS`` (5 by default), with no operator action.

  * With DPD2 the sample sets the alarm through the counter API, and the counter driver enables
    the wakeup source.
  * With DPD1, ``CONFIG_PM`` makes ``vbat_lptmr`` the low-power companion of the system timer,
    so the kernel owns its alarm. The sample sleeps for ``CONFIG_SAMPLE_DPD_WAKEUP_SECONDS``
    and the kernel arms the alarm with that timeout.

* ``CONFIG_SAMPLE_DPD_WAKEUP_BUTTON`` wakes on a press of SW5. The sample configures an
  interrupt on the falling edge of the ``wakeup_button`` pin (VBAT GPIO pin 4) and enables the
  wakeup source named in its ``wakeup-ctrls`` property.

.. warning::

   SW5 is also the on/off key of the PMIC. Press it briefly: a long press may switch the PMIC,
   and with it the board, off.

The board devicetree enables ``vbat_lptmr`` and ``gpio5`` and describes SW5, so the sample needs
no overlay.

Requirements
************

This sample uses the MIMXRT2660-EVK.

Building and Running
********************

DPD2 with the timer wakeup (default):

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mimxrt2660_evk/deep_power_down
   :board: mimxrt2660_evk/mimxrt2663/cm85
   :goals: build flash
   :compact:

DPD2 with the SW5 wakeup:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mimxrt2660_evk/deep_power_down
   :board: mimxrt2660_evk/mimxrt2663/cm85
   :goals: build flash
   :gen-args: -DCONFIG_SAMPLE_DPD_WAKEUP_BUTTON=y
   :compact:

DPD1 with the timer wakeup:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mimxrt2660_evk/deep_power_down
   :board: mimxrt2660_evk/mimxrt2663/cm85
   :goals: build flash
   :gen-args: -DCONFIG_PM=y -DCONFIG_SAMPLE_DPD_MODE_DPD1=y
   :compact:

DPD1 with the SW5 wakeup:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mimxrt2660_evk/deep_power_down
   :board: mimxrt2660_evk/mimxrt2663/cm85
   :goals: build flash
   :gen-args: -DCONFIG_PM=y -DCONFIG_SAMPLE_DPD_MODE_DPD1=y -DCONFIG_SAMPLE_DPD_WAKEUP_BUTTON=y
   :compact:

Open a serial terminal on the board console, then reset the board or power it on, so the run
starts from a cold boot. With the timer wakeup the cycles follow each other without operator
action. With the SW5 wakeup, press SW5 once for every ``Entering deep power down`` line.

Twister runs the four configurations as the ``dpd1.timer``, ``dpd2.timer``, ``dpd1.button`` and
``dpd2.button`` scenarios. The button scenarios need an operator, so they only run when the
``fixture_press_sw5`` fixture is given, for example with ``--fixture fixture_press_sw5``.

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS ***
   mimxrt2660_evk deep power down sample (DPD2, timer wakeup)
   Entering deep power down, cycle 1 of 3
   *** Booting Zephyr OS ***
   mimxrt2660_evk deep power down sample (DPD2, timer wakeup)
   Woke from deep power down, cycle 1 of 3
   Entering deep power down, cycle 2 of 3
   *** Booting Zephyr OS ***
   mimxrt2660_evk deep power down sample (DPD2, timer wakeup)
   Woke from deep power down, cycle 2 of 3
   Entering deep power down, cycle 3 of 3
   *** Booting Zephyr OS ***
   mimxrt2660_evk deep power down sample (DPD2, timer wakeup)
   Woke from deep power down, cycle 3 of 3
   Done, staying awake
