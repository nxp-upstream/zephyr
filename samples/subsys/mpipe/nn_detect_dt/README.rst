.. zephyr:code-sample:: mpipe-nn-detect-dt
   :name: Live detection pipeline in the devicetree
   :relevant-api: mpipe_dt

   Detect objects in the frames of a camera and draw their boxes, with a
   pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-nn-detect` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

Two descriptions exist. ``pipeline_plane.dtsi`` shows the camera on plane 0 of
the display and has the overlay draw the boxes into frames of its own on
plane 1, for a display that composes planes. ``pipeline_frame.dtsi`` has the
overlay draw the latest boxes into the video frame before it is displayed. A
board overlay includes the one its display allows.

On the MIMXRT1170-EVK the board overlay selects the eLCDIF, which has no
planes, and includes ``pipeline_frame.dtsi``. The :zephyr:code-sample:`mpipe-nn-detect`
sample shows the other way on the same board: the LCDIFv2 blends a plane of
boxes over the video.

The code that remains is what the devicetree names: the model and its working
memory, the function drawing the boxes and, when they are drawn in the video
frame, the function receiving the results. The application has no ``main()``.

Building and Running
********************

native_sim, fake backend
========================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/nn_detect_dt
   :board: native_sim/native/64
   :snippets: video-sw-generator
   :goals: build
   :compact:

MIMXRT1170-EVK, TensorFlow Lite Micro
=====================================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/nn_detect_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: nxp_btb44_ov5640 rk055hdmipi4ma0
   :gen-args: -DEXTRA_CONF_FILE=overlay-tflm.conf -DNN_MODEL=<path to .tflite>
   :goals: build flash
   :compact:

MIMXRT700-EVK, Neutron NPU
==========================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/nn_detect_dt
   :board: mimxrt700_evk/mimxrt798s/cm33_cpu0
   :shield: dvp_20pin_ov7670 rk055hdmipi4ma0
   :gen-args: -DEXTRA_CONF_FILE="overlay-tflm.conf;overlay-neutron.conf" -DNN_MODEL=<path to .tflite>
   :goals: build flash
   :compact:

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
