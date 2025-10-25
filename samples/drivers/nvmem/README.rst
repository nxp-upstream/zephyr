.. zephyr:code-sample:: nvmem
   :name: NVMEM
   :relevant-api: nvmem_interface

   Store a boot count value in NVMEM.

Overview
********

This sample demonstrates the :ref:`NVMEM driver API <nvmem_api>` in a simple boot counter
application.

Building and Running
********************

In case the target board has defined an NVMEM device with alias ``nvmem-0`` the
sample can be built without further ado. This applies for example to the
:zephyr:board:`native_sim` board:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/nvmem
   :host-os: unix
   :board: native_sim
   :goals: run
   :compact:

Otherwise either a board specific overlay needs to be defined, or a shield must
be activated. Any board with Arduino headers can for example build the sample
as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/nvmem
   :board: nrf52840dk/nrf52840
   :goals: build
   :shield: x_nucleo_eeprma2
   :compact:

For :zephyr:board:`gd32f450i_eval` board. First bridge the JP5 to USART with the jumper cap,
Then the sample can be built and executed for the  as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/nvmem
   :board: gd32f450i_eval
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

    Found NVMEM device "EEPROM_M24C02"
    Using nvmem with size of: 256.
    Device booted 7 times.
    Reset the MCU to see the increasing boot counter.
