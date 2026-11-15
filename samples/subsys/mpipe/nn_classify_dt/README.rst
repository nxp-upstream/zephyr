.. zephyr:code-sample:: mpipe-nn-classify-dt
   :name: Live classification pipeline in the devicetree
   :relevant-api: mpipe_dt

   Classify the frames of a camera while showing them, with a pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-nn-classify` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` splits the camera stream with a tee: one
branch goes to the display, the other through
``nn_converter -> nn_runner -> nn_decoder -> app_sink``.

The code that remains is what the devicetree names: the ``model`` and
``arena`` properties of the runner node name the model and its working memory,
and the ``callback`` property of the application sink node names the function
receiving the results. The application has no ``main()``.

Building and Running
********************

native_sim, fake backend
========================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/nn_classify_dt
   :board: native_sim/native/64
   :snippets: video-sw-generator
   :goals: build
   :compact:

MIMXRT1170-EVK, TensorFlow Lite Micro
=====================================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/nn_classify_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: nxp_btb44_ov5640 rk055hdmipi4ma0
   :gen-args: -DEXTRA_CONF_FILE=overlay-tflm.conf
   :goals: build flash
   :compact:

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
