.. zephyr:board:: x_mr_kw45_ble_lr

Overview
********

The X-MR-KW45-BLE-LR is a compact form-factor board based on the NXP KW45B41Z
wireless MCU. The board is intended for Bluetooth® Low Energy applications,
including Long Range (Coded PHY), and provides access to key MCU peripherals
such as CAN-FD and FlexIO.

The KW45 family integrates an Arm® Cortex®-M33 core with a 2.4 GHz radio
subsystem optimized for Bluetooth LE. The radio subsystem offloads wireless
processing from the main CPU, enabling efficient low-power operation for
connected applications.

Hardware
********

- KW45B41Z Arm Cortex-M33 microcontroller
- On-chip Flash memory
- On-chip SRAM and Tightly Coupled Memory (TCM)
- Bluetooth Low Energy radio with Long Range support
- CAN-FD interface (external transceiver required)
- J-Link compatible debug interface

Supported Features
==================

.. zephyr:board-supported-hw::

Fetch Binary Blobs
******************

Some Bluetooth configurations require NXP-provided firmware binaries for the
radio subsystem. These binaries can be fetched using:

.. code-block:: console

   west blobs fetch hal_nxp

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).
