.. zephyr:code-sample:: usb-host-hid
   :name: USB Host HID Boot
   :relevant-api: usb_host_core_api input_interface

   Read key presses and mouse movement from a USB HID boot device.

Overview
********

This sample uses the USB host HID Boot class driver to poll a USB keyboard or
mouse and reports the decoded events through the input subsystem. The device is
enumerated and configured automatically on connection.

Only devices exposing the HID boot protocol interface (``bInterfaceSubClass``
1, ``bInterfaceProtocol`` 1 for keyboards and 2 for mice) are supported. A
composite keyboard and mouse device needs ``CONFIG_USBH_HID_BOOT_INSTANCES_COUNT``
set to at least 2.

Requirements
************

A board with a USB host controller driver and a ``zephyr_uhc0`` node label on
the controller to use. The host port has to supply VBUS to the connected
device.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/usb/host_hid
   :board: frdm_mcxa577
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS build v4.4.0 ***
   [00:00:00.001,000] <inf> usbh_hid_boot: HID Boot Device Class initialized
   [00:00:00.002,000] <inf> main: USB host HID sample started, connect a keyboard or a mouse
   [00:00:05.118,000] <inf> main: usbh_hid_boot_0: x 3
   [00:00:05.118,000] <inf> main: usbh_hid_boot_0: y -1
   [00:00:05.734,000] <inf> main: usbh_hid_boot_0: code 272 pressed
   [00:00:05.856,000] <inf> main: usbh_hid_boot_0: code 272 released
