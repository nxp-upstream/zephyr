.. zephyr:code-sample:: mpipe-dual-pipeline
   :name: Dual multimedia pipeline

   Build and run an audio pipeline and a video pipeline side by side in a
   single application.

Description
***********

This sample combines the :zephyr:code-sample:`dmic-i2s` audio sample and the
camera-to-display video sample into a single application that builds and runs
two independent multimedia pipelines on top of the Multimedia Pipeline
(``mpipe``) framework:

.. graphviz::

   digraph pipelines {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];

     subgraph cluster_audio {
       label="Audio pipeline";
       dmic  [label="DMIC\nSource"];
       acaps [label="Caps\nFilter"];
       gain  [label="Gain\nTransform"];
       i2s   [label="I2S Codec\nSink"];
       dmic -> acaps -> gain -> i2s;
     }

     subgraph cluster_video {
       label="Video pipeline";
       vsrc  [label="Video\nSource"];
       vcaps [label="Caps\nFilter"];
       vtr   [label="JPEG/\nTransform"];
       disp  [label="Display\nSink"];
       vsrc -> vcaps -> vtr -> disp;
     }
   }

Both pipelines are built independently, and each is driven by its own
``mpipe_player`` instance. The player owns the pipeline's run-time lifecycle
(play, pause, stop, replay, quit) and auto-stops the pipeline on end-of-stream
or error. The ``main()`` function builds and starts every enabled pipeline,
then waits until each player has quit (either from the shell or after an
end-of-stream / error) before tearing them down.

Interactive Control
===================

The mpipe player registers shell commands. Because more than one player is
registered, a single command applies to every player at once, so one keystroke
drives both pipelines together:

- ``p`` - play/pause toggle
- ``s`` - stop
- ``r`` - replay from the beginning
- ``q`` - quit

The grouped ``player`` command (``player play|pause|stop|replay|quit|status``)
is also available for discoverability and tab-completion. ``player status``
lists each registered player and its current state.

Requirements
************

For the audio pipeline:

* A board with digital microphone (DMIC) support
* A board with I2S and audio codec support

For the video pipeline:

* A board with a camera (video source) and a display

Both pipelines also require DMA support and sufficient RAM for audio and video
buffering.

This sample has been tested on :zephyr:board:`mimxrt1170_evk`.

Building and Running
********************

This sample can be found under
:zephyr_file:`samples/subsys/mpipe/dual_pipeline`.

For :zephyr:board:`mimxrt1170_evk`, build this sample application with the
following commands:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/dual_pipeline
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :goals: build flash
   :compact:

To build only one of the pipelines, disable the other one, for example to build
only the audio pipeline:

.. code-block:: console

   west build -b mimxrt1170_evk@B/mimxrt1176/cm7 \
       samples/subsys/mpipe/dual_pipeline -- -DCONFIG_APP_VIDEO_PIPELINE=n

Sample Output
*************

The application reports how many pipelines it starts and then processes audio
and video in real time until each pipeline reaches end-of-stream or reports an
error:

.. code-block:: console

   *** Booting Zephyr OS build ***
   [00:00:01.000,000] <inf> main: Starting 2 pipeline(s)

Configuration Options
*********************

The sample supports the following configuration options:

* ``CONFIG_APP_AUDIO_PIPELINE`` - build and run the audio pipeline (default y).
* ``CONFIG_APP_VIDEO_PIPELINE`` - build and run the video pipeline (default y).
* ``CONFIG_USE_I2S_TARGET_CODEC_CONTROLLER`` - use the codec as the I2S clock
  controller.
* ``CONFIG_VIDEO_FRAME_WIDTH`` / ``CONFIG_VIDEO_FRAME_HEIGHT`` /
  ``CONFIG_VIDEO_FRAME_RATE`` / ``CONFIG_VIDEO_PIXEL_FORMAT`` - video format.
* ``CONFIG_VIDEO_SOURCE_CROP_*`` - video source crop area.
* ``CONFIG_VIDEO_CTRL_HFLIP`` / ``CONFIG_VIDEO_CTRL_VFLIP`` /
  ``CONFIG_VIDEO_ROTATION_ANGLE`` - video transform controls.
* ``CONFIG_PROP_NUM_BUFS`` - number of buffers the video source provides before
  stopping (0 means run forever).

Devicetree Configuration
************************

The audio pipeline requires:

* ``dmic_dev`` node label for the DMIC.
* ``i2s_codec_tx`` node alias for the I2S output.
* ``audio_codec`` node label for the audio codec.

The video pipeline requires a video source device and a display device, and
optionally a ``zephyr,jpegdec`` or ``zephyr,videotrans`` chosen node for JPEG
decoding / video transformation.
