#!/bin/bash
#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#
# Convenience wrapper: export all mpipe upstream PR branches one by one.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPORT_SCRIPT="${SCRIPT_DIR}/export_mpipe_upstream.sh"

TARGETS=(
    core
    utils
    base
    fs
    vid
    img
    disp
    aud
    sample-fs
    sample-cam_disp
    sample-jpeg_dec
    sample-tee_dec
    sample-dmic_i2s
)

for target in "${TARGETS[@]}"; do
    echo "========================================"
    echo "Exporting target: ${target}"
    echo "========================================"
    bash "${EXPORT_SCRIPT}" --no-check "${target}"
done

# Push to upstream
# git push --force nxp-upstream upstream/mpipe-core:libMP_RFC
# git push --force nxp-upstream upstream/mpipe-utils:mp-utils
# git push --force nxp-upstream upstream/mpipe-base:mp_zbase
# git push --force nxp-upstream upstream/mpipe-fs:mp-zfs
# git push --force nxp-upstream upstream/mpipe-disp:mp-zdisp
# git push --force nxp-upstream upstream/mpipe-vid:mp-zvid
# git push --force nxp-upstream upstream/mpipe-img:mp-zjpeg
# git push --force nxp-upstream upstream/mpipe-aud:libmp_zaud
# git push --force nxp-upstream upstream/mpipe-sample-fs:mp-sample-fs
# git push --force nxp-upstream upstream/mpipe-sample-cam_disp:libmp_video_sample
# git push --force nxp-upstream upstream/mpipe-sample-jpeg_dec:mp-sample-jpeg_dec
# git push --force nxp-upstream upstream/mpipe-sample-tee_dec:mp-sample-tee_dec
# git push --force nxp-upstream upstream/mpipe-sample-dmic_i2s:libmp_audio_sample

# Dependencies branches:
# git push --force nxp-upstream libmp_video:libmp_video
# git push --force nxp-upstream libmp_audio_dependencies_rebased:mp_aud_dependencies
