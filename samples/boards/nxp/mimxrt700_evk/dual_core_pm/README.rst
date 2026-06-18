.. zephyr:code-sample:: nxp_rt700_dual_core_pm
   :name: RT700 EVK dual-core power management

   Exercise active, sleep, and deep-sleep combinations on both RT700 EVK
   Cortex-M33 cores.

Overview
********

This sample builds two Zephyr images for the MIMXRT700-EVK:

* the main image runs on CM33 CPU0 and boots CM33 CPU1 through the existing
  ``CONFIG_SECOND_CORE_MCUX`` board initialization path;
* the remote image runs on CM33 CPU1.

Both images enable Zephyr system power management and register a PM notifier.
CPU0 acts as the test orchestrator and sends simple MU commands to the CPU1
remote image. CPU1 acknowledges that it is ready, CPU0 sends a GO command, and
then both cores run the selected measurement window.

The test matrix covers:

* both cores active;
* CPU0 sleep while CPU1 stays active;
* CPU1 sleep while CPU0 stays active;
* both cores sleep;
* CPU0 deep-sleep while CPU1 stays active;
* CPU1 deep-sleep while CPU0 stays active;
* both cores deep-sleep.

The sleep cases force ``PM_STATE_RUNTIME_IDLE`` for the next idle entry. The
deep-sleep cases force ``PM_STATE_SUSPEND_TO_IDLE`` for the next idle entry.

The serial output gives a coarse software confirmation that both cores booted
and that PM entry/exit callbacks are firing. Use the stable sleep windows for
board current measurement.

Building and Running
********************

Build the CPU0 and CPU1 images together with sysbuild:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mimxrt700_evk/dual_core_pm
   :board: mimxrt700_evk/mimxrt798s/cm33_cpu0
   :west-args: --sysbuild
   :goals: build flash
   :compact:

After reset, the console should show CPU0 stepping through the matrix and CPU1
responding to each command, for example:

.. code-block:: console

   RT700 dual-core PM sample: CPU0 started on mimxrt700_evk/mimxrt798s/cm33_cpu0
   RT700 dual-core PM sample: CPU1 started on mimxrt700_evk/mimxrt798s/cm33_cpu1
   CPU0: case 1: active baseline
   CPU1: prepared for active window
   CPU0: entering active window
   CPU1: entering active window
   CPU1: active window complete
   CPU0: local active window complete
   CPU0: case 4: CPU0 and CPU1 sleep
   CPU1: prepared for sleep window
   CPU0: entering sleep window

Measurement Notes
*****************

Measure RT700 SoC current through JP21 on the EVK. Remove the default JP21
jumper and insert the current meter or power analyzer in series. JP21 measures
the RT700 SoC rail, so it does not isolate CPU0 and CPU1 independently. To see
the contribution of each core, compare the active baseline, single-core sleep,
single-core deep-sleep, and coordinated sleep/deep-sleep windows.

The default low-power window is 10 seconds. It can be changed at build time:

.. code-block:: console

   west build -p always --sysbuild \
     -b mimxrt700_evk/mimxrt798s/cm33_cpu0 \
     samples/boards/nxp/mimxrt700_evk/dual_core_pm \
      -- -DSB_CONFIG_RT700_DUAL_CORE_PM_SLEEP_SECONDS=30
