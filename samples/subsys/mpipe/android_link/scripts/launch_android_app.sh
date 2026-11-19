#!/usr/bin/env bash
#
# Copyright 2024-2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#
# Push the demo's phone application to the phone and launch it against the
# board. Needs adb (ADB, default: the one in PATH or in ANDROID_HOME) with the
# phone authorized, and the prebuilt application (SERVER_JAR, default: the
# sample's android_app/scrcpy-server).
#
#   ./launch_android_app.sh                                  # every default below
#   SERVER_JAR=~/scrcpy-server ./launch_android_app.sh       # a jar elsewhere
#   REMOTE_HOST=192.0.2.1 AUDIO=true MIC=true ./launch_android_app.sh
#   WIFI_SSID= ./launch_android_app.sh                        # Ethernet: join no network
#   KEEP_SAVED_WIFI=true ./launch_android_app.sh              # keep saved board entries
#
# Each value must match the board's build: see the sample's README.
#
# Wi-Fi: the server joins the board's network by itself (WIFI_SSID/WIFI_PSK)
# with a local-only network request, so the phone keeps mobile data as its
# default route and no settings change. A saved entry for the same SSID would
# make Android treat it as a regular network and drop the internet, so the
# script forgets such entries before launching.
#
set -e

SCRCPY_VERSION=4.1
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -n "${ADB:-}" ]; then
    :
elif command -v adb >/dev/null 2>&1; then
    ADB=adb
else
    ADB="${ANDROID_HOME:-$HOME/Android/Sdk}/platform-tools/adb"
fi
SERVER_JAR="${SERVER_JAR:-$SCRIPT_DIR/../android_app/scrcpy-server}"
# The board's address and port (CONFIG_NET_CONFIG_MY_IPV4_ADDR or the soft-AP
# address, CONFIG_ANDROID_LINK_PORT)
REMOTE_HOST="${REMOTE_HOST:-192.0.2.1}"
REMOTE_PORT="${REMOTE_PORT:-5000}"
# The frames' size: the panel's (CONFIG_ANDROID_LINK_FRAME_WIDTH/HEIGHT)
WIDTH="${WIDTH:-720}"
HEIGHT="${HEIGHT:-1280}"
# The audio connection (CONFIG_ANDROID_LINK_AUDIO) and the microphone one
# (CONFIG_ANDROID_LINK_MIC)
AUDIO="${AUDIO:-true}"
MIC="${MIC:-true}"
# playback = the phone's playback (mirror); mic-voice-communication = its
# microphone with echo cancelling (intercom). The board's modes switch it at
# run time, this is only the source to start with.
AUDIO_SOURCE="${AUDIO_SOURCE:-playback}"
# The board's network (CONFIG_ANDROID_LINK_WIFI_SSID/PSK); empty on Ethernet
WIFI_SSID="${WIFI_SSID-zephyr-link}"
WIFI_PSK="${WIFI_PSK-zephyrdemo}"
MAX_FPS="${MAX_FPS:-15}"
# JPEG quality of the frames: a software decoder takes about 45 ms plus 0.3 ms
# per KB for a 720x1280 frame on a Cortex-M7, so a lighter frame is a faster
# one; 70 keeps text readable at a quarter less data than 80
JPEG_QUALITY="${JPEG_QUALITY:-70}"

if [ ! -f "$SERVER_JAR" ]; then
    echo "Server jar not found: $SERVER_JAR (set SERVER_JAR)" >&2
    exit 1
fi

echo "===> Stopping a running server, if any"
for pid in $("$ADB" shell "ps -A | grep app_process" | awk '{print $2}'); do
    "$ADB" shell "kill -9 $pid" || true
done
# The old server's cleanup process deletes the jar when it notices the server
# is gone; wait for it, or it deletes the one pushed below
for i in 1 2 3 4 5 6 7 8 9 10; do
    [ -z "$("$ADB" shell "ps -A | grep app_process")" ] && break
    sleep 0.5
done

echo "===> Pushing the server"
"$ADB" push "$SERVER_JAR" /data/local/tmp/scrcpy-server.jar

# A saved entry for the board's SSID would make Android join it as an ordinary
# network and take the internet away: forget any such entry
if [ "${KEEP_SAVED_WIFI:-false}" != "true" ] && [ -n "$WIFI_SSID" ]; then
    for id in $("$ADB" shell cmd wifi list-networks 2>/dev/null \
                | awk -v s="$WIFI_SSID" '$2 == s {print $1}' | sort -u); do
        "$ADB" shell cmd wifi forget-network "$id" >/dev/null 2>&1
    done
fi

echo "===> Launching the server against $REMOTE_HOST:$REMOTE_PORT" \
     "(${WIDTH}x${HEIGHT} audio=$AUDIO source=$AUDIO_SOURCE mic=$MIC)"
# Fixed-size MJPEG frames, the capture locked to the natural orientation, raw
# streams with no headers, raw PCM audio, and the board's microphone coming
# back; the server exits and restores the phone's settings when the board
# asks it to (cleanup=true).
"$ADB" shell "CLASSPATH=/data/local/tmp/scrcpy-server.jar nohup app_process / \
    com.genymobile.scrcpy.Server $SCRCPY_VERSION \
    remote_host=$REMOTE_HOST remote_port=$REMOTE_PORT \
    video=true video_source=display video_codec=mjpeg \
    video_codec_options=width=$WIDTH,height=$HEIGHT,quality=$JPEG_QUALITY \
    capture_orientation=@0 max_fps=$MAX_FPS \
    audio=$AUDIO audio_source=$AUDIO_SOURCE audio_codec=raw mic=$MIC \
    wifi_ssid=$WIFI_SSID wifi_psk=$WIFI_PSK \
    control=true show_touches=true cleanup=true raw_stream=true >/dev/null 2>&1 &"

"$ADB" shell "ps -A | grep app_process" || true
