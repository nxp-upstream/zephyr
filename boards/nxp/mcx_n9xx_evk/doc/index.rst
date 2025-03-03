.. zephyr:board:: mcx_n9xx_evk

work in progress

Hardware
********

- MCX-N947 Dual Arm Cortex-M33 microcontroller running at 150 MHz
- 2MB dual-bank on chip Flash
- 512 KB RAM
- USB high-speed (Host/Device) with on-chip HS PHY. HS USB Type-C connectors
- 10x LP Flexcomms each supporting SPI, I2C, UART
- 2x FlexCAN with FD, 2x I3Cs, 2x SAI
- 1x Ethernet with QoS
- On-board MCU-Link debugger with CMSIS-DAP

For more information about the MCX-N947 SoC and MCX-N9XX-EVK board, see:

- `MCX-N947 SoC Website`_
- `MCX-N947 Datasheet`_
- `MCX-N947 Reference Manual`_

Supported Features
==================

The MCX-N9XX-EVK board configuration supports the following hardware features:

+-----------+------------+-------------------------------------+
| Interface | Controller | Driver/Component                    |
+===========+============+=====================================+
| NVIC      | on-chip    | nested vector interrupt controller  |
+-----------+------------+-------------------------------------+
| SYSTICK   | on-chip    | systick                             |
+-----------+------------+-------------------------------------+
| PINMUX    | on-chip    | pinmux                              |
+-----------+------------+-------------------------------------+
| GPIO      | on-chip    | gpio                                |
+-----------+------------+-------------------------------------+
| UART      | on-chip    | serial port-polling;                |
|           |            | serial port-interrupt               |
+-----------+------------+-------------------------------------+
| CLOCK     | on-chip    | clock_control                       |
+-----------+------------+-------------------------------------+
| FLASH     | on-chip    | soc flash                           |
+-----------+------------+-------------------------------------+
| FLEXSPI   | on-chip    | flash programming                   |
+-----------+------------+-------------------------------------+
| HWINFO    | on-chip    | Unique device serial number         |
+-----------+------------+-------------------------------------+
| USBHS     | on-chip    | USB device                          |
+-----------+------------+-------------------------------------+

Targets available
==================

The default configuration file
only enables the first core. CPU0 is the only target that can run standalone.

CPU1 does not work without CPU0 enabling it.

System Clock
============

The MCX-N947 SoC is configured to use PLL0 running at 150MHz as a source for
the system clock.

Serial Port
===========

The MCX-N9XX-EVK SoC has 10 FLEXCOMM interfaces for serial communication.
Flexcomm 4 is configured as UART for the console.

Programming and Debugging
*************************

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).

Configuring a Debug Probe
=========================

A debug probe is used for both flashing and debugging the board. This board is
configured by default to use the MCU-Link CMSIS-DAP Onboard Debug Probe.

Using LinkServer
----------------

Linkserver is the default runner for this board, and supports the factory
default MCU-Link firmware. Follow the instructions in
:ref:`mcu-link-cmsis-onboard-debug-probe` to reprogram the default MCU-Link
firmware. This only needs to be done if the default onboard debug circuit
firmware was changed. To put the board in ``DFU mode`` to program the firmware,
short jumper J??.

Using J-Link
------------

There are two options. The onboard debug circuit can be updated with Segger
J-Link firmware by following the instructions in
:ref:`mcu-link-jlink-onboard-debug-probe`.
To be able to program the firmware, you need to put the board in ``DFU mode``
by shortening the jumper J??.
The second option is to attach a :ref:`jlink-external-debug-probe` to the
10-pin SWD connector (J??) of the board. Additionally, the jumper J?? must
be shortened.
For both options use the ``-r jlink`` option with west to use the jlink runner.

.. code-block:: console

   west flash -r jlink

Configuring a Console
=====================

Connect a USB cable from your PC to J??, and use the serial terminal of your choice
(minicom, putty, etc.) with the following settings:

- Speed: 115200
- Data: 8 bits
- Parity: None
- Stop bits: 1

Flashing
========

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: mcx_n9xx_evk/mcxn947/cpu0
   :goals: flash

Open a serial terminal, reset the board (press the RESET button), and you should
see the following message in the terminal:

.. code-block:: console

   *** Booting Zephyr OS build v3.6.0-479-g91faa20c6741 ***
   Hello World! mcx_n9xx_evk/mcxn947/cpu0

Flashing to QSPI
================

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :app: zephyr/samples/hello_world
   :board: mcx_n9xx_evk/mcxn947/cpu0/qspi
   :gen-args: -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE=\"bootloader/mcuboot/root-rsa-2048.pem\" -DCONFIG_BOOTLOADER_MCUBOOT=y
   :goals: flash


In order to load Zephyr application from QSPI you should program a bootloader like
MCUboot bootloader to internal flash. Here are the steps.

.. zephyr-app-commands::
   :app: bootloader/mcuboot/boot/zephyr
   :board: mcx_n9xx_evk/mcxn947/cpu0/qspi
   :goals: flash

Open a serial terminal, reset the board (press the RESET button), and you should
see the following message in the terminal:

.. code-block:: console

  *** Booting MCUboot v2.1.0-rc1-2-g9f034729d99a ***
  *** Using Zephyr OS build v3.6.0-4046-gf279a03af8ab ***
  I: Starting bootloader
  I: Primary image: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
  I: Secondary image: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
  I: Boot source: none
  I: Image index: 0, Swap type: none
  I: Bootloader chainload address offset: 0x0
  I: Jumping to the first image slot
  *** Booting Zephyr OS build v3.6.0-4046-gf279a03af8ab ***
  Hello World! mcx_n9xx_evk/mcxn947/cpu0/qspi

Debugging
=========

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: mcx_n9xx_evk/mcxn947/cpu0
   :goals: debug

Open a serial terminal, step through the application in your debugger, and you
should see the following message in the terminal:

.. code-block:: console

   *** Booting Zephyr OS build v3.6.0-479-g91faa20c6741 ***
   Hello World! mcx_n9xx_evk/mcxn947/cpu0

Troubleshooting
===============

.. include:: ../../common/segger-ecc-systemview.rst
   :start-after: segger-ecc-systemview

.. _MCX-N947 SoC Website:
   https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/general-purpose-mcus/mcx-arm-cortex-m/mcx-n-series-microcontrollers/mcx-n94x-54x-highly-integrated-multicore-mcus-with-on-chip-accelerators-intelligent-peripherals-and-advanced-security:MCX-N94X-N54X

.. _MCX-N947 Datasheet:
   https://www.nxp.com/docs/en/data-sheet/MCXNx4xDS.pdf

.. _MCX-N947 Reference Manual:
   https://www.nxp.com/webapp/Download?colCode=MCXNX4XRM
