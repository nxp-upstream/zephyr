.. zephyr:code-sample:: spdif
   :name: SPDIF Controller
   :relevant-api: audio_spdif_interface

   Configure and exercise the Zephyr SPDIF controller API.

Overview
********

This sample demonstrates the basic Zephyr SPDIF controller flow:

- configure the controller for packed 24-bit stereo frames
- program transmitter channel-status words
- queue a few transmit buffers
- start and stop the transfer path
- read back sticky controller status

The sample targets the RT1170 SPDIF controller introduced by the MCUX-backed
driver and uses a board overlay to enable the controller instance.

Requirements
************

The sample expects a devicetree node label named ``spdif``.

On :zephyr:board:`mimxrt1170_evk`, the underlying MCUX SDK SPDIF example also
documents board-level jumper and resistor changes before external SPDIF routing
is usable. This sample still builds and exercises the API without requiring
those changes, but meaningful signal verification on the EVK depends on that
hardware setup.

Building and Running
********************

The code can be found in :zephyr_file:`samples/drivers/audio/spdif`.

To build the application:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/audio/spdif
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :goals: build
   :compact: