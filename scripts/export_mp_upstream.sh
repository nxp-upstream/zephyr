#!/bin/bash
#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#
# Export Media Pipe (MP) subsystem from libmp_dev to upstream PR branches.
#
# This script generates clean, single-commit branches for each upstream PR
# by extracting the final state of relevant files from libmp_dev using git diff.
# All fixup commits are implicitly squashed since only the final diff is used.
#
# Each generated branch starts from BASE_REF (mmiot/main) and includes:
#   1. Cherry-picked dependency commits (from previously generated branches)
#   2. The target's own commit (new files from libmp_dev)
#
# Compliance checks only verify the target's own commit (HEAD~1..HEAD),
# not the cherry-picked dependencies (which are checked when their own
# branch is generated).
#
# Usage:
#   ./scripts/export_mp_upstream.sh              # Export all PRs
#   ./scripts/export_mp_upstream.sh core          # Export core only
#   ./scripts/export_mp_upstream.sh zvid          # Export zvid only (core must exist)
#   ./scripts/export_mp_upstream.sh --dry-run     # Show what would be done
#   ./scripts/export_mp_upstream.sh --list        # List available targets
#
# Requirements:
#   - Must be run from the zephyr repository root
#   - Must be on the libmp_dev branch (or specify --source-branch)
#   - mmiot/main remote must be available

set -euo pipefail

# ===========================================================================
# Configuration
# ===========================================================================

# The branch containing all MP development (core + all plugins)
SOURCE_BRANCH="libmp_dev"

# The base commit where libmp_dev diverged from mmiot/main
BASE_REF="mmiot/main"

# Branch name prefix for generated upstream branches
UPSTREAM_PREFIX="upstream/mp"

# Signed-off-by (from git config)
SOB_NAME="$(git config user.name)"
SOB_EMAIL="$(git config user.email)"
SOB="Signed-off-by: ${SOB_NAME} <${SOB_EMAIL}>"

# Date for display
TODAY="$(date +%Y-%m-%d)"

# ===========================================================================
# File mappings per PR target
# ===========================================================================

# Core: framework files + integration into subsys/Kconfig and subsys/CMakeLists.txt
CORE_PATHS=(
    "subsys/mp/Kconfig"
    "subsys/mp/CMakeLists.txt"
    "subsys/mp/core/"
    "subsys/mp/plugins/Kconfig"
    "subsys/mp/plugins/CMakeLists.txt"
    "include/zephyr/mp/core/"
    "subsys/Kconfig"
    "subsys/CMakeLists.txt"
    "lib/Kconfig"
    "lib/CMakeLists.txt"
)

# zvid plugin
ZVID_PATHS=(
    "subsys/mp/plugins/zvid/"
    "include/zephyr/mp/zvid/"
)

# zjpeg plugin (JPEG support)
ZJPEG_PATHS=(
    "subsys/mp/plugins/zjpeg/"
    "include/zephyr/mp/zjpeg/"
)

# zaud plugin
ZAUD_PATHS=(
    "subsys/mp/plugins/zaud/"
    "include/zephyr/mp/zaud/"
)

# zdisp plugin
ZDISP_PATHS=(
    "subsys/mp/plugins/zdisp/"
    "include/zephyr/mp/zdisp/"
)

# zfs plugin
ZFS_PATHS=(
    "subsys/mp/plugins/zfs/"
    "include/zephyr/mp/zfs/"
)

# Sample: cam_disp (camera to display pipeline)
SAMPLE_CAM_DISP_PATHS=(
    "samples/subsys/mp/cam_disp/"
)

# Sample: jpeg_dec (JPEG decoding pipeline)
SAMPLE_JPEG_DEC_PATHS=(
    "samples/subsys/mp/jpeg_dec/"
)

# Sample: fs (filesystem read/write pipeline)
SAMPLE_FS_PATHS=(
    "samples/subsys/mp/fs/"
)

# ===========================================================================
# Commit messages (following Zephyr convention: area: Short description)
# ===========================================================================

CORE_COMMIT_MSG="mp: Introduce Media Pipe (MP) subsystem

MP (Media Pipe) is a lightweight GStreamer-like multimedia framework
for Zephyr. MP reuses many concepts from GStreamer, such as elements,
pads, caps negotiation, and buffer negotiation and adopts a pipeline-
based architecture that decomposes multimedia processing into discrete,
interconnected elements.

It aims to simplify the development of multimedia applications by
providing simple and stable APIs for users to rapidly create their
specific applications, i.e., users simply select the built-in elements
and plugins suited to their purpose to construct a pipeline, and it
just works. This design promotes modularity, reusability, and efficient
resource management (e.g., zero-copy data flow). Moreover, the APIs
are generic enough so that application code can remain unchanged even
as MP evolves.

MP also features a highly modular, inheritance-based architecture
inspired by GStreamer, ensuring modularity, scalability, and
maintainability. For example, new custom elements can be easily added
by extending existing elements, without requiring modifications to the
core components. Plugins are decentralized from the core structures,
allowing seamless extension without altering the core framework.

${SOB}
Signed-off-by: Trung Hieu Le <trunghieu.le@nxp.com>"

ZVID_COMMIT_MSG="mp: Add zvid video plugin

Add the zvid (Zephyr Video) plugin for the MP subsystem. This plugin
provides video-specific elements that interface with Zephyr's video
subsystem, enabling building video capture and processing pipelines
using Zephyr video devices, e.g. camera, m2m devices

${SOB}"

ZJPEG_COMMIT_MSG="mp: Add zjpeg JPEG plugin

Add the zjpeg (Zephyr JPEG) plugin for the MP subsystem.

The plugin currently includes a JPEG parser element for extracting
JPEG frames from a byte stream, a SW-based JPEG decoder element for
decompressing JPEG data into raw video frames. JPEG encoder will be
supported in the future.

${SOB}"

ZAUD_COMMIT_MSG="mp: Add zaud audio plugin

Add the zaud (Zephyr Audio) plugin for the MP subsystem. This plugin
provides audio-specific elements that interface with Zephyr's audio
subsystems, enabling building audio capture, processing, and playback
pipelines using Zephyr audio devices.

The plugin includes audio source elements for PCM and DMIC capture,
an I2S codec sink for audio output, a gain control transform for
per-sample amplitude scaling, and audio buffer pool management.

${SOB}"

ZDISP_COMMIT_MSG="mp: Add zdisp display plugin

Add the zdisp (Zephyr Display) plugin for the MP subsystem. This
plugin provides display output elements that interface with Zephyr's
display subsystem, enabling building video display pipelines that
output processed frames to physical displays.

The plugin includes a display sink element that renders video frames
to a Zephyr display device, supporting partial frame updates and
configurable display regions.

${SOB}"

ZFS_COMMIT_MSG="mp: Add zfs filesystem plugin

Add the zfs (Zephyr Filesystem) plugin for the MP subsystem. This
plugin provides filesystem I/O elements that interface with Zephyr's
filesystem subsystem, enabling building pipelines that read from or
write to files on any Zephyr-supported filesystem (FAT, LittleFS,
etc.).

The plugin includes a file source element for reading data and a
file sink element for writing pipeline data using Zephyr's
filesystem API.

${SOB}"

SAMPLE_CAM_DISP_COMMIT_MSG="mp: samples: Add camera to display sample

Add the cam_disp sample application demonstrating how to build a
camera-to-display pipeline using the MP subsystem. This sample
captures video frames from a camera device using the zvid plugin and
renders them on a display using the zdisp plugin, showcasing
real-time video preview functionality.

${SOB}"

SAMPLE_JPEG_DEC_COMMIT_MSG="mp: samples: Add JPEG decoding sample

Add the jpeg_dec sample application demonstrating how to decode JPEG
images using the MP subsystem. This sample reads JPEG-compressed
data, decodes it using the zvid plugin's JPEG decoder elements, and
outputs the resulting video frames, showcasing the JPEG decoding
pipeline.

${SOB}"

SAMPLE_FS_COMMIT_MSG="mp: samples: Add filesystem sample

Add the fs sample application demonstrating how to read from and
write to files using the MP subsystem. This sample uses the zfs
plugin's file source and file sink elements to build a pipeline
that performs filesystem I/O on any Zephyr-supported filesystem.

${SOB}"

# ===========================================================================
# Dependency map: target -> dependency branches (in cherry-pick order)
# ===========================================================================

declare -A TARGET_DEPS
TARGET_DEPS=(
    [core]=""
    [zvid]="${UPSTREAM_PREFIX}-core"
    [zjpeg]="${UPSTREAM_PREFIX}-core"
    [zaud]="${UPSTREAM_PREFIX}-core"
    [zdisp]="${UPSTREAM_PREFIX}-core"
    [zfs]="${UPSTREAM_PREFIX}-core"
    [sample-cam_disp]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-zvid ${UPSTREAM_PREFIX}-zdisp"
    [sample-jpeg_dec]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-zvid ${UPSTREAM_PREFIX}-zjpeg ${UPSTREAM_PREFIX}-zdisp ${UPSTREAM_PREFIX}-zfs"
    [sample-fs]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-zfs"
)

# ===========================================================================
# Helpers
# ===========================================================================

DRY_RUN=false
SKIP_COMPLIANCE=false
TARGETS=()

log_info() {
    echo -e "\033[1;34m[INFO]\033[0m $*"
}

log_ok() {
    echo -e "\033[1;32m[OK]\033[0m $*"
}

log_warn() {
    echo -e "\033[1;33m[WARN]\033[0m $*"
}

log_error() {
    echo -e "\033[1;31m[ERROR]\033[0m $*"
}

die() {
    log_error "$@"
    exit 1
}

# Check that we're in the zephyr repo root
check_prerequisites() {
    if [ ! -f "Kconfig.zephyr" ]; then
        die "Must be run from the zephyr repository root"
    fi

    # Verify source branch exists
    if ! git rev-parse --verify "${SOURCE_BRANCH}" >/dev/null 2>&1; then
        die "Source branch '${SOURCE_BRANCH}' not found"
    fi

    # Verify base ref exists
    if ! git rev-parse --verify "${BASE_REF}" >/dev/null 2>&1; then
        die "Base ref '${BASE_REF}' not found. Run: git fetch mmiot"
    fi

    # Check for clean working tree
    if ! git diff --quiet || ! git diff --cached --quiet; then
        die "Working tree is not clean. Please commit or stash changes first."
    fi
}

# Check that all dependency branches exist for a target
# Args: $1=target_name
check_deps() {
    local target="$1"
    local deps="${TARGET_DEPS[${target}]}"

    if [ -z "${deps}" ]; then
        return 0
    fi

    for dep in ${deps}; do
        if ! git rev-parse --verify "${dep}" >/dev/null 2>&1; then
            log_error "Dependency branch '${dep}' not found for target '${target}'."
            log_error "Generate it first: ./scripts/export_mp_upstream.sh $(echo "${dep}" | sed "s|${UPSTREAM_PREFIX}-||")"
            return 1
        fi
    done
    return 0
}

# Generate a single upstream branch for a target.
# The branch starts from BASE_REF, cherry-picks dependency commits, then
# adds the target's own commit on top.
#
# Args: $1=target_name, $2=branch_name, $3=commit_msg, $4+=paths
generate_branch() {
    local target="$1"
    local branch="$2"
    local commit_msg="$3"
    shift 3
    local paths=("$@")
    local deps="${TARGET_DEPS[${target}]}"

    log_info "Generating branch: ${branch}"
    log_info "  Base: ${BASE_REF}"
    if [ -n "${deps}" ]; then
        log_info "  Dependencies: ${deps}"
    fi
    log_info "  Paths: ${paths[*]}"

    if ${DRY_RUN}; then
        log_info "  [DRY RUN] Would create branch '${branch}' from '${BASE_REF}'"
        if [ -n "${deps}" ]; then
            log_info "  [DRY RUN] Would cherry-pick from: ${deps}"
        fi
        log_info "  [DRY RUN] With files from ${SOURCE_BRANCH} -- ${paths[*]}"
        echo ""
        return 0
    fi

    # Check dependencies exist
    if ! check_deps "${target}"; then
        return 1
    fi

    # Remember current branch to return to it
    local current_branch
    current_branch="$(git branch --show-current)"

    # Create the branch from BASE_REF
    git checkout -B "${branch}" "${BASE_REF}" --quiet

    # Cherry-pick dependency commits (tip of each dependency branch)
    if [ -n "${deps}" ]; then
        for dep in ${deps}; do
            log_info "  Cherry-picking from ${dep}..."
            git -c core.hooksPath=/dev/null cherry-pick "$(git rev-parse "${dep}")" --quiet
        done
    fi

    # Checkout the exact file state from the source branch for each path.
    # This handles both new files and modified files correctly.
    local has_files=false
    for path in "${paths[@]}"; do
        if git ls-tree -r "${SOURCE_BRANCH}" -- "${path}" 2>/dev/null | grep -q .; then
            git checkout "${SOURCE_BRANCH}" -- "${path}"
            has_files=true
        fi
    done

    if ! ${has_files}; then
        log_warn "  No files found for target '${target}'. Skipping."
        git checkout "${current_branch}" --quiet
        return 1
    fi

    git add -A

    # Check if there are changes to commit
    if git diff --cached --quiet; then
        log_warn "  No changes to commit for target '${target}'. Skipping."
        git checkout "${current_branch}" --quiet
        return 1
    fi

    # Commit with the proper message (--no-verify to skip git hooks)
    git commit --no-verify -m "${commit_msg}" --quiet

    log_ok "  Branch '${branch}' created successfully"
    log_info "  Commit: $(git --no-pager log --oneline -1)"

    # Show stats
    git --no-pager diff --stat HEAD~1 HEAD | tail -3

    # Return to original branch
    git checkout "${current_branch}" --quiet
    echo ""
}

# Run compliance check on a branch (only the target's own commit).
# Uses HEAD~1..HEAD to check only the last commit, not dependencies.
check_compliance() {
    local branch="$1"

    log_info "Running compliance check on '${branch}' (HEAD~1..HEAD)..."

    if ${DRY_RUN}; then
        log_info "  [DRY RUN] Would run compliance check"
        return 0
    fi

    local current_branch
    current_branch="$(git branch --show-current)"

    git checkout "${branch}" --quiet

    # Find the compliance script
    local compliance_script="scripts/ci/check_compliance.py"
    if [ ! -f "${compliance_script}" ]; then
        log_warn "  Compliance script not found at ${compliance_script}. Skipping check."
        git checkout "${current_branch}" --quiet
        return 0
    fi

    # Check only the target's own commit (the last one), not dependencies
    local result=0
    python3 "${compliance_script}" -c "HEAD~1..HEAD" 2>&1 | \
        sed 's/^/  /' || result=$?

    git checkout "${current_branch}" --quiet

    if [ ${result} -ne 0 ]; then
        log_error "  Compliance check FAILED for '${branch}'"
        log_error "  Fix the issues in '${SOURCE_BRANCH}', then re-run this script."
        return 1
    fi

    log_ok "  Compliance check passed for '${branch}'"
    return 0
}

# ===========================================================================
# Target dispatch
# ===========================================================================

export_core() {
    generate_branch "core" "${UPSTREAM_PREFIX}-core" \
        "${CORE_COMMIT_MSG}" "${CORE_PATHS[@]}"
}

export_zvid() {
    generate_branch "zvid" "${UPSTREAM_PREFIX}-zvid" \
        "${ZVID_COMMIT_MSG}" "${ZVID_PATHS[@]}"
}

export_zjpeg() {
    generate_branch "zjpeg" "${UPSTREAM_PREFIX}-zjpeg" \
        "${ZJPEG_COMMIT_MSG}" "${ZJPEG_PATHS[@]}"
}

export_zaud() {
    generate_branch "zaud" "${UPSTREAM_PREFIX}-zaud" \
        "${ZAUD_COMMIT_MSG}" "${ZAUD_PATHS[@]}"
}

export_zdisp() {
    generate_branch "zdisp" "${UPSTREAM_PREFIX}-zdisp" \
        "${ZDISP_COMMIT_MSG}" "${ZDISP_PATHS[@]}"
}

export_zfs() {
    generate_branch "zfs" "${UPSTREAM_PREFIX}-zfs" \
        "${ZFS_COMMIT_MSG}" "${ZFS_PATHS[@]}"
}

export_sample_cam_disp() {
    generate_branch "sample-cam_disp" "${UPSTREAM_PREFIX}-sample-cam_disp" \
        "${SAMPLE_CAM_DISP_COMMIT_MSG}" "${SAMPLE_CAM_DISP_PATHS[@]}"
}

export_sample_jpeg_dec() {
    generate_branch "sample-jpeg_dec" "${UPSTREAM_PREFIX}-sample-jpeg_dec" \
        "${SAMPLE_JPEG_DEC_COMMIT_MSG}" "${SAMPLE_JPEG_DEC_PATHS[@]}"
}

export_sample_fs() {
    generate_branch "sample-fs" "${UPSTREAM_PREFIX}-sample-fs" \
        "${SAMPLE_FS_COMMIT_MSG}" "${SAMPLE_FS_PATHS[@]}"
}

# ===========================================================================
# Export all targets
# ===========================================================================

export_all() {
    TARGETS=(core zvid zjpeg zaud zdisp zfs sample-cam_disp sample-jpeg_dec sample-fs)

    log_info "=== Exporting all MP upstream PR branches ==="
    log_info "Source: ${SOURCE_BRANCH}"
    log_info "Base:   ${BASE_REF}"
    log_info "Date:   ${TODAY}"
    echo ""

    # Core must be first (plugins depend on it)
    export_core

    # Plugins (independent of each other, all depend on core)
    export_zvid
    export_zjpeg
    export_zaud
    export_zdisp
    export_zfs

    # Samples (depend on core + relevant plugin)
    export_sample_cam_disp
    export_sample_jpeg_dec
    export_sample_fs

    if ! ${SKIP_COMPLIANCE}; then
        echo ""
        log_info "=== Running compliance checks ==="
        echo ""

        local failed_targets=()
        for target in "${TARGETS[@]}"; do
            local branch="${UPSTREAM_PREFIX}-${target}"
            if ! check_compliance "${branch}"; then
                failed_targets+=("${target}")
            fi
        done

        echo ""
        if [ ${#failed_targets[@]} -eq 0 ]; then
            log_info "=== All compliance checks passed ==="
        else
            log_warn "=== Compliance check summary ==="
            log_warn "FAILED targets: ${failed_targets[*]}"
            log_warn ""
            log_warn "To fix:"
            log_warn "  1. Fix compliance issues in '${SOURCE_BRANCH}' and commit"
            log_warn "  2. Push '${SOURCE_BRANCH}' to mmiot: git push mmiot ${SOURCE_BRANCH}"
            log_warn "  3. Re-run: ./scripts/export_mp_upstream.sh"
        fi
    fi

    echo ""
    log_info "=== Export complete ==="
    log_info ""
    log_info "Generated branches:"
    for target in "${TARGETS[@]}"; do
        local branch="${UPSTREAM_PREFIX}-${target}"
        echo "  ${branch}: $(git --no-pager log --oneline -1 "${branch}" 2>/dev/null || echo 'N/A')"
    done
    log_info ""
    log_info "To push upstream PR branches to your fork:"
    for target in "${TARGETS[@]}"; do
        echo "  git push <remote> ${UPSTREAM_PREFIX}-${target}:mp-${target} --force"
    done
    log_info ""
    log_info "To push libmp_dev to mmiot:"
    echo "  git push mmiot ${SOURCE_BRANCH}"
}

# ===========================================================================
# Main
# ===========================================================================

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] [TARGET...]

Export MP subsystem from libmp_dev to upstream PR branches.

Each branch is built from ${BASE_REF}, with dependency commits cherry-picked
first, then the target's own commit added on top. Compliance checks only
verify the target's own commit (HEAD~1..HEAD).

Targets:
  core             Core MP framework (no dependencies)
  zvid             Video plugin (depends on core)
  zjpeg            JPEG plugin (depends on core)
  zaud             Audio plugin (depends on core)
  zdisp            Display plugin (depends on core)
  zfs              Filesystem plugin (depends on core)
  sample-cam_disp  Camera-to-display sample (depends on core, zvid, zdisp)
  sample-jpeg_dec  JPEG decoding sample (depends on core, zvid, zjpeg, zdisp, zfs)
  sample-fs        Filesystem sample (depends on core, zfs)
  all              All of the above (default)

Options:
  --dry-run     Show what would be done without making changes
  --list        List available targets
  --no-comply   Skip compliance checks
  --help        Show this help

Examples:
  $(basename "$0")                      # Export all
  $(basename "$0") core                 # Export core only
  $(basename "$0") core zvid            # Export core then zvid
  $(basename "$0") sample-cam_disp      # Export sample (core+zvid must exist)
  $(basename "$0") --dry-run            # Preview all exports
EOF
}

main() {
    local targets=()

    while [ $# -gt 0 ]; do
        case "$1" in
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --list)
                echo "Available targets: core zvid zjpeg zaud zdisp zfs sample-cam_disp sample-jpeg_dec sample-fs"
                exit 0
                ;;
            --no-comply)
                SKIP_COMPLIANCE=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            core|zvid|zjpeg|zaud|zdisp|zfs|sample-cam_disp|sample-jpeg_dec|sample-fs|all)
                targets+=("$1")
                shift
                ;;
            *)
                die "Unknown argument: $1. Use --help for usage."
                ;;
        esac
    done

    # Default to all if no targets specified
    if [ ${#targets[@]} -eq 0 ]; then
        targets=(all)
    fi

    check_prerequisites

    for target in "${targets[@]}"; do
        case "${target}" in
            all)
                export_all
                return
                ;;
            core)
                TARGETS+=(core)
                export_core
                ;;
            zvid)
                TARGETS+=(zvid)
                export_zvid
                ;;
            zjpeg)
                TARGETS+=(zjpeg)
                export_zjpeg
                ;;
            zaud)
                TARGETS+=(zaud)
                export_zaud
                ;;
            zdisp)
                TARGETS+=(zdisp)
                export_zdisp
                ;;
            zfs)
                TARGETS+=(zfs)
                export_zfs
                ;;
            sample-cam_disp)
                TARGETS+=(sample-cam_disp)
                export_sample_cam_disp
                ;;
            sample-jpeg_dec)
                TARGETS+=(sample-jpeg_dec)
                export_sample_jpeg_dec
                ;;
            sample-fs)
                TARGETS+=(sample-fs)
                export_sample_fs
                ;;
        esac
    done

    if ! ${SKIP_COMPLIANCE}; then
        echo ""
        log_info "=== Running compliance checks ==="
        echo ""

        local failed_targets_single=()
        for target in "${TARGETS[@]}"; do
            local branch="${UPSTREAM_PREFIX}-${target}"
            if ! check_compliance "${branch}"; then
                failed_targets_single+=("${target}")
            fi
        done

        echo ""
        if [ ${#failed_targets_single[@]} -eq 0 ]; then
            log_info "=== All compliance checks passed ==="
        else
            log_warn "=== Compliance check summary ==="
            log_warn "FAILED targets: ${failed_targets_single[*]}"
            log_warn ""
            log_warn "To fix:"
            log_warn "  1. Fix compliance issues in '${SOURCE_BRANCH}' and commit"
            log_warn "  2. Push '${SOURCE_BRANCH}' to mmiot: git push mmiot ${SOURCE_BRANCH}"
            log_warn "  3. Re-run: ./scripts/export_mp_upstream.sh"
        fi
    fi

    echo ""
    log_info "=== Export complete ==="
    log_info ""
    log_info "Generated branches:"
    for target in "${TARGETS[@]}"; do
        local branch="${UPSTREAM_PREFIX}-${target}"
        echo "  ${branch}: $(git --no-pager log --oneline -1 "${branch}" 2>/dev/null || echo 'N/A')"
    done
    log_info ""
    log_info "To push upstream PR branches to your fork:"
    for target in "${TARGETS[@]}"; do
        echo "  git push <remote> ${UPSTREAM_PREFIX}-${target}:mp-${target} --force"
    done
    log_info ""
    log_info "To push libmp_dev to mmiot:"
    echo "  git push mmiot ${SOURCE_BRANCH}"
}

main "$@"
