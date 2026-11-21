.. zephyr:code-sample:: mpipe-h264-enc-dec
   :name: H.264 encode and decode pipeline
   :relevant-api: mpipe_vid mpipe_disp mpipe_base

   Encode the frames of a camera to H.264, decode them again and display
   them, in software.

Overview
********

This sample captures frames from a camera, encodes them to H.264 with the software encoder
element, decodes the stream with the software decoder element and shows the result, using the
Multimedia Pipeline subsystem. What the display shows is what a receiver of the stream would see,
so the sample is a way to judge the picture the encoder gives at a quantizer and a way to measure
what the two codecs cost on a board.

Pipeline topology
=================

.. graphviz::

   digraph pipeline {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     vid_src      [label="vid_src\n(vid)"];
     caps_filter  [label="caps_filter\n(base)"];
     to_yuv       [label="vid_convert\n(vid)"];
     encoder      [label="vid_h264_encoder\n(vid)"];
     decoder      [label="vid_h264_decoder\n(vid)"];
     to_rgb       [label="vid_convert\n(vid)"];
     display      [label="disp_sink\n(disp)"];
     vid_src -> caps_filter -> to_yuv -> encoder -> decoder -> to_rgb -> display;
   }

The caps filter sets the frame size, since the memory of the encoder is reserved for one. The
first converter turns the camera frames into the planar YUV 4:2:0 the encoder takes, the second
one turns the decoded frames into the RGB565 of the display.

With :kconfig:option:`CONFIG_H264_TEST_PATTERN` a ``vid_overlay`` element sits before the first
converter and replaces the content of the camera frames by a pattern that moves, which gives the
codecs the same work whatever the camera sees.

Requirements
************

A camera that provides BGRX32 frames and a display that takes RGB565 ones, or
:ref:`native_sim <native_sim>` with the :ref:`snippet-video-sw-generator`. The encoder comes from
the ``minih264`` module and the decoder from the ``h264swdec`` module.

Building and running
********************

On :ref:`native_sim <native_sim>`, with the emulated camera:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/h264_enc_dec
   :board: native_sim/native/64
   :snippets: video-sw-generator
   :goals: build run
   :compact:

On the MIMXRT1170-EVKB with the OV5640 camera and the RK055HDMIPI4MA0 panel:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/h264_enc_dec
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: nxp_btb44_ov5640,rk055hdmipi4ma0
   :goals: build flash
   :compact:

On this board the codecs run from the instruction TCM and work in the on-chip RAM
(:kconfig:option:`CONFIG_CODEC_CODE_RELOCATE`, :kconfig:option:`CONFIG_CODEC_DATA_RELOCATE`):
the flash is a serial one and the default RAM is the SDRAM, and both are slow for code that
touches every pixel.

Add ``-DCONFIG_MPIPE_STAGE_STATS=y`` to log, once per second, the frame rate and the time each
element takes per frame.

Sample output
=============

.. code-block:: console

   [00:00:00.302,000] <inf> main: Pipeline linked.
   [00:00:00.323,000] <inf> mpipe_player: Player #0 state: PLAYING

The player shell stops, replays and quits the pipeline (``player stop``, ``player replay``,
``player quit``).
