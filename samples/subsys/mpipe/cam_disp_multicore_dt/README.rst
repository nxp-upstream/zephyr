.. zephyr:code-sample:: mpipe-camera-display-multicore-dt
   :name: Video source on one core, display on the other, in the devicetree
   :relevant-api: mpipe_dt

   Display the frames of a video source of the other core, with a pipeline described in the
   devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-camera-display-multicore` sample with the
pipeline of the primary core described in the devicetree: a ``zephyr,mpipe-pipeline`` node
holds one child node per element, and the ``sources`` property of an element names the
element it receives its buffers from. The pipeline is built at boot from that description,
and the options of the framework and of its elements are enabled by the nodes.

The pipeline of ``pipeline.dtsi`` is ``vid_src_client -> caps_filter -> disp_sink``. The
source client names its channel, a ``zephyr,mpipe-erpc-channel`` node that holds the IPC
endpoint of the pipeline the other core serves. The image of the other core is the one of
the cam_disp_multicore sample.

The application has no code: the pipeline node has the ``autostart`` property.

Building and Running
********************

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/cam_disp_multicore_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Dcam_disp_multicore_dt_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

The build needs the ``erpc`` module and the ``erpcgen`` host tool.

The player shell drives the pipeline while it runs: ``p`` toggles play and
pause, ``s`` stops, ``r`` replays and ``q`` quits.
