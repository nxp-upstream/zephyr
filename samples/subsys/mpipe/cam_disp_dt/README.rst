.. zephyr:code-sample:: mpipe-camera-display-dt
   :name: Camera to display pipeline in the devicetree
   :relevant-api: mpipe_dt

   Show the frames of a camera on a display, with a pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`camera-display-sample` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` is ``vid_src -> caps_filter -> disp_sink``. A
board overlay completes it with the frame format and the stages its hardware
offers: the PXP rotating the frames on the :zephyr:board:`mimxrt1170_evk`, the
JPEG decoder and a pixel format converter on the :zephyr:board:`mimxrt700_evk`.

The application has no code: the pipeline node has the ``autostart`` property.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/cam_disp_dt
   :board: native_sim/native/64
   :snippets: video-sw-generator
   :goals: build
   :compact:

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/cam_disp_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: nxp_btb44_ov5640 rk055hdmipi4ma0
   :goals: build flash
   :compact:

MIMXRT700-EVK
=============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/cam_disp_dt
   :board: mimxrt700_evk/mimxrt798s/cm33_cpu0
   :shield: dvp_20pin_ov7670 rk055hdmipi4ma0
   :goals: build flash
   :compact:

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
