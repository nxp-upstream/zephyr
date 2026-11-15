.. zephyr:code-sample:: mpipe-jpeg-decode-multicore-dt
   :name: Multi-core MJPEG decode pipeline in the devicetree
   :relevant-api: mpipe_dt

   Decode an MJPEG file on the other core, with a pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-jpeg-decode-multicore` sample with its
pipeline described in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` is
``file_src -> jpeg_parser -> caps_filter -> queue -> client -> disp_sink``, where
the client element hands the frames to the decoder running on the other core
over eRPC. The image of the other core is the one of the jpeg_dec_multicore sample.

The application has no code: the pipeline node has the ``autostart`` property.
The test MJPEG file is generated at build time with the GStreamer CLI, which
needs ``gst-launch-1.0`` on the host, at the frame size the caps filter node
states, and written at boot to the path the file source node plays.

Building and Running
********************

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/jpeg_dec_multicore_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Djpeg_dec_multicore_dt_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

The build needs the ``erpc`` module and the ``erpcgen`` host tool.

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
