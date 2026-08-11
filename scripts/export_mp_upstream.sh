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
#   ./scripts/export_mp_upstream.sh vid           # Export vid only (core must exist)
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

# Explicit commit authorship.
#
# The author and Signed-off-by trailers are hardcoded (NOT taken from the
# git config of whoever runs this script) so that the generated commits
# always carry the correct authorship regardless of who runs the export.
#
# Authors (used with `git commit --author=...`)
AUTHOR_PHIBANG="Phi Bang Nguyen <phibang.nguyen@nxp.com>"
AUTHOR_MICHAL="Michal Chvatal <michal.chvatal@nxp.com>"

# Signed-off-by trailers
SOB_PHIBANG="Signed-off-by: Phi Bang Nguyen <phibang.nguyen@nxp.com>"
SOB_TRUNGHIEU="Signed-off-by: Trung Hieu Le <trunghieu.le@nxp.com>"
SOB_MICHAL="Signed-off-by: Michal Chvatal <michal.chvatal@nxp.com>"
SOB_TOMAS="Signed-off-by: Tomas Barak <tomas.barak@nxp.com>"


# Date for display
TODAY="$(date +%Y-%m-%d)"

# ===========================================================================
# File mappings per PR target
# ===========================================================================

# Core: framework files + integration into subsys/Kconfig and subsys/CMakeLists.txt
#
# Core sources now live flattened directly under subsys/mp/*.c and their
# public headers under include/zephyr/mp/*.h (no more core/ or plugins/
# subdirectories). Because plugins share the subsys/mp/ directory as
# subdirectories, the core paths are listed explicitly (a bare
# "subsys/mp/" pathspec would also drag in the plugin subdirectories).
CORE_PATHS=(
    "subsys/mp/Kconfig"
    "subsys/mp/CMakeLists.txt"
    "subsys/mp/mp_bin.c"
    "subsys/mp/mp_buffer.c"
    "subsys/mp/mp_bus.c"
    "subsys/mp/mp_dispatch.c"
    "subsys/mp/mp_element.c"
    "subsys/mp/mp_fake_src.c"
    "subsys/mp/mp_object.c"
    "subsys/mp/mp_pad.c"
    "subsys/mp/mp_parser.c"
    "subsys/mp/mp_pipeline.c"
    "subsys/mp/mp_sink.c"
    "subsys/mp/mp_src.c"
    "subsys/mp/mp_structure.c"
    "subsys/mp/mp_thread.c"
    "subsys/mp/mp_transform.c"
    "subsys/mp/mp_transform_client.c"
    "subsys/mp/mp_value.c"
    "subsys/mp/mp_workqueue.c"
    "include/zephyr/mp/mp.h"
    "include/zephyr/mp/mp_bin.h"
    "include/zephyr/mp/mp_buffer.h"
    "include/zephyr/mp/mp_bus.h"
    "include/zephyr/mp/mp_dispatch.h"
    "include/zephyr/mp/mp_element.h"
    "include/zephyr/mp/mp_fake_src.h"
    "include/zephyr/mp/mp_message.h"
    "include/zephyr/mp/mp_object.h"
    "include/zephyr/mp/mp_pad.h"
    "include/zephyr/mp/mp_parser.h"
    "include/zephyr/mp/mp_pipeline.h"
    "include/zephyr/mp/mp_sink.h"
    "include/zephyr/mp/mp_src.h"
    "include/zephyr/mp/mp_structure.h"
    "include/zephyr/mp/mp_thread.h"
    "include/zephyr/mp/mp_transform.h"
    "include/zephyr/mp/mp_transform_client.h"
    "include/zephyr/mp/mp_value.h"
    "include/zephyr/mp/mp_workqueue.h"
    "subsys/Kconfig"
    "subsys/CMakeLists.txt"
    "lib/Kconfig"
    "lib/CMakeLists.txt"
    "MAINTAINERS.yml"
)

# vid plugin
VID_PATHS=(
    "subsys/mp/vid/"
    "include/zephyr/mp/vid/"
)

# img plugin (image codec support)
IMG_PATHS=(
    "subsys/mp/img/"
    "include/zephyr/mp/img/"
)

# aud plugin
AUD_PATHS=(
    "subsys/mp/aud/"
    "include/zephyr/mp/aud/"
)

# disp plugin
DISP_PATHS=(
    "subsys/mp/disp/"
    "include/zephyr/mp/disp/"
)

# fs plugin
FS_PATHS=(
    "subsys/mp/fs/"
    "include/zephyr/mp/fs/"
)

# base plugin
BASE_PATHS=(
    "subsys/mp/base/"
    "include/zephyr/mp/base/"
)

# utils (helper utilities built on top of the core, e.g. the mp_player
# pipeline controller). Directory globs so future files under utils/ are
# picked up automatically.
UTILS_PATHS=(
    "subsys/mp/utils/"
    "include/zephyr/mp/utils/"
    "tests/subsys/mp/utils/"
)


# Sample: cam_disp (camera to display pipeline)
SAMPLE_CAM_DISP_PATHS=(
    "samples/subsys/mp/cam_disp/"
)

# Sample: jpeg_dec (JPEG decoding pipeline)
SAMPLE_JPEG_DEC_PATHS=(
    "samples/subsys/mp/jpeg_dec/"
)

# Sample: tee_dec (tee JPEG decoding pipeline)
SAMPLE_TEE_DEC_PATHS=(
    "samples/subsys/mp/tee_dec/"
)

# Sample: fs (filesystem read/write pipeline)
SAMPLE_FS_PATHS=(
    "samples/subsys/mp/fs/"
)

# Sample: dmic_i2s (DMIC to I2S audio pipeline)
SAMPLE_DMIC_I2S_PATHS=(
    "samples/subsys/mp/dmic_i2s/"
)

# Core tests: unit tests and pipeline tests for libmp core
CORE_TEST_PATHS=(
    "tests/subsys/mp/unit/"
    "tests/subsys/mp/pipeline/"
    "tests/subsys/mp/build_all/"
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

${SOB_PHIBANG}
${SOB_TRUNGHIEU}"


VID_COMMIT_MSG="mp: Add video plugin

Add the vid (Zephyr Video) plugin for the MP subsystem. This plugin
provides video-specific elements that interface with Zephyr's video
subsystem, enabling building video capture and processing pipelines
using Zephyr video devices, e.g. camera, m2m devices

${SOB_PHIBANG}"


IMG_COMMIT_MSG="mp: Add image codec plugin

Add the img (Zephyr Image Codec) plugin for the MP subsystem.

The plugin currently includes a JPEG parser element for extracting
JPEG frames from a byte stream, a SW-based JPEG decoder element for
decompressing JPEG data into raw video frames. Other elements like
jpegenc, y4mdec, etc. will be added in the future.

${SOB_PHIBANG}"

AUD_COMMIT_MSG="mp: Add audio plugin

Add the aud (Zephyr Audio) plugin for the MP subsystem. This plugin
provides audio-specific elements that interface with Zephyr's audio
subsystems, enabling building audio capture, processing, and playback
pipelines using Zephyr audio devices.

The plugin includes audio source elements for PCM and DMIC capture,
an I2S codec sink for audio output, a gain control transform for
per-sample amplitude scaling, and audio buffer pool management.

${SOB_MICHAL}
${SOB_TOMAS}"


DISP_COMMIT_MSG="mp: Add display plugin

Add the disp (Zephyr Display) plugin for the MP subsystem. This
plugin provides display output elements that interface with Zephyr's
display subsystem, enabling building video display pipelines that
output processed frames to physical displays.

The plugin includes a display sink element that renders video frames
to a Zephyr display device, supporting partial frame updates and
configurable display regions.

${SOB_PHIBANG}"


FS_COMMIT_MSG="mp: Add filesystem plugin

Add the fs (Zephyr Filesystem) plugin for the MP subsystem. This
plugin provides filesystem I/O elements that interface with Zephyr's
filesystem subsystem, enabling building pipelines that read from or
write to files on any Zephyr-supported filesystem (FAT, LittleFS,
etc.).

The plugin includes a file source element for reading data and a
file sink element for writing pipeline data using Zephyr's
filesystem API.

${SOB_PHIBANG}"

BASE_COMMIT_MSG="mp: Add base plugin

Add the base plugin for the MP subsystem. This plugin provides
generic, reusable elements like:
- queue: pipeline-level threading element
- tee: pipeline branching element
- capsfilter: caps enforcement element

${SOB_PHIBANG}"

UTILS_COMMIT_MSG="mp: Add utils

Add the utils helpers. These are optional, reusable utilities built on
top of the subsys to simplify application development.

The utils currently includes:
- mp_player, a small pipeline controller that drives a pipeline
  through its states and exposes simple play/pause/stop/replay/quit
  controls (usable from a shell).
- mp_dump, which renders a pipeline topology, element states and the
  negotiated caps on each link as a Graphviz graph, on demand from the
  shell or automatically on every state change and error through the
  player.

Tests for the dump rendering are included.

Assisted-by: Claude:claude-opus-5

${SOB_PHIBANG}"

SAMPLE_CAM_DISP_COMMIT_MSG="mp: samples: Add camera to display sample

Add the cam_disp sample application demonstrating how to build a
camera-to-display pipeline using the MP subsystem. This sample
captures video frames from a camera device using the vid plugin and
renders them on a display using the disp plugin, showcasing
real-time video preview functionality.

${SOB_PHIBANG}"

SAMPLE_JPEG_DEC_COMMIT_MSG="mp: samples: Add JPEG decoding sample

Add the jpeg_dec sample application demonstrating how to decode JPEG
images using the MP subsystem. This sample reads JPEG-compressed
data, decodes it using the vid plugin's JPEG decoder elements, and
outputs the resulting video frames, showcasing the JPEG decoding
pipeline.

${SOB_PHIBANG}"

SAMPLE_TEE_DEC_COMMIT_MSG="mp: samples: Add TEE JPEG decoding sample

Add the tee_dec sample application demonstrating how to decode JPEG
images, display them and write the decoded data to a file at the same
time, showcasing pipeline branching feature of the MP subsystem.

${SOB_PHIBANG}"

SAMPLE_FS_COMMIT_MSG="mp: samples: Add filesystem sample

Add the fs sample application demonstrating how to read from and
write to files using the MP subsystem. This sample uses the fs
plugin's file source and file sink elements to build a pipeline
that performs filesystem I/O on any Zephyr-supported filesystem.

${SOB_PHIBANG}"

CORE_TEST_COMMIT_MSG="mp: Add core tests

Add build-only, unit and mock pipeline tests for the MP core.

The mock pipeline is composed of a fake source, a transform and
a sink to verify the whole core framework behavior such as
pipeline creation, caps negotiation and data flow.

Assisted-by: Claude:claude-opus-4.6
${SOB_PHIBANG}
${SOB_TRUNGHIEU}"

SAMPLE_DMIC_I2S_COMMIT_MSG="mp: samples: Add DMIC to I2S audio sample

Add the dmic_i2s sample application demonstrating how to build an
audio pipeline using the MP subsystem. This sample provides a simple
pipeline that captures audio from a digital microphone (DMIC), applies
gain control using the aud plugin's gain element, and outputs the
processed audio through an I2S codec to a speaker.

${SOB_MICHAL}
${SOB_TOMAS}"

# ===========================================================================
# Dependency map: target -> dependency branches (in cherry-pick order)
# ===========================================================================

declare -A TARGET_DEPS
TARGET_DEPS=(
    [core]=""
    [vid]="${UPSTREAM_PREFIX}-core"
    [img]="${UPSTREAM_PREFIX}-core"
    [aud]="${UPSTREAM_PREFIX}-core"
    [disp]="${UPSTREAM_PREFIX}-core"
    [fs]="${UPSTREAM_PREFIX}-core"
    [base]="${UPSTREAM_PREFIX}-core"
    [utils]="${UPSTREAM_PREFIX}-core"
    [sample-cam_disp]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-base \
        ${UPSTREAM_PREFIX}-vid ${UPSTREAM_PREFIX}-disp ${UPSTREAM_PREFIX}-utils"
    [sample-jpeg_dec]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-base \
        ${UPSTREAM_PREFIX}-vid ${UPSTREAM_PREFIX}-img ${UPSTREAM_PREFIX}-disp \
        ${UPSTREAM_PREFIX}-fs ${UPSTREAM_PREFIX}-utils"
    [sample-tee_dec]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-base \
        ${UPSTREAM_PREFIX}-vid ${UPSTREAM_PREFIX}-img ${UPSTREAM_PREFIX}-disp \
        ${UPSTREAM_PREFIX}-fs ${UPSTREAM_PREFIX}-utils"
    [sample-fs]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-fs"
    [sample-dmic_i2s]="${UPSTREAM_PREFIX}-core ${UPSTREAM_PREFIX}-base ${UPSTREAM_PREFIX}-aud"
)

# ===========================================================================
# Author map: target -> commit author (passed to `git commit --author=...`)
#
# This is the primary/main author of each commit. It is decoupled from the
# git config of the person running the script so that authorship is always
# correct regardless of who runs the export. The full list of contributors
# is captured by the Signed-off-by trailers in the commit messages above.
# ===========================================================================

declare -A TARGET_AUTHOR
TARGET_AUTHOR=(
    [core]="${AUTHOR_PHIBANG}"
    [vid]="${AUTHOR_PHIBANG}"
    [img]="${AUTHOR_PHIBANG}"
    [aud]="${AUTHOR_MICHAL}"
    [disp]="${AUTHOR_PHIBANG}"
    [fs]="${AUTHOR_PHIBANG}"
    [base]="${AUTHOR_PHIBANG}"
    [utils]="${AUTHOR_PHIBANG}"
    [sample-cam_disp]="${AUTHOR_PHIBANG}"
    [sample-jpeg_dec]="${AUTHOR_PHIBANG}"
    [sample-tee_dec]="${AUTHOR_PHIBANG}"
    [sample-fs]="${AUTHOR_PHIBANG}"
    [sample-dmic_i2s]="${AUTHOR_MICHAL}"
)

# ===========================================================================
# Build-all test map: target -> testcase name in build_all/tests.yaml
#
# tests/subsys/mp/build_all/tests.yaml in the source branch is a single
# file that contains one build_only entry per plugin (plus the core entry).
# When exporting, each commit must only carry the entries relevant to it:
#   - the core-tests commit keeps only 'mp.core.build'
#   - each plugin commit appends only its own entry
# Samples have no build_all entry (empty / unset).
# ===========================================================================

declare -A TARGET_BUILD_TEST
TARGET_BUILD_TEST=(
    [core]="mp.core.build"
    [base]="mp.base.build"
    [aud]="mp.audio.build"
    [vid]="mp.video.build"
    [disp]="mp.display.build"
    [img]="mp.img.build"
    [fs]="mp.fs.build"
)

# Path to the shared build_all testcase file (relative to repo root).
BUILD_ALL_TESTCASE="tests/subsys/mp/build_all/tests.yaml"


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

# Pause the run so the human operator can resolve a cherry-pick conflict by
# hand, then continue the same run once done. This is intentionally generic:
# it triggers on ANY cherry-pick conflict (not just tests.yaml), so future
# code changes that introduce new conflicts are handled without special-casing.
#
# The caller is expected to run `git cherry-pick --continue` after this returns
# 0. If stdin is not a TTY (e.g. CI / piped input), we cannot prompt, so we
# return non-zero to let the caller abort and fail loudly.
#
# Args: $1=commit (sha being picked), $2=subject (commit subject, for display)
# Returns: 0 if resolved and ready to continue, 1 if unable to prompt.
pause_for_manual_resolution() {
    local commit="$1"
    local subject="$2"

    log_warn "Cherry-pick paused on ${commit} (${subject})"
    log_warn "  Conflicted files:"
    git diff --name-only --diff-filter=U | sed 's/^/    /'

    # Non-interactive: cannot pause for manual resolution.
    if [ ! -t 0 ]; then
        log_error "  stdin is not a TTY; cannot pause for manual resolution."
        return 1
    fi

    log_warn "  Repository: $(pwd)"
    log_warn "  You will now be dropped into an interactive sub-shell in the"
    log_warn "  repository so you can resolve the conflict in THIS terminal:"
    log_warn "    1. Edit the conflicted file(s) to fix the '<<<<<<<' markers."
    log_warn "    2. Stage them:  git add <file>..."
    log_warn "    3. Type 'exit' (or press Ctrl-D) to resume the export."
    log_warn "  Do NOT run 'git cherry-pick --continue' yourself; the script"
    log_warn "  will do that for you once you leave the sub-shell."

    # Drop into an interactive sub-shell for resolution, then re-check. We only
    # loop while conflict markers (unmerged files) remain, so the operator can
    # always leave once the conflicts are resolved. We intentionally do NOT
    # loop on "nothing staged": a resolution can legitimately be empty (e.g. the
    # incoming change is already present), and trapping on that would make it
    # impossible to leave the sub-shell. The caller handles the empty case.
    while true; do
        # Run an interactive shell bound to the terminal so editing, git add,
        # git status, etc. all work in this same terminal. The shell inherits
        # the current working directory (the repo).
        "${SHELL:-/bin/bash}" </dev/tty >/dev/tty 2>&1 || true

        local unmerged
        unmerged="$(git diff --name-only --diff-filter=U)"
        if [ -n "${unmerged}" ]; then
            log_warn "  There are still unresolved (unmerged) files:"
            echo "${unmerged}" | sed 's/^/    /'
            log_warn "  Fix them and 'git add' them, then 'exit' the sub-shell"
            log_warn "  again (or 'git cherry-pick --abort' then 'exit' to skip)."
            continue
        fi

        break
    done

    return 0
}



# Extract a single named test block from the source build_all/tests.yaml.
# A block starts at a line "  <name>:" (two-space indent) and runs until the
# next top-level test entry ("  mp.*.build:") or end of file. Trailing blank
# lines are stripped. The extracted text is printed to stdout.
#
# Args: $1=test_name (e.g. "mp.base.build")

extract_build_test_block() {
    local test_name="$1"

    git show "${SOURCE_BRANCH}:${BUILD_ALL_TESTCASE}" 2>/dev/null | awk -v name="${test_name}" '
        # Detect the start of any top-level test entry (two-space indent).
        /^  mp\.[a-zA-Z0-9_.]+\.build:[[:space:]]*$/ {
            if ($0 == "  " name ":") {
                capturing = 1
            } else {
                capturing = 0
            }
        }
        capturing { print }
    ' | sed -e :a -e '/^[[:space:]]*$/{$d;N;ba}'
}

# Rewrite build_all/tests.yaml in the working tree so it contains only the
# "tests:" header plus the named test blocks passed as arguments (in order).
#
# Args: $1+=test_name(s)
write_build_test_file() {
    local names=("$@")
    local tmp
    tmp="$(mktemp)"

    echo "tests:" > "${tmp}"
    for name in "${names[@]}"; do
        echo "" >> "${tmp}"
        extract_build_test_block "${name}" >> "${tmp}"
    done

    mv "${tmp}" "${BUILD_ALL_TESTCASE}"
}

# Append the named test block to the existing build_all/tests.yaml in the
# working tree (used by plugin commits, which already inherit the file with
# only the core entry from the cherry-picked core-tests commit).
#
# Args: $1=test_name
append_build_test_block() {
    local test_name="$1"

    # Ensure exactly one blank line separates entries.
    printf '\n' >> "${BUILD_ALL_TESTCASE}"
    extract_build_test_block "${test_name}" >> "${BUILD_ALL_TESTCASE}"
}

# Print, one name per line, every top-level test block found on stdin (a
# build_all/tests.yaml stream). For example, "mp.core.build".
list_build_test_names() {
    awk '/^  mp\.[a-zA-Z0-9_.]+\.build:[[:space:]]*$/ { gsub(/[ :]/, ""); print }'
}

# Auto-resolve a conflict on build_all/tests.yaml during a cherry-pick.
#
# Every plugin commit appends its own test block right after the core block.
# When a sample cherry-picks several plugins, those appends collide. The
# correct resolution is the UNION of:
#   - the blocks already present on HEAD  (stage :2, "ours"), and
#   - the blocks from the incoming commit (stage :3, "theirs").
# This keeps every plugin already applied and adds the incoming plugin's block.
# Crucially, it does NOT pull in blocks from plugins that have not been
# cherry-picked yet: the resolution reflects only what actually exists at this
# point in the sequence, so the file grows one plugin at a time.
#
# The kept blocks are emitted in the canonical order in which they appear in the
# libmp_dev source file, and each block's content is regenerated verbatim from
# libmp_dev, so the result is always well-formed and deterministic.
resolve_build_test_conflict() {
    local ours theirs present ordered=()
    local name

    ours="$(git show ":2:${BUILD_ALL_TESTCASE}" 2>/dev/null | list_build_test_names)"
    theirs="$(git show ":3:${BUILD_ALL_TESTCASE}" 2>/dev/null | list_build_test_names)"

    # Space-padded haystack for whole-word membership tests. The command
    # substitutions collapse the newline-separated lists into spaces.
    present=" $(echo ${ours}) $(echo ${theirs}) "

    # Walk the source file in its natural order and keep the blocks that are
    # present on either side of the conflict.
    while IFS= read -r name; do
        [ -z "${name}" ] && continue
        if [[ "${present}" == *" ${name} "* ]]; then
            ordered+=("${name}")
        fi
    done < <(git show "${SOURCE_BRANCH}:${BUILD_ALL_TESTCASE}" | list_build_test_names)

    write_build_test_file "${ordered[@]}"
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
    # Cherry-pick a range of commits, silently dropping any that become empty
    # (i.e., already applied). Compatible with all Git versions.
    # Args: $1=to_ref
    cherry_pick_range() {
        local to_ref="$1"
        local commits

        # Use --cherry-pick to skip commits that are already applied
        # (patch-equivalent) on the current HEAD. This is essential: every
        # plugin branch carries its own copy of the core framework and the
        # "mp: Add core tests" commits, so when a sample cherry-picks several
        # plugin ranges those duplicates would otherwise be re-applied and
        # conflict (notably the core-tests commit trying to reset
        # build_all/tests.yaml back to core-only). --right-only keeps only
        # commits reachable from the dependency branch, not from HEAD.
        mapfile -t commits < <(git rev-list --reverse --cherry-pick --right-only "HEAD...${to_ref}")


        for commit in "${commits[@]}"; do
            if ! git -c core.hooksPath=/dev/null cherry-pick "${commit}" --quiet 2>/dev/null; then
                # CHERRY_PICK_HEAD exists while a cherry-pick is paused
                if git rev-parse CHERRY_PICK_HEAD >/dev/null 2>&1; then
                    if git diff --cached --quiet && git diff --quiet; then
                        # Nothing staged and nothing modified = commit already applied.
                        # Use --abort (not --skip) for compatibility with Git < 2.32.
                        # Since we pick one commit at a time, abort just clears the
                        # cherry-pick state and leaves HEAD unchanged, which is correct.
                        git cherry-pick --abort 2>/dev/null || true
                    else
                        # Real conflict. Two cases:
                        #   1. The ONLY conflicted file is build_all/tests.yaml.
                        #      This is expected when a sample cherry-picks several
                        #      plugins that each append their own test block to the
                        #      same file. We resolve it deterministically as the union
                        #      of the blocks already on HEAD ("ours") and the incoming
                        #      commit's blocks ("theirs"), keeping only what actually
                        #      exists at this point in the sequence (see
                        #      resolve_build_test_conflict). No human interaction
                        #      required.
                        #   2. Anything else: pause and let the operator resolve it by
                        #      hand, then continue the same run.
                        local subject conflicted
                        subject="$(git log -1 --pretty=format:%s "${commit}")"
                        conflicted="$(git diff --name-only --diff-filter=U)"
                        if [ "${conflicted}" = "${BUILD_ALL_TESTCASE}" ]; then
                            log_info "  Auto-resolving ${BUILD_ALL_TESTCASE} conflict on ${commit} (${subject})"
                            resolve_build_test_conflict
                            # Force-add: the build_all directory is matched by a
                            # .gitignore pattern, so a plain 'git add' of the freshly
                            # written file is refused and returns non-zero, which (under
                            # 'set -e') would abort the whole script right here and leave
                            # the operator to run 'git cherry-pick --continue' by hand.
                            # '-f' stages the resolved file regardless of the ignore rule.
                            git add -f "${BUILD_ALL_TESTCASE}"

                            # The resolved index may be identical to HEAD (the incoming
                            # commit adds no new block, e.g. it was already present). In
                            # that case the cherry-pick would be empty: skip it. Compare
                            # the tree, not the staged diff, since after resolution the
                            # index is fully merged.
                            if git diff --cached --quiet HEAD; then
                                log_warn "  Resolution of ${commit} is empty; skipping commit."
                                git cherry-pick --abort 2>/dev/null || true
                            elif ! GIT_EDITOR=true git -c core.hooksPath=/dev/null \
                                    cherry-pick --continue --no-edit >/dev/null 2>&1; then
                                log_error "Cherry-pick --continue failed on ${commit}"
                                git status --short | sed 's/^/    /'
                                git cherry-pick --abort 2>/dev/null || true
                                return 1
                            else
                                log_ok "  Auto-resolved and continued past ${commit}"
                            fi
                        elif pause_for_manual_resolution "${commit}" "${subject}"; then


                            # The operator may have run 'git cherry-pick --abort'
                            # inside the sub-shell to skip this commit; in that
                            # case there is no cherry-pick in progress anymore,
                            # so there is nothing to continue.
                            if ! git rev-parse CHERRY_PICK_HEAD >/dev/null 2>&1; then
                                log_warn "  Cherry-pick of ${commit} was aborted/skipped; continuing."
                            elif git diff --cached --quiet; then
                                # Conflicts resolved but nothing staged to commit
                                # (resolution was empty, e.g. incoming change is
                                # already present). Skip this commit.
                                log_warn "  Resolution of ${commit} is empty; skipping commit."
                                git cherry-pick --abort 2>/dev/null || true
                            elif ! GIT_EDITOR=true git -c core.hooksPath=/dev/null \
                                    cherry-pick --continue --no-edit >/dev/null 2>&1; then
                                log_error "Cherry-pick --continue failed on ${commit}"
                                git cherry-pick --abort 2>/dev/null || true
                                return 1
                            fi
                        else
                            log_error "Cherry-pick conflict on commit ${commit}"
                            git cherry-pick --abort 2>/dev/null || true
                            return 1
                        fi

                    fi
                else
                    log_error "Cherry-pick failed on commit ${commit}"
                    return 1
                fi
            fi
        done
    }


    # Cherry-pick dependency commits (all commits from each dependency branch)
    if [ -n "${deps}" ]; then
        for dep in ${deps}; do
            log_info "  Cherry-picking all commits from ${dep}..."
            cherry_pick_range "${dep}"
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

    # Append this plugin's own build_all entry. The core-tests commit (cherry-
    # picked as a dependency) provides build_all/tests.yaml with only the
    # core entry; each plugin adds exactly its own build test here. Targets
    # without a build test (e.g. samples) are left untouched.
    local build_test="${TARGET_BUILD_TEST[${target}]:-}"
    if [ -n "${build_test}" ] && [ -f "${BUILD_ALL_TESTCASE}" ]; then
        log_info "  Adding build test '${build_test}' to ${BUILD_ALL_TESTCASE}"
        append_build_test_block "${build_test}"
    fi

    git add -A


    # Check if there are changes to commit
    if git diff --cached --quiet; then
        log_warn "  No changes to commit for target '${target}'. Skipping."
        git checkout "${current_branch}" --quiet
        return 1
    fi

    # Commit with the proper message and explicit author (--no-verify to
    # skip git hooks). The author is looked up from TARGET_AUTHOR so it does
    # not depend on the git config of whoever runs the script.
    local author="${TARGET_AUTHOR[${target}]}"
    git commit --no-verify --author="${author}" -m "${commit_msg}" --quiet


    log_ok "  Branch '${branch}' created successfully"
    log_info "  Commit: $(git --no-pager log --oneline -1)"

    # Show stats
    git --no-pager diff --stat HEAD~1 HEAD | tail -3

    # Return to original branch
    git checkout "${current_branch}" --quiet
    echo ""
}

# Run compliance check on a branch.
# Aligned with Zephyr CI workflow:
#   - Excludes KconfigBasic, SysbuildKconfigBasic, ClangFormat
#   - Uses --annotate for detailed output
#   - Increases diff.renameLimit for large PRs
#
# Args: $1=branch_name, $2=commit_range (optional, default: HEAD~1..)
check_compliance() {
    local branch="$1"
    local range="${2:-HEAD~1..}"

    log_info "Running compliance check on '${branch}' (${range})..."

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

    # Match Zephyr CI: increase rename limit for large PRs
    git config diff.renameLimit 10000

    # Match Zephyr CI: exclude KconfigBasic, SysbuildKconfigBasic, ClangFormat
    local excludes="-e KconfigBasic -e SysbuildKconfigBasic -e ClangFormat -e Ruff"

    # Check only the specified commit range
    local result=0
    python3 "${compliance_script}" --annotate ${excludes} -c "${range}" 2>&1 | \
        sed 's/^/  /' || result=$?

    git checkout "${current_branch}" --quiet

    if [ ${result} -ne 0 ]; then
        log_error "  Compliance check FAILED for '${branch}' (${range})"
        log_error "  Fix the issues in '${SOURCE_BRANCH}', then re-run this script."
        return 1
    fi

    log_ok "  Compliance check passed for '${branch}' (${range})"
    return 0
}

# Run doxygen coverage delta check on a branch.
# Adapted from Zephyr CI workflow: .github/workflows/doxygen-coverage-delta.yml
# Ensures that any newly introduced public API symbols are properly documented.
#
# The check generates doxygen coverage JSON for both the target branch and the
# base branch, then uses scripts/ci/doxygen_coverage_diff.py to compare them.
#
# Args: $1=branch_name
check_doxygen_coverage() {
    local branch="$1"

    log_info "Running doxygen coverage delta check on '${branch}'..."

    if ${DRY_RUN}; then
        log_info "  [DRY RUN] Would run doxygen coverage delta check"
        return 0
    fi

    local current_branch
    current_branch="$(git branch --show-current)"

    # Check required tools
    local diff_script="scripts/ci/doxygen_coverage_diff.py"
    if [ ! -f "${diff_script}" ]; then
        log_warn "  Doxygen coverage diff script not found at ${diff_script}. Skipping."
        return 0
    fi

    if ! command -v doxygen >/dev/null 2>&1; then
        log_warn "  'doxygen' not found in PATH. Skipping coverage check."
        return 0
    fi

    local pr_root
    pr_root="$(pwd)"

    # --- Generate PR branch coverage JSON ---
    git checkout "${branch}" --quiet

    log_info "  Generating PR branch coverage JSON..."
    make -C doc doxygen-coverage-json 2>&1 | tail -3 | sed 's/^/  /' || true

    if [ ! -f "doc/_build/doc-coverage.json" ]; then
        log_warn "  Failed to generate PR coverage JSON. Skipping."
        git checkout "${current_branch}" --quiet
        return 0
    fi
    cp doc/_build/doc-coverage.json /tmp/mp-pr-coverage.json

    # --- Generate Base branch coverage JSON ---
    log_info "  Generating base (${BASE_REF}) coverage JSON..."

    local base_worktree="/tmp/mp-base-worktree-$"
    git worktree add --detach "${base_worktree}" "${BASE_REF}" --quiet 2>/dev/null || true

    if [ -d "${base_worktree}" ]; then
        # Use PR branch doc build config for consistency (as CI does)
        cp doc/CMakeLists.txt doc/Makefile "${base_worktree}/doc/" 2>/dev/null || true
        make -C "${base_worktree}/doc" doxygen-coverage-json 2>&1 | tail -3 | sed 's/^/  /' || true

        if [ -f "${base_worktree}/doc/_build/doc-coverage.json" ]; then
            cp "${base_worktree}/doc/_build/doc-coverage.json" /tmp/mp-base-coverage.json
        fi

        # Cleanup worktree
        git worktree remove "${base_worktree}" --force 2>/dev/null || rm -rf "${base_worktree}"
    fi

    git checkout "${current_branch}" --quiet

    if [ ! -f "/tmp/mp-base-coverage.json" ]; then
        log_warn "  Failed to generate base coverage JSON. Skipping."
        rm -f /tmp/mp-pr-coverage.json
        return 0
    fi

    # --- Compare coverage ---
    log_info "  Checking for undocumented new API..."
    local result=0
    python3 "${diff_script}" \
        --reference /tmp/mp-base-coverage.json \
        --comparison /tmp/mp-pr-coverage.json \
        --strip-reference-prefix "${base_worktree}" \
        --strip-comparison-prefix "${pr_root}" 2>&1 | sed 's/^/  /' || result=$?

    # Cleanup temp files
    rm -f /tmp/mp-pr-coverage.json /tmp/mp-base-coverage.json

    if [ ${result} -ne 0 ]; then
        log_error "  Doxygen coverage delta check FAILED for '${branch}'"
        log_error "  New API symbols are missing documentation."
        log_error "  Fix by adding /** @brief ... */ comments to undocumented symbols."
        return 1
    fi

    log_ok "  Doxygen coverage delta check passed for '${branch}'"
    return 0
}

# ===========================================================================
# Target dispatch
# ===========================================================================

# Export the core framework commit onto upstream/mp-core (commit 1 of 2).
export_core() {
    generate_branch "core" "${UPSTREAM_PREFIX}-core" \
        "${CORE_COMMIT_MSG}" "${CORE_PATHS[@]}"
}

# Append the core tests commit onto upstream/mp-core (commit 2 of 2).
# Depends on upstream/mp-core (cherry-picks it), then adds test files on top.
export_core_tests() {
    local branch="${UPSTREAM_PREFIX}-core"

    log_info "Appending core-tests commit to branch: ${branch} (source: ${SOURCE_BRANCH})"
    log_info "  Paths: ${CORE_TEST_PATHS[*]}"

    if ${DRY_RUN}; then
        log_info "  [DRY RUN] Would append core-tests commit to '${branch}' from '${SOURCE_BRANCH}'"
        echo ""
        return 0
    fi

    local current_branch
    current_branch="$(git branch --show-current)"
    git checkout "${branch}" --quiet

    local has_files=false
    for path in "${CORE_TEST_PATHS[@]}"; do
        if git ls-tree -r "${SOURCE_BRANCH}" -- "${path}" 2>/dev/null | grep -q .; then
            git checkout "${SOURCE_BRANCH}" -- "${path}"
            has_files=true
        fi
    done

    if ! ${has_files}; then
        log_warn "  No test files found in '${SOURCE_BRANCH}'. Skipping tests commit."
        git checkout "${current_branch}" --quiet
        return 0
    fi

    # The shared build_all/tests.yaml contains one entry per plugin. The
    # core-tests commit must only carry the core build test; each plugin's
    # entry is added by its own plugin commit.
    if [ -f "${BUILD_ALL_TESTCASE}" ]; then
        log_info "  Reducing ${BUILD_ALL_TESTCASE} to '${TARGET_BUILD_TEST[core]}' only"
        write_build_test_file "${TARGET_BUILD_TEST[core]}"
    fi

    # The core build must exercise CONFIG_MP_DUMP (a core option gating the
    # element name); guarantee it in the shared prj.conf, idempotently.
    if [ -f "tests/subsys/mp/build_all/prj.conf" ]; then
        grep -q '^CONFIG_MP_DUMP=y$' tests/subsys/mp/build_all/prj.conf ||
            echo 'CONFIG_MP_DUMP=y' >> tests/subsys/mp/build_all/prj.conf
    fi

    git add -A


    if git diff --cached --quiet; then
        log_warn "  No test changes to commit. Skipping tests commit."
        git checkout "${current_branch}" --quiet
        return 0
    fi

    # core-tests commit is authored by Phi Bang (see Signed-off-by trailers
    # in CORE_TEST_COMMIT_MSG for the full contributor list).
    git commit --no-verify --author="${AUTHOR_PHIBANG}" \
        -m "${CORE_TEST_COMMIT_MSG}" --quiet


    log_ok "  core-tests commit appended to '${branch}' successfully"
    log_info "  Commit: $(git --no-pager log --oneline -1)"
    git --no-pager diff --stat HEAD~1 HEAD | tail -3

    git checkout "${current_branch}" --quiet
    echo ""
}

export_vid() {
    generate_branch "vid" "${UPSTREAM_PREFIX}-vid" \
        "${VID_COMMIT_MSG}" "${VID_PATHS[@]}"
}

export_img() {
    generate_branch "img" "${UPSTREAM_PREFIX}-img" \
        "${IMG_COMMIT_MSG}" "${IMG_PATHS[@]}"
}

export_aud() {
    generate_branch "aud" "${UPSTREAM_PREFIX}-aud" \
        "${AUD_COMMIT_MSG}" "${AUD_PATHS[@]}"
}

export_disp() {
    generate_branch "disp" "${UPSTREAM_PREFIX}-disp" \
        "${DISP_COMMIT_MSG}" "${DISP_PATHS[@]}"
}

export_fs() {
    generate_branch "fs" "${UPSTREAM_PREFIX}-fs" \
        "${FS_COMMIT_MSG}" "${FS_PATHS[@]}"
}

export_base() {
    generate_branch "base" "${UPSTREAM_PREFIX}-base" \
        "${BASE_COMMIT_MSG}" "${BASE_PATHS[@]}"
}

export_utils() {
    generate_branch "utils" "${UPSTREAM_PREFIX}-utils" \
        "${UTILS_COMMIT_MSG}" "${UTILS_PATHS[@]}"
}

export_sample_cam_disp() {
    generate_branch "sample-cam_disp" "${UPSTREAM_PREFIX}-sample-cam_disp" \
        "${SAMPLE_CAM_DISP_COMMIT_MSG}" "${SAMPLE_CAM_DISP_PATHS[@]}"
}

export_sample_jpeg_dec() {
    generate_branch "sample-jpeg_dec" "${UPSTREAM_PREFIX}-sample-jpeg_dec" \
        "${SAMPLE_JPEG_DEC_COMMIT_MSG}" "${SAMPLE_JPEG_DEC_PATHS[@]}"
}

export_sample_tee_dec() {
    generate_branch "sample-tee_dec" "${UPSTREAM_PREFIX}-sample-tee_dec" \
        "${SAMPLE_TEE_DEC_COMMIT_MSG}" "${SAMPLE_TEE_DEC_PATHS[@]}"
}

export_sample_fs() {
    generate_branch "sample-fs" "${UPSTREAM_PREFIX}-sample-fs" \
        "${SAMPLE_FS_COMMIT_MSG}" "${SAMPLE_FS_PATHS[@]}"
}

export_sample_dmic_i2s() {
    generate_branch "sample-dmic_i2s" "${UPSTREAM_PREFIX}-sample-dmic_i2s" \
        "${SAMPLE_DMIC_I2S_COMMIT_MSG}" "${SAMPLE_DMIC_I2S_PATHS[@]}"
}

# ===========================================================================
# Export all targets
# ===========================================================================

export_all() {
    TARGETS=(core vid img aud disp fs base utils \
        sample-cam_disp sample-jpeg_dec sample-tee_dec \
        sample-fs sample-dmic_i2s)

    log_info "=== Exporting all MP upstream PR branches ==="
    log_info "Source: ${SOURCE_BRANCH}"
    log_info "Base:   ${BASE_REF}"
    log_info "Date:   ${TODAY}"
    echo ""

    # Core must be first (plugins depend on it).
    # Two commits on upstream/mp-core: framework first, then tests.
    export_core
    export_core_tests

    # Plugins (independent of each other, all depend on core)
    export_vid
    export_img
    export_aud
    export_disp
    export_fs
    export_base

    # Utils (depend on core; exported before samples that cherry-pick it)
    export_utils

    # Samples (depend on core + relevant plugin + utils)
    export_sample_cam_disp
    export_sample_jpeg_dec
    export_sample_tee_dec
    export_sample_fs
    export_sample_dmic_i2s

    if ! ${SKIP_COMPLIANCE}; then
        echo ""
        log_info "=== Running compliance checks ==="
        echo ""

        local failed_targets=()
        for target in "${TARGETS[@]}"; do
            local branch="${UPSTREAM_PREFIX}-${target}"
            if [ "${target}" = "core" ]; then
                # core branch has two commits: [framework] [tests]
                # Check each commit individually
                if ! check_compliance "${branch}" "HEAD~2..HEAD~1"; then
                    failed_targets+=(core)
                fi
                if ! check_compliance "${branch}" "HEAD~1.."; then
                    failed_targets+=(core-tests)
                fi
            else
                if ! check_compliance "${branch}"; then
                    failed_targets+=("${target}")
                fi
            fi
        done

        echo ""
        log_info "=== Running doxygen coverage delta checks ==="
        echo ""

        local failed_doxygen=()
        for target in "${TARGETS[@]}"; do
            local branch="${UPSTREAM_PREFIX}-${target}"
            if ! check_doxygen_coverage "${branch}"; then
                failed_doxygen+=("${target}")
            fi
        done

        echo ""
        if [ ${#failed_targets[@]} -eq 0 ] && [ ${#failed_doxygen[@]} -eq 0 ]; then
            log_info "=== All checks passed ==="
        else
            if [ ${#failed_targets[@]} -gt 0 ]; then
                log_warn "=== Compliance check summary ==="
                log_warn "FAILED targets: ${failed_targets[*]}"
            fi
            if [ ${#failed_doxygen[@]} -gt 0 ]; then
                log_warn "=== Doxygen coverage summary ==="
                log_warn "FAILED targets: ${failed_doxygen[*]}"
            fi
            log_warn ""
            log_warn "To fix:"
            log_warn "  1. Fix issues in '${SOURCE_BRANCH}' and commit"
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
        if [ "${target}" = "core" ]; then
            while IFS= read -r line; do
                echo "  ${branch}: ${line}"
            done < <(git --no-pager log --oneline "${BASE_REF}..${branch}" 2>/dev/null) \
                || echo "  ${branch}: N/A"
        else
            echo "  ${branch}: $(git --no-pager log --oneline -1 "${branch}" 2>/dev/null || echo 'N/A')"
        fi
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
verify the target's own commit and tests (HEAD~2..HEAD).

Targets:
  core             Core MP framework + tests
  vid             Video plugin (depends on core)
  img             Image codec plugin (depends on core)
  aud             Audio plugin (depends on core)
  disp            Display plugin (depends on core)
  fs              Filesystem plugin (depends on core)
  base            Base plugin (depends on core)
  utils            Utils helper library (depends on core)
  sample-cam_disp  Camera-to-display sample (depends on core, base, vid, disp, utils)
  sample-jpeg_dec  JPEG decoding sample (depends on core, base, vid, img, disp, fs, utils)
  sample-tee_dec   Multi-branch jpeg decoding sample
                   (depends on core, base, vid, img, disp, fs, utils)
  sample-fs        Filesystem sample (depends on core, fs)
  sample-dmic_i2s  DMIC to I2S sample (depends on core, base, aud)
  all              All of the above (default)

Options:
  --dry-run     Show what would be done without making changes
  --list        List available targets
  --no-check   Skip compliance checks
  --help        Show this help

Examples:
  $(basename "$0")                      # Export all
  $(basename "$0") core                 # Export core only
  $(basename "$0") core vid            # Export core then vid
  $(basename "$0") sample-cam_disp      # Export sample (core+vid must exist)
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
                echo "Available targets:"
                echo "  core vid img aud disp fs base utils"
                echo "  sample-cam_disp sample-jpeg_dec"
                echo "  sample-tee_dec sample-fs sample-dmic_i2s"
                exit 0
                ;;
            --no-check)
                SKIP_COMPLIANCE=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            core|vid|img|aud|disp|fs|base|utils|\
            sample-cam_disp|sample-jpeg_dec|\
            sample-tee_dec|sample-fs|sample-dmic_i2s|all)
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
                export_core_tests
                ;;
            vid)
                TARGETS+=(vid)
                export_vid
                ;;
            img)
                TARGETS+=(img)
                export_img
                ;;
            aud)
                TARGETS+=(aud)
                export_aud
                ;;
            disp)
                TARGETS+=(disp)
                export_disp
                ;;
            fs)
                TARGETS+=(fs)
                export_fs
                ;;
            base)
                TARGETS+=(base)
                export_base
                ;;
            utils)
                TARGETS+=(utils)
                export_utils
                ;;
            sample-cam_disp)
                TARGETS+=(sample-cam_disp)
                export_sample_cam_disp
                ;;
            sample-jpeg_dec)
                TARGETS+=(sample-jpeg_dec)
                export_sample_jpeg_dec
                ;;
            sample-tee_dec)
                TARGETS+=(sample-tee_dec)
                export_sample_tee_dec
                ;;
            sample-fs)
                TARGETS+=(sample-fs)
                export_sample_fs
                ;;
            sample-dmic_i2s)
                TARGETS+=(sample-dmic_i2s)
                export_sample_dmic_i2s
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
            if [ "${target}" = "core" ]; then
                # core branch has two commits; check each individually
                if ! check_compliance "${branch}" "HEAD~2..HEAD~1"; then
                    failed_targets_single+=(core)
                fi
                if ! check_compliance "${branch}" "HEAD~1.."; then
                    failed_targets_single+=(core-tests)
                fi
            else
                if ! check_compliance "${branch}"; then
                    failed_targets_single+=("${target}")
                fi
            fi
        done

        echo ""
        log_info "=== Running doxygen coverage delta checks ==="
        echo ""

        local failed_doxygen_single=()
        for target in "${TARGETS[@]}"; do
            local branch="${UPSTREAM_PREFIX}-${target}"
            if ! check_doxygen_coverage "${branch}"; then
                failed_doxygen_single+=("${target}")
            fi
        done

        echo ""
        if [ ${#failed_targets_single[@]} -eq 0 ] && [ ${#failed_doxygen_single[@]} -eq 0 ]; then
            log_info "=== All checks passed ==="
        else
            if [ ${#failed_targets_single[@]} -gt 0 ]; then
                log_warn "=== Compliance check summary ==="
                log_warn "FAILED targets: ${failed_targets_single[*]}"
            fi
            if [ ${#failed_doxygen_single[@]} -gt 0 ]; then
                log_warn "=== Doxygen coverage summary ==="
                log_warn "FAILED targets: ${failed_doxygen_single[*]}"
            fi
            log_warn ""
            log_warn "To fix:"
            log_warn "  1. Fix issues in '${SOURCE_BRANCH}' and commit"
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
        if [ "${target}" = "core" ]; then
            while IFS= read -r line; do
                echo "  ${branch}: ${line}"
            done < <(git --no-pager log --oneline "${BASE_REF}..${branch}" 2>/dev/null) \
                || echo "  ${branch}: N/A"
        else
            echo "  ${branch}: $(git --no-pager log --oneline -1 "${branch}" 2>/dev/null || echo 'N/A')"
        fi
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
