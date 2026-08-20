#!/usr/bin/env bash
#
# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0
#
# Helper to build and run the screen_mirror sample on native_sim.
#
# The sample needs two things on the host that a plain "west build -t run"
# does not set up by itself:
#
#   1. A virtual Ethernet interface (zeth) so the Zephyr networking stack has
#      something to bind to. Without it you get:
#          <err> eth_tap: Cannot create zeth (-1/Operation not permitted)
#      This is created by the net-setup.sh script that ships with the Zephyr
#      "net-tools" project and requires root (it uses "ip tuntap"/"ip addr").
#
#   2. The native_sim executable itself. It is built with native_sim.conf, which
#      puts the Zephyr shell on this terminal's stdin/stdout, so the interactive
#      player prompt (p/s/r/q) appears here; it then listens on the TCP port for
#      a client to connect.
#
# Usage:
#   ./run_native_sim.sh                 build (if needed) + set up zeth + run
#   ./run_native_sim.sh --no-build      skip the build step
#   ./run_native_sim.sh --no-net        skip the zeth setup (already up)
#   ./run_native_sim.sh --pristine      force a clean (pristine) build
#
# Debugging a crash:
#   ./run_native_sim.sh --gdb           run under gdb, stop at the faulting line
#   ./run_native_sim.sh --asan          build with the address sanitizer and run
#   ./run_native_sim.sh --core          run bare, then open the core dump in gdb
#
#   --gdb leaves you at a prompt on SIGSEGV with the backtrace already printed,
#   so "bt full", "frame N", "print var" and "info threads" all work. It is the
#   one to reach for when the fault address means nothing on its own.
#
#   --asan is the one that finds the cause rather than the symptom: a buffer
#   overrun or a use-after-free is reported where it happens, with the
#   allocation and free stacks, instead of crashing later somewhere unrelated.
#   Start here for a segfault whose backtrace looks impossible. It implies a
#   pristine build, since the sanitizer changes every object file.
#
#   --core is for a fault that will not reproduce under a debugger.
#
#   A debug build (-O0 -g, no inlining) is used for --gdb and --core so the
#   backtrace has real line numbers and arguments rather than <optimized out>;
#   pass --opt to keep the normal optimisation level.
#
# Environment overrides:
#   ZEPHYR_BASE     path to the zephyr tree (default: autodetected)
#   NET_TOOLS_DIR   path to the net-tools checkout that holds net-setup.sh
#   BUILD_DIR       build output directory (default: <sample>/build)
#   ZETH_ADDR       host-side IPv4 for zeth (default: 192.0.2.2)
#   GDB             debugger to use (default: gdb)
#   ASAN_OPTIONS    passed through to the sanitizer runtime
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLE_DIR="${SCRIPT_DIR}"

# Zephyr base: walk up from the sample (…/zephyr/samples/subsys/mpipe/screen_mirror)
ZEPHYR_BASE="${ZEPHYR_BASE:-$(cd "${SAMPLE_DIR}/../../../.." && pwd)}"

BUILD_DIR="${BUILD_DIR:-${SAMPLE_DIR}/build}"
# Use the 64-bit native_sim variant: the sample pulls in the SDL display glue,
# and host SDL2 packages are 64-bit only. The default (32-bit) native_sim fails
# to link with "skipping incompatible libSDL2 ... cannot find -lSDL2".
BOARD="${BOARD:-native_sim/native/64}"

ZEPHYR_EXE="${BUILD_DIR}/zephyr/zephyr.exe"

# net-tools may live next to the workspace root (…/zephyrproject/tools/net-tools)
# or be provided explicitly through NET_TOOLS_DIR.
find_net_setup() {
	local candidates=(
		"${NET_TOOLS_DIR:-}"
		"${ZEPHYR_BASE}/../tools/net-tools"
		"${ZEPHYR_BASE}/../net-tools"
		"${HOME}/net-tools"
		"${HOME}/zephyrproject/tools/net-tools"
	)
	local d
	for d in "${candidates[@]}"; do
		if [ -n "${d}" ] && [ -x "${d}/net-setup.sh" ]; then
			echo "${d}/net-setup.sh"
			return 0
		fi
	done
	return 1
}

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
DO_BUILD=1
DO_NET=1
PRISTINE=0
MODE=run
DEBUG_BUILD=auto
ZETH_ADDR="${ZETH_ADDR:-192.0.2.2}"
GDB="${GDB:-gdb}"

for arg in "$@"; do
	case "${arg}" in
	--no-build) DO_BUILD=0 ;;
	--no-net)   DO_NET=0 ;;
	--pristine) PRISTINE=1 ;;
	--gdb)      MODE=gdb ;;
	--core)     MODE=core ;;
	--asan)     MODE=asan ;;
	--opt)      DEBUG_BUILD=0 ;;
	--debug)    DEBUG_BUILD=1 ;;
	-h | --help)
		sed -n '2,62p' "${BASH_SOURCE[0]}"
		exit 0
		;;
	*)
		echo "Unknown option: ${arg}" >&2
		exit 1
		;;
	esac
done

# A crash is only readable with symbols and without inlining, so the debugging
# modes default to a debug build. --opt overrides it.
if [ "${DEBUG_BUILD}" = "auto" ]; then
	case "${MODE}" in
	gdb | core) DEBUG_BUILD=1 ;;
	*)          DEBUG_BUILD=0 ;;
	esac
fi

# Always build with the native_sim overlay so the Zephyr shell lands on this
# terminal's stdin/stdout (interactive player prompt) rather than a pseudo-tty.
EXTRA_CONF=(-DEXTRA_CONF_FILE=native_sim.conf)
if [ "${DEBUG_BUILD}" -eq 1 ]; then
	EXTRA_CONF+=(-DCONFIG_DEBUG_OPTIMIZATIONS=y -DCONFIG_DEBUG_THREAD_INFO=y)
fi

if [ "${MODE}" = "asan" ]; then
	# Every object file changes, so a sanitizer build cannot reuse the
	# ordinary one. Keep it in its own directory to avoid rebuilding the
	# world on every switch, and force a pristine configure the first time.
	EXTRA_CONF+=(-DCONFIG_ASAN=y -DCONFIG_UBSAN=y)
	BUILD_DIR="${BUILD_DIR:-${SAMPLE_DIR}/build}-asan"
	ZEPHYR_EXE="${BUILD_DIR}/zephyr/zephyr.exe"
	[ -d "${BUILD_DIR}" ] || PRISTINE=1
fi

# ---------------------------------------------------------------------------
# SDL2 multiarch include workaround
# ---------------------------------------------------------------------------
# On Debian/Ubuntu the top-level /usr/include/SDL2/SDL_config.h just does
#     #include <SDL2/_real_SDL_config.h>
# and the real header only exists under the multiarch dir
# (e.g. /usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h). pkg-config
# only reports -I/usr/include/SDL2, so the native_sim SDL display glue fails
# with "SDL2/_real_SDL_config.h: No such file or directory". Add the multiarch
# include root to C_INCLUDE_PATH so the redirect resolves.
add_sdl_multiarch_include() {
	local real
	real="$(find /usr/include -name '_real_SDL_config.h' 2>/dev/null | head -n1)"
	if [ -n "${real}" ]; then
		# strip the trailing SDL2/_real_SDL_config.h to get the include root
		local root
		root="$(dirname "$(dirname "${real}")")"
		case ":${C_INCLUDE_PATH:-}:" in
		*":${root}:"*) ;;
		*)
			export C_INCLUDE_PATH="${root}${C_INCLUDE_PATH:+:${C_INCLUDE_PATH}}"
			echo ">> Added ${root} to C_INCLUDE_PATH (SDL2 multiarch workaround)"
			;;
		esac
	fi
}
add_sdl_multiarch_include

# ---------------------------------------------------------------------------
# 1. Build
# ---------------------------------------------------------------------------
if [ "${DO_BUILD}" -eq 1 ]; then
	echo ">> Building ${SAMPLE_DIR} for ${BOARD}"

	if [ "${#EXTRA_CONF[@]}" -gt 0 ]; then
		echo "   extra config: ${EXTRA_CONF[*]}"
	fi

	if [ "${PRISTINE}" -eq 1 ]; then
		west build -p always -b "${BOARD}" -d "${BUILD_DIR}" "${SAMPLE_DIR}" \
			${EXTRA_CONF[@]+-- "${EXTRA_CONF[@]}"}
	else
		west build -b "${BOARD}" -d "${BUILD_DIR}" "${SAMPLE_DIR}" \
			${EXTRA_CONF[@]+-- "${EXTRA_CONF[@]}"}
	fi
fi

if [ ! -x "${ZEPHYR_EXE}" ]; then
	echo "!! ${ZEPHYR_EXE} not found; build first (drop --no-build)." >&2
	exit 1
fi

# ---------------------------------------------------------------------------
# 2. Virtual Ethernet (zeth)
# ---------------------------------------------------------------------------
NET_SETUP=""
if [ "${DO_NET}" -eq 1 ]; then
	if NET_SETUP="$(find_net_setup)"; then
		echo ">> Setting up zeth via ${NET_SETUP} (needs sudo)"
		sudo "${NET_SETUP}" start
		# Give the host side an address on the same subnet as the target
		# (192.0.2.1/24 is configured inside the sample).
		sudo ip addr add "${ZETH_ADDR}/24" dev zeth 2>/dev/null || true
		sudo ip link set dev zeth up
	else
		cat >&2 <<-EOF
		!! Could not find net-setup.sh.
		   Clone the Zephyr net-tools project and either place it at
		   ${ZEPHYR_BASE}/../tools/net-tools or point NET_TOOLS_DIR at it:

		       git clone https://github.com/zephyrproject-rtos/net-tools
		       NET_TOOLS_DIR=\$PWD/net-tools ${BASH_SOURCE[0]}

		   Continuing without zeth; the sample will report
		   "Cannot create zeth" and TCP will not work.
		EOF
	fi
fi

cleanup() {
	if [ "${DO_NET}" -eq 1 ] && [ -n "${NET_SETUP}" ]; then
		echo ""
		echo ">> Tearing down zeth"
		sudo "${NET_SETUP}" stop || true
	fi
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# 3. Run
# ---------------------------------------------------------------------------
echo ">> Launching ${ZEPHYR_EXE}"
echo "   (Zephyr shell is on this terminal: use p/s/r/q or 'player status';"
echo "    connect a client to 192.0.2.1 on the sample's TCP port.)"
echo "   Press Ctrl-C to stop."
echo ""

case "${MODE}" in
run)
	"${ZEPHYR_EXE}"
	;;

gdb)
	# native_sim is a plain host binary, so it debugs like one. Two things
	# have to be said explicitly:
	#
	#   - the sample runs its pipeline on Zephyr threads, which are host
	#     threads here, so the fault often surfaces on a thread other than
	#     the one that set it up: "thread apply all bt" is what shows that.
	#   - SIGUSR1/SIGUSR2 and SIGALRM belong to the simulator's own timer and
	#     thread switching, so gdb must pass them through untouched or the
	#     run dies long before reaching the bug.
	if ! command -v "${GDB}" >/dev/null; then
		echo "!! ${GDB} not found; install gdb or set GDB=..." >&2
		exit 1
	fi

	echo ">> Under ${GDB}: it will stop at the fault with a backtrace."
	echo "   Useful there: bt full / thread apply all bt / frame N / print x"
	echo ""

	# Not exec: the EXIT trap has to survive this so zeth is torn down when
	# the debugger quits. Leaving it configured makes the next run fail in
	# net-setup.sh with "Device or resource busy".
	"${GDB}" -q \
		-ex "handle SIGUSR1 nostop noprint pass" \
		-ex "handle SIGUSR2 nostop noprint pass" \
		-ex "handle SIGALRM nostop noprint pass" \
		-ex "set confirm off" \
		-ex "set pagination off" \
		-ex "set print pretty on" \
		-ex "run" \
		-ex "echo \n>> Stopped. Backtrace of the faulting thread:\n" \
		-ex "bt full" \
		-ex "echo \n>> All threads:\n" \
		-ex "thread apply all bt" \
		--args "${ZEPHYR_EXE}"
	;;

asan)
	# halt_on_error=0 matches CONFIG_ASAN_RECOVER=y, so a first report does
	# not hide the rest. detect_leaks is off: the sample is killed with
	# Ctrl-C and would otherwise drown the real finding in shutdown leaks.
	export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=0:abort_on_error=0:detect_leaks=0:print_stacktrace=1:strict_string_checks=1:detect_stack_use_after_return=1}"
	export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}"

	echo ">> Sanitizer run. ASAN_OPTIONS=${ASAN_OPTIONS}"
	echo "   A report names the faulting access, then the allocation and"
	echo "   free stacks of the object involved - read those, not the crash."
	echo ""

	"${ZEPHYR_EXE}"
	;;

core)
	# The host may be routing cores to a handler (systemd-coredump, apport),
	# in which case no file lands here; say so rather than looking clean.
	ulimit -c unlimited
	CORE_PATTERN="$(cat /proc/sys/kernel/core_pattern 2>/dev/null || echo '?')"

	cd "${BUILD_DIR}"
	rm -f core core.*
	set +e
	"${ZEPHYR_EXE}"
	RC=$?
	set -e

	echo ""
	echo ">> Exited with ${RC}"

	CORE="$(ls -1t core core.* 2>/dev/null | head -n1 || true)"
	if [ -n "${CORE}" ]; then
		echo ">> Opening ${BUILD_DIR}/${CORE}"
		"${GDB}" -q \
			-ex "set pagination off" \
			-ex "bt full" \
			-ex "echo \n>> All threads:\n" \
			-ex "thread apply all bt" \
			"${ZEPHYR_EXE}" "${CORE}"
		exit 0
	fi

	echo "!! No core file in ${BUILD_DIR}."
	echo "   kernel.core_pattern is: ${CORE_PATTERN}"
	case "${CORE_PATTERN}" in
	\|*)
		echo "   It starts with '|', so a handler took the core. Retrieve it with"
		echo "       coredumpctl gdb $(basename "${ZEPHYR_EXE}")"
		echo "   or use --gdb instead, which needs no core at all."
		;;
	*)
		echo "   The run may simply not have faulted."
		;;
	esac
	;;
esac
