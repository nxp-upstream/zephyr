.. zephyr:code-sample:: nxp_mcx_mcxn_poweroff
   :name: NXP MCXN Series MCUs Poweroff
   :relevant-api: sys_poweroff

   Use poweroff on NXP MCXN series MCUs.

Overview
********

This example demonstrates how to power off NXP MCXN series MCUs.

Building, Flashing and Running
******************************

Building and Running for NXP FRDM-MCXN236
=========================================
Build the application for the :zephyr:board:`frdm_mcxn236/mcxn236` board.

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mcx/mcxn/poweroff
   :board: frdm_mcxn236/mcxn236
   :goals: build flash
   :compact:

Building and Running for NXP FRDM-MCXN947
=========================================
Build the application for the :zephyr:board:`frdm_mcxn947/mcxn947/cpu0` board.

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/mcx/mcxn/poweroff
   :board: frdm_mcxn947/mcxn947/cpu0
   :goals: build flash
   :compact:

Sample Output
=================
FRDM-MCXN236, FRDM-MCXN947 output
-------------------------------------------------------------

.. code-block:: console
   *** Booting Zephyr OS build v4.2.0-rc1-255-gf71b531cb990 ***
   Will wakeup after 5 seconds
   Press key to power off the system
   Powering off
