.. _frdm_mcxe32b:

NXP FRDM-MCXE32B
#################

Overview
********

The FRDM-MCXE32B is an evaluation board for the NXP MCXE32B dual-core
Cortex-M7 microcontroller. The board is pin-compatible with the FRDM-MCXE31B
and shares the same peripheral set, adding a second Cortex-M7 core (cpu1).

.. image:: img/frdm_mcxe32b.webp
   :align: center
   :alt: FRDM-MCXE32B

Hardware
********

- NXP MCXE32B dual-core Cortex-M7 SoC running at up to 160 MHz
- 288 KB shared SRAM (144 KB per core)
- 3920 KB on-chip flash
- 32 KB ITCM + 64 KB DTCM per core
- LPUART, LPSPI, LPI2C, FlexCAN, SAI, ADC, EIRQ, GPIO
- RGB LED, user button
- Arduino-compatible headers

Supported Features
==================

The ``frdm_mcxe32b/mcxe32b/cpu0`` board target supports the following hardware features:

+-----------+------------+-------------------------------------+
| Interface | Controller | Driver/Component                    |
+===========+============+=====================================+
| UART      | on-chip    | serial port-polling;                |
|           |            | serial port-interrupt               |
+-----------+------------+-------------------------------------+
| GPIO      | on-chip    | gpio                                |
+-----------+------------+-------------------------------------+
| I2C       | on-chip    | i2c                                 |
+-----------+------------+-------------------------------------+
| SPI       | on-chip    | spi                                 |
+-----------+------------+-------------------------------------+
| CAN       | on-chip    | can                                 |
+-----------+------------+-------------------------------------+
| ADC       | on-chip    | adc                                 |
+-----------+------------+-------------------------------------+
| DMA       | on-chip    | dma                                 |
+-----------+------------+-------------------------------------+

The ``frdm_mcxe32b/mcxe32b/cpu1`` board target provides a minimal second-core
environment with UART console and access to the cpu1 SRAM region.

Serial Console
==============

UART5 (LPUART5) is used as the default console for both cpu0 and cpu1,
mapped to pins PTE14 (TX) and PTE3 (RX).

Programming and Debugging
*************************

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: frdm_mcxe32b/mcxe32b/cpu0
   :goals: build flash

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: frdm_mcxe32b/mcxe32b/cpu1
   :goals: build flash
