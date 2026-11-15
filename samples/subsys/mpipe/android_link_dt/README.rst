.. zephyr:code-sample:: mpipe-android-link-dt
   :name: Android link pipelines in the devicetree
   :relevant-api: mpipe_dt

   Mirror an Android phone, with its three pipelines described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe_android_link` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

``pipeline.dtsi`` describes the video pipeline, the audio playback pipeline and
the microphone pipeline, none with the ``autostart`` property: the application
plays them when a phone connects.

The code that remains is the behavior of the application, shared with the
android_link sample: the network, the connections of the phone, the control
messages, the touch controller and the messages the overlay draws. It reaches
the players and the socket elements of the devicetree with
``MPIPE_DT_PLAYER_GET()`` and ``MPIPE_DT_ELEMENT_GET()`` to hand them the
connected sockets.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link_dt
   :board: native_sim/native/64
   :gen-args: -DEXTRA_CONF_FILE=native_sim.conf
   :goals: build
   :compact:

MIMXRT1170-EVK
==============

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link_dt
   :board: mimxrt1170_evk@B/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :gen-args: -DEXTRA_CONF_FILE=overlay-wifi.conf
   :goals: build flash
   :compact:

See the android_link sample for the phone side.
