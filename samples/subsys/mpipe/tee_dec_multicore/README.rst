.. zephyr:code-sample:: mpipe-tee-decode-multicore
   :name: Pipelines on two cores
   :relevant-api: mpipe_pipeline_server mpipe_vid_pipeline_servers mpipe_vid_transform_clients mpipe_vid_sink_clients mpipe_erpc mpipe_base

   Play Motion-JPEG with a pipeline on each core: decode and check on the second core.

Overview
********

This sample runs Multimedia Pipelines on the two cores of a SoC, working as one. The
primary core plays an MJPEG stream: it splits it into JPEG frames, completes the decoded
frames, displays them and records the compressed stream on the SD card. The secondary
core does two jobs for it, at two places of the pipeline, each with a pipeline of its own:

- it decodes every frame and draws on it, two elements in a row that the primary core
  sees as one;
- it checks the markers of every compressed frame and counts the damaged ones.

Each of the two pipelines of the secondary core is reached through a client element of
the primary core's pipeline, a transform for the first and a sink for the second, over
eRPC and the Zephyr IPC service. A frame crosses between the cores once for each job,
however many elements work on it there, and the primary core is idle meanwhile.

Compare with :zephyr:code-sample:`mpipe-jpeg-decode-multicore`, where the secondary core runs no
pipeline but a hand-written server around a decoder: what a core that runs neither Zephyr
nor this framework would do.

Pipeline topology
=================

.. graphviz::

   digraph pipelines {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];

     subgraph cluster_primary {
       label="primary core";
       app_src     [label="app_src\n(base)"];
       jpeg_parser [label="jpeg_parser\n(img)"];
       caps_filter [label="caps_filter\n(base)"];
       tee         [label="tee\n(base)"];
       dec_queue   [label="queue\n(base)"];
       dec_client  [label="vid_transform_client\n(vid, eRPC)", fillcolor="#c8d8f0"];
       overlay     [label="vid_overlay\n(vid)"];
       display     [label="display\n(disp)"];
       check_queue  [label="queue\n(base)"];
       check_client [label="vid_sink_client\n(vid, eRPC)", fillcolor="#c8d8f0"];
       rec_queue    [label="queue\n(base)"];
       file_sink    [label="file_sink\n(fs)"];
       app_src -> jpeg_parser -> caps_filter -> tee;
       tee -> dec_queue -> dec_client -> overlay -> display;
       tee -> check_queue -> check_client;
       tee -> rec_queue -> file_sink;
     }

     subgraph cluster_decoding {
       label="secondary core: decoding pipeline";
       dec_src   [label="app_src\n(base)"];
       jpeg_dec  [label="jpeg_decoder\n(img)"];
       dec_draw  [label="vid_overlay\n(vid)"];
       dec_sink  [label="app_sink\n(base)"];
       dec_src -> jpeg_dec -> dec_draw -> dec_sink;
     }

     subgraph cluster_checking {
       label="secondary core: checking pipeline";
       check_src  [label="app_src\n(base)"];
       check_caps [label="caps_filter\n(base)"];
       check_sink [label="app_sink\n(base)"];
       check_src -> check_caps -> check_sink;
     }

     dec_client -> dec_src [style=dashed, constraint=false, label="pipeline server"];
     check_client -> check_src [style=dashed, constraint=false, label="pipeline server"];
   }

Primary core
------------

- ``mpipe_app_src`` takes the MJPEG stream from the application, which holds it in its
  image, and pushes it again at every replay.
- ``mpipe_img_jpeg_parser`` splits the stream into JPEG frames and ``mpipe_caps_filter``
  states their size (``width`` and ``height`` properties of its node).
- ``mpipe_tee`` gives every frame to the three branches, and each ``mpipe_queue`` runs
  its branch on a thread of its own, so that none waits for another.
- ``mpipe_vid_transform_client`` stands for the decoding pipeline of the other core: it
  hands each JPEG frame over by address and waits for the decoded one. The formats it
  negotiates with its neighbors are those of that pipeline.
- ``mpipe_vid_overlay`` draws a frame counter, the work of this core on the frames, and
  ``mpipe_disp_sink`` shows them.
- ``mpipe_vid_sink_client`` stands for the checking pipeline of the other core: the
  stream ends there as far as this pipeline is concerned.
- ``mpipe_file_sink`` writes the frames to the file named by the ``path`` property of its
  node.

Secondary core
--------------

The ``remote`` application builds two ordinary pipelines and serves each to its client
with a pipeline server:

- decoding: ``mpipe_img_jpeg_decoder`` decodes the frame and ``mpipe_vid_overlay`` draws a
  bar that grows with the frames, between an ``mpipe_app_src`` and an ``mpipe_app_sink``.
  The decoder writes into the memory of the primary core: the frame is never copied.
- checking: an ``mpipe_app_sink`` hands every frame to a function of the application,
  which looks for the markers that open and close a JPEG frame, behind an
  ``mpipe_caps_filter`` that tells the client what is checked.

Each server has its IPC endpoint and its thread, which is the streaming thread of the
pipeline it serves, so one job never delays the other. The channel of each client element
names the endpoint of its server. A core with less to spare can serve both pipelines on one
endpoint and from one thread: the channels then name the same endpoint and each opens its
function there, and their calls are answered one at a time. The two clients speak the same
two interfaces, defined in
:zephyr_file:`subsys/mpipe/erpc/mpipe_stream.erpc` and
:zephyr_file:`subsys/mpipe/erpc/vid/mpipe_vid_format.erpc`. The eRPC objects of both cores
are static: eRPC takes nothing from the heap.

The pipeline is described in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node
per element, and the ``sources`` property of an element names the element it receives its buffers
from. The pipeline is built at boot from that description, and the options of the framework and of
its elements are enabled by the nodes.

Each image has a ``pipeline.dtsi``. The primary core pushes an MJPEG stream
into a pipeline with two client elements; the remote core describes the
decoding and the checking pipelines those clients are served by. The
pipelines of the remote core have no ``autostart`` property: the servers
drive them.

The code that remains is the behavior of the application: on the primary core
the loop pushing the stream into the application source and the function the
overlay node names to draw on the frames; on the remote core the functions its
overlay and application sink nodes name and the two servers, which get their pipeline and its
application source and sink with ``MPIPE_DT_PIPELINE_GET()`` and
``MPIPE_DT_ELEMENT_GET()``.

Requirements
************

The sample supports :zephyr:board:`mimxrt1170_evk` with the ``rk055hdmipi4ma0`` display
shield and a FAT-formatted, writable SD card. The M7 is the primary core, the M4 the
secondary one.

The build host needs:

- GStreamer (the ``gst-launch-1.0`` tool and the ``good`` plugins which include
  ``videotestsrc`` and ``jpegenc``) to generate the test stream. On Debian/Ubuntu::

    sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good

- ``erpcgen``, the eRPC interface compiler, on the ``PATH``. It is built from the
  ``erpcgen`` directory of the ``erpc`` module (``modules/lib/erpc``).

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/tee_dec_multicore
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Dtee_dec_multicore_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

Sysbuild builds the remote application for the M4 and the primary application loads it at
boot. The M4 console is on ``LPUART2``:

.. code-block:: console

   I: Remote started
   I: Serving mpipe_vid_transform
   I: Serving mpipe_vid_sink
   I: Checked 30 frames, 0 damaged

The display shows the test stream with the counter of the M7 at the top and the bar of
the M4 at the bottom, and the M7 console shows the pipeline reaching end of stream:

.. code-block:: console

   [00:00:00.000,000] <inf> main: SDCard mounted at /SD:
   [00:00:00.000,000] <inf> main: Pipeline linked.
   [00:00:xx.xxx,xxx] <inf> mpipe_player: Player #0: end of stream

The recording is then on the card. The player's shell commands on the M7 console drive
both cores: ``player replay`` plays and records the stream again, and ``player pause``
pauses the three pipelines.
