.. zephyr:code-sample:: mpipe-camera-display-multicore
   :name: Video source on one core, display on the other
   :relevant-api: mpipe_vid_src_clients mpipe_pipeline_server mpipe_erpc mpipe_disp mpipe_base

   Display on one core the frames a video source produces on the other.

Overview
********

This sample runs a Multimedia Pipeline on each core of a SoC. The secondary core owns the
video source; the primary core displays its frames. It is the two-core version of
:zephyr:code-sample:`camera-display-sample`.

The pipeline of the primary core is:

.. code-block:: none

   vid_src_client -> caps_filter -> disp_sink

- ``mpipe_vid_src_client`` is a source whose frames come from the other core. It offers the
  formats the remote source reports, gives one of its buffers at each call and gets it back
  filled.
- ``mpipe_caps_filter`` asks for RGB565 frames of 320x240 pixels, which fit in the memory
  the two cores share.
- ``mpipe_disp_sink`` renders the frames to the display.

The ``remote`` application, on the secondary core, builds an ordinary pipeline and serves it
with a pipeline server:

.. code-block:: none

   vid_src -> app_sink

- ``mpipe_vid_src`` captures from the video device of that core. No camera is wired to the
  secondary core of the supported board, so the device is the software video generator of
  Zephyr, which draws moving color bars.
- ``mpipe_app_sink`` is where the frames leave the pipeline: the server hands each one to
  the client of the other core.

The served pipeline has a source of its own, so it streams on its own thread. The client
asks for the next frame; when none is ready within a short time the server says so and the
client asks again. This is what lets the primary core stop, pause and replay at any time.

The player shell of the primary core drives both pipelines: ``p`` toggles play and pause,
``s`` stops, ``r`` replays and ``q`` quits.

Limits
======

- The server copies each frame once, from the buffer of its video device into the buffer
  of the client.
- A stop takes effect within the time the server waits for a frame,
  :kconfig:option:`CONFIG_MPIPE_ERPC_SERVER_PRODUCE_TIMEOUT_MS`.

Requirements
************

The sample supports :zephyr:board:`mimxrt1170_evk` with the ``rk055hdmipi4ma0`` display
shield. The M7 is the primary core, the M4 the secondary one.

The build host needs ``erpcgen``, the eRPC interface compiler, on the ``PATH``. It is built
from the ``erpcgen`` directory of the ``erpc`` module (``modules/lib/erpc``).

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/cam_disp_multicore
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Dcam_disp_multicore_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

Sysbuild builds the remote application for the M4 and the primary application loads it at
boot. The color bars appear at the top left of the display.
