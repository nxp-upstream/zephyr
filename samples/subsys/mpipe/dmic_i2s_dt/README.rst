.. zephyr:code-sample:: mpipe-dmic-i2s-dt
   :name: DMIC to I2S pipeline in the devicetree
   :relevant-api: mpipe_dt

   Play a digital microphone on a codec, with a pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`dmic-i2s` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` is
``dmic_src -> caps_filter -> gain -> i2s_codec_sink``. The memory slab the
audio blocks come from is defined by the microphone source node and shared
with the sink of the same pipeline.

The application has no code: the pipeline node has the ``autostart`` property.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/dmic_i2s_dt
   :board: native_sim/native/64
   :goals: build
   :compact:

The emulated microphone reads ``dmic_in.pcm`` from the working directory and
the emulated I2S controller writes to ``build/native_sim_audio_pipeline_tx.pcm``.

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/dmic_i2s_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :goals: build flash
   :compact:
