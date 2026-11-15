.. zephyr:code-sample:: mpipe-jpeg-decode-dt
   :name: MJPEG decode pipeline in the devicetree
   :relevant-api: mpipe_dt

   Decode an MJPEG file to a display, with a pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-jpeg-decode` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` is
``file_src -> jpeg_parser -> caps_filter -> jpeg_decoder -> disp_sink``, with the
software decoder. A board overlay changes it for its hardware: the
:zephyr:board:`mimxrt1170_evk` adds the PXP rotating the frames, the
:zephyr:board:`mimxrt700_evk` replaces the software decoder by the JPEG decoder
of the SoC followed by a pixel format converter.

The application has no code: the pipeline node has the ``autostart`` property.
The test MJPEG file is generated at build time with the GStreamer CLI, which
needs ``gst-launch-1.0`` on the host, at the frame size the caps filter node
states, and written at boot to the path the file source node plays.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/jpeg_dec_dt
   :board: native_sim/native/64
   :goals: build
   :compact:

Run, erasing the flash-backed FAT volume first::

  ./build/zephyr/zephyr.exe --flash_erase

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/jpeg_dec_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :goals: build flash
   :compact:

MIMXRT700-EVK
=============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/jpeg_dec_dt
   :board: mimxrt700_evk/mimxrt798s/cm33_cpu0
   :shield: rk055hdmipi4ma0
   :goals: build flash
   :compact:

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
