.. zephyr:code-sample:: mpipe-pip-dt
   :name: Picture-in-picture pipelines in the devicetree
   :relevant-api: mpipe_dt

   Show a camera and an MJPEG file on two display planes, with two pipelines
   described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-pip` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

``pipeline.dtsi`` describes two pipelines, each with a player of its own: the
camera on the background plane of the display, and an MJPEG file decoded into
a translucent window on a second plane, whose position and opacity are
properties of its display sink node.

The application has no code: the pipeline node has the ``autostart`` property.
The test MJPEG file is generated at build time with the GStreamer CLI, which
needs ``gst-launch-1.0`` on the host, at the frame size the caps filter node
states, and written at boot to the path the file source node plays.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/pip_dt
   :board: native_sim/native/64
   :snippets: video-sw-generator
   :goals: build
   :compact:

Run, erasing the flash-backed FAT volume first::

  ./build/zephyr/zephyr.exe --flash_erase

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/pip_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: nxp_btb44_ov5640 rk055hdmipi4ma0
   :goals: build flash
   :compact:

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
