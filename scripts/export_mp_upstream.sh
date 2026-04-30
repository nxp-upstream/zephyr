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
# Usage:
#   ./scripts/export_mp_upstream.sh              # Export all PRs
#   ./scripts/export_mp_upstream.sh core          # Export core only
#   ./scripts/export_mp_upstream.sh zvid          # Export zvid only (includes core)
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
# This is mmiot/main's "console: use zephyr ring buffer" commit
BASE_REF="mmiot/main"

# Branch name prefix for generated upstream branches
UPSTREAM_PREFIX="upstream/mp"

# Signed-off-by (from git config)
SOB_NAME="$(git config user.name)"
SOB_EMAIL="$(git config user.email)"
SOB="Signed-off-by: ${SOB_NAME} <${SOB_EMAIL}>"

# Date for tagging
TODAY="$(date +%Y-%m-%d)"

# ===========================================================================
# File mappings per PR target
# ===========================================================================

# Core: framework files + integration into subsys/Kconfig and subsys/CMakeLists.txt
CORE_PATHS=(
    "subsys/mp/Kconfig"
    "subsys/mp/CMakeLists.txt"
    "subsys/mp/src/core/"
    "subsys/mp/src/plugins/Kconfig"
    "subsys/mp/src/plugins/CMakeLists.txt"
    "include/zephyr/mp/mp.h"
    "include/zephyr/mp/core/"
    "subsys/Kconfig"
    "subsys/CMakeLists.txt"
    "lib/Kconfig"
    "lib/CMakeLists.txt"
)

# zvid plugin (includes zjpeg - JPEG support is part of zvid)
ZVID_PATHS=(
    "subsys/mp/src/plugins/zvid/"
    "include/zephyr/mp/zvid/"
)

# zaud plugin
ZAUD_PATHS=(
    "subsys/mp/src/plugins/zaud/"
    "include/zephyr/mp/zaud/"
)

# zdisp plugin
ZDISP_PATHS=(
    "subsys/mp/src/plugins/zdisp/"
    "include/zephyr/mp/zdisp/"
)

# zfs plugin
ZFS_PATHS=(
    "subsys/mp/src/plugins/zfs/"
    "include/zephyr/mp/zfs/"
)

# ===========================================================================
# Commit messages (following Zephyr convention: area: Short description)
# ===========================================================================

CORE_COMMIT_MSG="subsys: mp: Introduce Media Pipe (MP) subsystem

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

${SOB}"

ZVID_COMMIT_MSG="subsys: mp: Add zvid video plugin

Add the zvid (Zephyr Video) plugin for the MP subsystem. This plugin
provides video-specific elements that interface with Zephyr's video
subsystem, enabling building video capture and processing pipelines
using Zephyr video devices.

The plugin includes video source, transform, and buffer pool elements
for capturing, processing, and managing video buffers. It also
provides multi-core support via RPC-based remote video transform for
offloading video processing to secondary cores, pixel format
conversion utilities, and JPEG decoding support (parser and decoder
elements for processing compressed image data from cameras or file
sources).

${SOB}"

ZAUD_COMMIT_MSG="subsys: mp: Add zaud audio plugin

Add the zaud (Zephyr Audio) plugin for the MP subsystem. This plugin
provides audio-specific elements that interface with Zephyr's audio
subsystems, enabling building audio capture, processing, and playback
pipelines using Zephyr audio devices.

The plugin includes audio source elements for PCM and DMIC capture,
an I2S codec sink for audio output, a gain control transform for
per-sample amplitude scaling, and audio buffer pool management.

${SOB}"

ZDISP_COMMIT_MSG="subsys: mp: Add zdisp display plugin

Add the zdisp (Zephyr Display) plugin for the MP subsystem. This
plugin provides display output elements that interface with Zephyr's
display subsystem, enabling building video display pipelines that
output processed frames to physical displays.

The plugin includes a display sink element that renders video frames
to a Zephyr display device, supporting partial frame updates and
configurable display regions.

${SOB}"

ZFS_COMMIT_MSG="subsys: mp: Add zfs filesystem plugin

Add the zfs (Zephyr Filesystem) plugin for the MP subsystem. This
plugin provides filesystem I/O elements that interface with Zephyr's
filesystem subsystem, enabling building pipelines that read from or
write to files on any Zephyr-supported filesystem (FAT, LittleFS,
etc.).

The plugin includes a file source element for reading data and a
file sink element for writing pipeline data using Zephyr's
filesystem API.

${SOB}"

# ===========================================================================
# Helpers
# ===========================================================================

DRY_RUN=false
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

# Generate a single upstream branch for a target
# Args: $1=target_name, $2=branch_name, $3=base_branch, $4=commit_msg, $5+=paths
generate_branch() {
    local target="$1"
    local branch="$2"
    local base_branch="$3"
    local commit_msg="$4"
    shift 4
    local paths=("$@")

    log_info "Generating branch: ${branch}"
    log_info "  Base: ${base_branch}"
    log_info "  Paths: ${paths[*]}"

    if ${DRY_RUN}; then
        log_info "  [DRY RUN] Would create branch '${branch}' from '${base_branch}'"
        log_info "  [DRY RUN] With files from ${SOURCE_BRANCH} -- ${paths[*]}"
        echo ""
        return 0
    fi

    # Remember current branch to return to it
    local current_branch
    current_branch="$(git branch --show-current)"

    # Create the branch from the base
    git checkout -B "${branch}" "${base_branch}" --quiet

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

    # Commit with the proper message
    git commit -m "${commit_msg}" --quiet

    log_ok "  Branch '${branch}' created successfully"
    log_info "  Commit: $(git --no-pager log --oneline -1)"

    # Show stats
    git --no-pager diff --stat HEAD~1 HEAD | tail -3

    # Return to original branch
    git checkout "${current_branch}" --quiet
    echo ""
}

# Run compliance check on a branch
check_compliance() {
    local branch="$1"
    local base_branch="$2"

    log_info "Running compliance check on '${branch}'..."

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

    local result=0
    python3 "${compliance_script}" -c "${base_branch}..HEAD" 2>&1 | while IFS= read -r line; do
        echo "  ${line}"
    done || result=$?

    git checkout "${current_branch}" --quiet

    if [ ${result} -ne 0 ]; then
        log_error "  Compliance check FAILED for '${branch}'"
        log_error "  Fix the issues in '${SOURCE_BRANCH}', then re-run this script."
        return 1
    fi

    log_ok "  Compliance check passed for '${branch}'"
    return 0
}

# Tag the source branch
tag_export() {
    local tag_name="upstream-export/${TODAY}"

    if git rev-parse --verify "refs/tags/${tag_name}" >/dev/null 2>&1; then
        # Tag already exists for today, add a sequence number
        local seq=1
        while git rev-parse --verify "refs/tags/${tag_name}-${seq}" >/dev/null 2>&1; do
            seq=$((seq + 1))
        done
        tag_name="${tag_name}-${seq}"
    fi

    if ${DRY_RUN}; then
        log_info "[DRY RUN] Would create tag '${tag_name}' on '${SOURCE_BRANCH}'"
        return 0
    fi

    git tag -a "${tag_name}" "${SOURCE_BRANCH}" -m "Upstream export on ${TODAY}

Exported the following upstream PR branches:
$(for t in "${TARGETS[@]}"; do echo "  - ${UPSTREAM_PREFIX}-${t}"; done)

Source: ${SOURCE_BRANCH} ($(git rev-parse --short "${SOURCE_BRANCH}"))
Base: ${BASE_REF} ($(git rev-parse --short "${BASE_REF}"))"

    log_ok "Created tag '${tag_name}' on '${SOURCE_BRANCH}'"
}

# ===========================================================================
# Target dispatch
# ===========================================================================

export_core() {
    generate_branch "core" "${UPSTREAM_PREFIX}-core" "${BASE_REF}" \
        "${CORE_COMMIT_MSG}" "${CORE_PATHS[@]}"
}

export_zvid() {
    generate_branch "zvid" "${UPSTREAM_PREFIX}-zvid" "${UPSTREAM_PREFIX}-core" \
        "${ZVID_COMMIT_MSG}" "${ZVID_PATHS[@]}"
}

export_zaud() {
    generate_branch "zaud" "${UPSTREAM_PREFIX}-zaud" "${UPSTREAM_PREFIX}-core" \
        "${ZAUD_COMMIT_MSG}" "${ZAUD_PATHS[@]}"
}

export_zdisp() {
    generate_branch "zdisp" "${UPSTREAM_PREFIX}-zdisp" "${UPSTREAM_PREFIX}-core" \
        "${ZDISP_COMMIT_MSG}" "${ZDISP_PATHS[@]}"
}

export_zfs() {
    generate_branch "zfs" "${UPSTREAM_PREFIX}-zfs" "${UPSTREAM_PREFIX}-core" \
        "${ZFS_COMMIT_MSG}" "${ZFS_PATHS[@]}"
}

export_all() {
    TARGETS=(core zvid zaud zdisp zfs)

    log_info "=== Exporting all MP upstream PR branches ==="
    log_info "Source: ${SOURCE_BRANCH}"
    log_info "Base:   ${BASE_REF}"
    log_info "Date:   ${TODAY}"
    echo ""

    # Core must be first (plugins depend on it)
    export_core

    # Plugins (independent of each other, all depend on core)
    export_zvid
    export_zaud
    export_zdisp
    export_zfs

    echo ""
    log_info "=== Running compliance checks ==="
    echo ""

    local failed_targets=()
    for target in "${TARGETS[@]}"; do
        local branch="${UPSTREAM_PREFIX}-${target}"
        local base
        if [ "${target}" = "core" ]; then
            base="${BASE_REF}"
        else
            base="${UPSTREAM_PREFIX}-core"
        fi

        if ! check_compliance "${branch}" "${base}"; then
            failed_targets+=("${target}")
        fi
    done

    echo ""
    if [ ${#failed_targets[@]} -eq 0 ]; then
        log_info "=== All compliance checks passed ==="
        tag_export
    else
        log_warn "=== Compliance check summary ==="
        log_warn "FAILED targets: ${failed_targets[*]}"
        log_warn ""
        log_warn "To fix:"
        log_warn "  1. Fix compliance issues in '${SOURCE_BRANCH}' and commit"
        log_warn "  2. Push '${SOURCE_BRANCH}' to mmiot: git push mmiot ${SOURCE_BRANCH}"
        log_warn "  3. Re-run: ./scripts/export_mp_upstream.sh"
        echo ""
        log_info "Tagging anyway so you can track this export attempt..."
        tag_export
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

Targets:
  core      Core MP framework
  zvid      Video plugin (includes JPEG support and core as dependency)
  zaud      Audio plugin (includes core as dependency)
  zdisp     Display plugin (includes core as dependency)
  zfs       Filesystem plugin (includes core as dependency)
  all       All of the above (default)

Options:
  --dry-run     Show what would be done without making changes
  --list        List available targets
  --no-comply   Skip compliance checks
  --help        Show this help

Examples:
  $(basename "$0")                  # Export all
  $(basename "$0") core             # Export core only
  $(basename "$0") zvid             # Export core + zvid
  $(basename "$0") --dry-run        # Preview all exports
EOF
}

SKIP_COMPLIANCE=false

main() {
    local targets=()

    while [ $# -gt 0 ]; do
        case "$1" in
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --list)
                echo "Available targets: core zvid zaud zdisp zfs"
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
            core|zvid|zaud|zdisp|zfs|all)
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
                # Ensure core is exported first (zvid depends on it)
                if ! git rev-parse --verify "${UPSTREAM_PREFIX}-core" >/dev/null 2>&1; then
                    log_info "Exporting core first (zvid depends on it)..."
                    TARGETS+=(core)
                    export_core
                fi
                TARGETS+=(zvid)
                export_zvid
                ;;
            zaud)
                if ! git rev-parse --verify "${UPSTREAM_PREFIX}-core" >/dev/null 2>&1; then
                    log_info "Exporting core first (zaud depends on it)..."
                    TARGETS+=(core)
                    export_core
                fi
                TARGETS+=(zaud)
                export_zaud
                ;;
            zdisp)
                if ! git rev-parse --verify "${UPSTREAM_PREFIX}-core" >/dev/null 2>&1; then
                    log_info "Exporting core first (zdisp depends on it)..."
                    TARGETS+=(core)
                    export_core
                fi
                TARGETS+=(zdisp)
                export_zdisp
                ;;
            zfs)
                if ! git rev-parse --verify "${UPSTREAM_PREFIX}-core" >/dev/null 2>&1; then
                    log_info "Exporting core first (zfs depends on it)..."
                    TARGETS+=(core)
                    export_core
                fi
                TARGETS+=(zfs)
                export_zfs
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
            local base
            if [ "${target}" = "core" ]; then
                base="${BASE_REF}"
            else
                base="${UPSTREAM_PREFIX}-core"
            fi

            if ! check_compliance "${branch}" "${base}"; then
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

    # Tag always
    tag_export

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
