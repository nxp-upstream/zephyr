.. _bluetooth_classic_bip_client_b2b_tests:

Bluetooth Classic BIP Client b2b Tests
#######################################

Overview
********

This test suite uses ``bumble`` for testing Bluetooth Classic BIP (Basic Imaging Profile)
communication between a host PC (running :ref:`Twister <twister_script>`) and a device
under test (DUT) running Zephyr as a BIP client, paired with a second board (the harness
device) acting as the BIP server.

Prerequisites
*************

The test suite has the following prerequisites:

* The ``bumble`` library installed on the host PC.
The Bluetooth Classic controller on PC side is required. Refer to getting started of `bumble`_
for details.

The HCI transport for ``bumble`` can be configured as follows:

* A specific configuration context can be provided along with the ``usb_hci`` fixture separated by
  a ``:`` (i.e. specify fixture ``usb_hci:usb:0`` to use the ``usb:0`` as hci transport for
  ``bumble``).
* The configuration context can be overridden using the `hci transport`_ can be provided using the
  ``--hci-transport`` test suite argument (i.e. run ``twister`` with the
  ``--pytest-args=--hci-transport=usb:0`` argument to use the ``usb:0`` as hci transport for
  ``bumble``).

Building and Running
********************

Running on Hardware
===================

Running the test suite on hardware requires a HCI transport connected to the host PC, plus a
second :zephyr:board:`mimxrt1170_evk@B/mimxrt1176/cm7` board flashed as the harness device
(BIP server).

The test suite can be launched using Twister:

.. code-block:: shell

   west twister -v -p mimxrt1170_evk@B/mimxrt1176/cm7 --device-testing --device-serial COM4 -T tests/bluetooth/classic/b2b/bip_client_test -O bip_client_test --force-platform --west-flash --west-runner=jlink -X usb_hci:usb:0

.. _bumble:
   https://google.github.io/bumble/getting_started.html

.. _hci transport:
   https://google.github.io/bumble/transports/index.html
