.. zephyr:code-sample:: mpipe-tee-decode-multicore-dt
   :name: Pipelines on two cores in the devicetree
   :relevant-api: mpipe_dt

   Run a pipeline whose decoding and checking are pipelines of the other core,
   all described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-tee-decode-multicore` sample with
its pipeline described in the devicetree: a ``zephyr,mpipe-pipeline`` node holds
one child node per element, and the ``sources`` property of an element names the
element it receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled by
the nodes.

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

Building and Running
********************

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/tee_dec_multicore_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Dtee_dec_multicore_dt_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

The build needs the ``erpc`` module and the ``erpcgen`` host tool.

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
