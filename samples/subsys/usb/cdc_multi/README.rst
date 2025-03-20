.. zephyr:code-sample:: usb-cdc-multi
   :name: USB CDC-MULTI
   :relevant-api: usbd_api _usb_device_core_api uart_interface

   Use USB CDC-ACM driver to enumerate multiple CDC devices on two controllers.

Overview
********

This sample app demonstrates use of a USB Communication Device Class (CDC)
Abstract Control Model (ACM) driver provided by the Zephyr project.  This sample
configures two USB controllers and a total of 8 CDC devices.  All CDC devices
enumerate and a terminal program can connect to them.  The debug console will
update when a connection is established or disconnected.  But the CDC devices
do not echo characters back.
This sample can be found under :zephyr_file:`samples/subsys/usb/cdc_multi` in the
Zephyr project tree.

This sample also performs a UART loopback test.  Two UARTs are connected
externally with HW flow control signals.  The UART test thread will periodically
send a message on both UARTs and confirm the message was received on both UARTs,
and confirms the line control signals.

Requirements
************

This project requires an USB device driver, which is available for multiple
boards supported in Zephyr.

Building and Running
********************

Running
=======

Plug the board into a host device, for example, a PC running Linux.
The board will be detected as shown by the Linux dmesg command:

.. code-block:: console

   usb 9-1: new full-speed USB device number 112 using uhci_hcd
   usb 9-1: New USB device found, idVendor=8086, idProduct=f8a1
   usb 9-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
   usb 9-1: Product: CDC-ACM
   usb 9-1: Manufacturer: Intel
   usb 9-1: SerialNumber: 00.01
   cdc_acm 9-1:1.0: ttyACM1: USB ACM device

The app prints on serial output (UART1), used for the console:

.. code-block:: console

   Wait for DTR

Open a serial port emulator, for example minicom
and attach it to detected CDC ACM device:

.. code-block:: console

   minicom --device /dev/ttyACM1

The app should respond on serial output with:

.. code-block:: console

   DTR set, start test
   Baudrate detected: 115200

Troubleshooting
===============

If the ModemManager runs on your operating system, it will try
to access the CDC ACM device and maybe you can see several characters
including "AT" on the terminal attached to the CDC ACM device.
You can add or extend the udev rule for your board to inform
ModemManager to skip the CDC ACM device.
For this example, it would look like this:

.. code-block:: none

   ATTRS{idVendor}=="8086" ATTRS{idProduct}=="f8a1", ENV{ID_MM_DEVICE_IGNORE}="1"

Hardware Setup
**************

This sample also performs a loopback UART test through two UARTs.  Some
platforms require the UART signals to be connected externally:

MCX-N9XX-EVK
============

On this platform, the DCD and DSR signals are inputs.  To test these signals,
they are both shorted externally to DTR.  The UART test toggles the DTR signals
periodically, and confirms the UART driver reads the DCD and DSR signals
correctly.  These are the HW changes required to run this test:
   - Short J2-pin18  (FC2_P0/RXD/P4_0)  to J2-pin12  (FC1_P1/TXD/P0_25)
   - Short J2-pin20  (FC2_P1/TXD/P4_1)  to J2-pin8   (FC1_P0/RXD/P0_24)
   - Short J1-pin4   (FC2_P2/RTS/P4_2)  to J2-pin6   (FC1_P3/CTS/P0_27)
   - Short J1-pin2   (FC2_P3/CTS/P4_3)  to J2-pin10  (FC1_P2/RTS/P0_26)
   - Short J20-pin25 (FC2_P4/DSR/P4_20) to J20-pin12 (FC1_P5/DTR/P0_13) and J20-pin28 (FC2_P6/DCD/P4_23)
   - Short J20-pin26 (FC2_P5/DTR/P4_21) to J20-pin9  (FC1_P4/DSR/P0_12) and J4-pin6   (FC1_P6/DCD/P0_14)
