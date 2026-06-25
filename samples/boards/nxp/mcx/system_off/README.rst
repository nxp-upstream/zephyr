.. zephyr:code-sample:: nxp_mcx_system_off
   :name: NXP MCX system off
   :relevant-api: sys_poweroff wuc_interface

   Use system off (Deep Power Down) on NXP MCXA/MCXN SoCs and wake through
   the WUU wakeup controller.

Overview
********

This sample puts an NXP MCXA or MCXN SoC into system off, which is mapped
to the SoC Deep Power Down mode. In Deep Power Down the CORE power domain
is gated and the chip wakes through the reset routine, so a wakeup looks
like a cold boot.

The sample waits for a console key press before each system-off, then enters
Deep Power Down. Until a key is pressed the SoC stays awake and debuggable, so a
freshly programmed board never bricks its debug port by powering off on its own
before it can be re-flashed. The wakeup resets the board, which re-runs
``main()`` and prompts again - one key press per system-off cycle.

The wakeup source is configured through the :ref:`WUC (Wakeup Controller)
<wuc_api>` subsystem (the ``nxp,wuc-wuu`` driver). Two wakeup sources are
selectable at build time:

* ``CONFIG_SYSTEM_OFF_WAKEUP_TIMER`` (default) arms LPTMR0 through the counter
  alarm API and routes it to the core as a WUU internal-module wakeup source, so
  the board wakes itself after a few seconds with no external action.

* ``CONFIG_SYSTEM_OFF_WAKEUP_BUTTON`` waits for a transition on a WUU external
  pin. The board devicetree attaches the WUU source to its wakeup button node
  (next to the button ``gpios``) and aliases it as ``wakeup-button``; the sample
  simply enables ``DT_ALIAS(wakeup_button)``. No sample overlay is required. A
  board describes it like::

     &user_button_2 {
         wakeup-ctrls = <&wuu NXP_WUU_PIN_0_FALLING_INT>;
     };

     / {
         aliases {
             wakeup-button = &user_button_2;
         };
     };

Building and Running
********************

Build and flash the sample with the default (timer) wakeup source:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mcx/system_off
   :board: frdm_mcxn947/mcxn947/cpu0
   :goals: build flash
   :compact:

Sample Output
*************

.. code-block:: console

   frdm_mcxn947/mcxn947/cpu0 system off demo
   Press any key to enter system off
   Entering system off; the timer wakes it in 5 s
   <the timer wakes the SoC after 5 s through the reset routine; main() restarts>
   frdm_mcxn947/mcxn947/cpu0 system off demo
   Press any key to enter system off
   ...
