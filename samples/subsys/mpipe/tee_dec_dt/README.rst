.. zephyr:code-sample:: mpipe-tee-decode-dt
   :name: MJPEG tee pipeline in the devicetree
   :relevant-api: mpipe_dt

   Split one MJPEG stream into a display branch and a file branch, with a
   pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-tee-decode` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` reads an MJPEG file and splits it with a tee:
one branch decodes the frames to the display, the other writes them back to a
file. The two nodes naming the tee as their source take its pads in node order,
and each branch starts with a queue, which gives it a thread of its own.

The application has no code: the pipeline node has the ``autostart`` property.
The test MJPEG file is generated at build time with the GStreamer CLI, which
needs ``gst-launch-1.0`` on the host, at the frame size the caps filter node
states, and written at boot to the path the file source node plays.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/tee_dec_dt
   :board: native_sim/native/64
   :goals: build
   :compact:

Run, erasing the flash-backed FAT volume first::

  ./build/zephyr/zephyr.exe --flash_erase

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/tee_dec_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :goals: build flash
   :compact:

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
