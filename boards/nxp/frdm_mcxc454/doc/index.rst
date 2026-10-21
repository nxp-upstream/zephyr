.. zephyr:board:: frdm_mcxc454

Overview
********

FRDM-MCXC454 is a compact and scalable development board for rapid
prototyping of the MCX C454 MCU. It offers industry standard headers
for easy access to the MCU's I/Os and integrated open-standard serial
interfaces.

Unlike the Cortex-M0+ parts of the MCX C family, the MCX C454 is built
around an Arm Cortex-M23 core.

Hardware
********

- MCXC454VLH Arm Cortex-M23 microcontroller running at up to 72 MHz
- 64LQFP package
- 256 KB flash
- 32 KB SRAM
- 5x low-power UART, 3x low-power I2C, 2x low-power SPI, 1x I3C
- 3x 32-bit timers, PWM, 16-bit ADC and comparator
- Touch sensing interface and segment LCD controller
- Tri-color LED
- User button SW2

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

The MCXC454 SoC has four pairs of pinmux/gpio controllers (PORT0/GPIO0,
PORT1/GPIO1, PORT2/GPIO2 and PORT3/GPIO3).

+-------+-------------+---------------------------+
| Name  | Function    | Usage                     |
+=======+=============+===========================+
| P0_2  | LPUART0_RX  | UART Console              |
+-------+-------------+---------------------------+
| P0_3  | LPUART0_TX  | UART Console              |
+-------+-------------+---------------------------+
| P2_3  | GPIO        | Red LED                   |
+-------+-------------+---------------------------+
| P2_7  | GPIO        | Green LED                 |
+-------+-------------+---------------------------+
| P2_10 | GPIO        | Blue LED                  |
+-------+-------------+---------------------------+
| P1_7  | GPIO        | User button SW2           |
+-------+-------------+---------------------------+

System Clock
============

The MCXC454 SoC is configured to use FRO12M running at 12 MHz as the
system clock source.

Serial Port
===========

The MCXC454 LPUART0 is used for the console.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).

Configuring a Debug Probe
=========================

A debug probe is used for both flashing and debugging the board.

Using J-Link
------------

Attach a :ref:`jlink-external-debug-probe` to the SWD connector of the
board. The J-Link software must support the MCXC454 device. Use the
``-r jlink`` option with west to use the jlink runner.

.. code-block:: console

   west flash -r jlink

Configuring a Console
=====================

Connect a USB to serial adapter to the LPUART0 pins listed in
`Connections and IOs`_, and use the serial terminal of your choice
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
   :board: frdm_mcxc454
   :goals: flash

Open a serial terminal, reset the board, and you should see the following
message in the terminal:

.. code-block:: console

   *** Booting Zephyr OS build v4.5.0-rc2-257-g1405adc9a604 ***
   Hello World! frdm_mcxc454/mcxc454

Debugging
=========

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: frdm_mcxc454
   :goals: debug

Open a serial terminal, step through the application in your debugger, and you
should see the following message in the terminal:

.. code-block:: console

   *** Booting Zephyr OS build v4.5.0-rc2-257-g1405adc9a604 ***
   Hello World! frdm_mcxc454/mcxc454

.. include:: ../../common/board-footer.rst.inc
