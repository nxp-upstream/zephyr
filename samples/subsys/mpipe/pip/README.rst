.. zephyr:code-sample:: mpipe-pip
   :name: Picture-in-picture on display planes
   :relevant-api: mpipe_disp mpipe_vid mpipe_img mpipe_fs mpipe_base

   Show a camera stream and a decoded MJPEG file on two planes of the
   display controller, which blends them without any copy.

Overview
********

This sample runs two pipelines of the Multimedia Pipeline subsystem that end on two
planes of the same display: the camera stream fills the panel on plane 0, and
a Motion-JPEG file decoded in software is shown in a translucent window on plane 1. No
element composes the two streams. Each pipeline hands its latest frame to its plane
through :c:func:`display_set_plane` and :c:func:`display_commit`, and the display
controller blends the planes at scan-out, so neither frame is copied after it was
produced.

Pipeline topology
=================

.. graphviz::

   digraph pip {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     vid_src      [label="vid_src\n(vid)"];
     caps_filter0 [label="caps_filter\n(base)"];
     disp_sink0   [label="disp_sink\nplane 0"];
     file_src     [label="file_src\n(fs)"];
     jpeg_parser  [label="jpeg_parser\n(img)"];
     caps_filter1 [label="caps_filter\n(base)"];
     jpeg_decoder [label="jpeg_decoder\n(img)"];
     disp_sink1   [label="disp_sink\nplane 1"];
     vid_src -> caps_filter0 -> disp_sink0;
     file_src -> jpeg_parser -> caps_filter1 -> jpeg_decoder -> disp_sink1;
   }

The second ``disp_sink`` is bound to plane 1 with the ``MPIPE_PROP_DISP_SINK_PLANE``
property and placed with ``MPIPE_PROP_DISP_SINK_X``, ``MPIPE_PROP_DISP_SINK_Y`` and
``MPIPE_PROP_DISP_SINK_ALPHA``. The window position and opacity are sample options
(``CONFIG_PIP_X``, ``CONFIG_PIP_Y``, ``CONFIG_PIP_ALPHA``).

Each pipeline has its own player. The camera runs until the shell command ``q`` quits
both players; the MJPEG file ends after ``CONFIG_MJPEG_NUM_FRAMES`` frames, its player
stops and the window leaves the screen, and ``r 1`` plays it again.

Requirements
************

* A display whose driver implements planes: the SDL display of ``native_sim``
  or the LCDIFv2 of the :zephyr:board:`mimxrt1170_evk`.
* A camera: the emulated one of the ``video-sw-generator`` snippet on ``native_sim``, or
  the :ref:`nxp_btb44_ov5640` shield on the EVK.
* A FAT volume for the MJPEG file, which the sample writes there at boot: the flash
  simulator on ``native_sim``, the SD card on the EVK.
* ``gst-launch-1.0`` on the build host, which generates the MJPEG file at build time.

Building and Running
********************

On ``native_sim``:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/pip
   :board: native_sim/native/64
   :snippets: video-sw-generator
   :goals: build run
   :compact:

On the i.MX RT1170 EVK with the OV5640 camera and the RK055HDMIPI4MA0 panel:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/pip
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: nxp_btb44_ov5640,rk055hdmipi4ma0
   :goals: build flash
   :compact:

The camera frame is shown as captured, 640x480 in the panel's portrait orientation, and
the 320x240 window overlaps its top-left corner at 63% opacity.

Sample Output
=============

.. code-block:: console

   [00:00:00.000,000] <inf> main: SDCard mounted at /SD:
   [00:00:00.000,000] <inf> main: Pipelines linked.
   [00:00:00.000,000] <inf> mpipe_player: Player #0 state: PLAYING
   [00:00:00.000,000] <inf> mpipe_player: Player #1 state: PLAYING
   [00:00:05.000,000] <inf> mpipe_player: Player #1: end of stream
