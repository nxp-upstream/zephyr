#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

source "${ZEPHYR_BASE}/tests/bluetooth/classic/sim/common/common_utils.sh"

run_native_sim_test "bluetooth.classic.sim.l2cap.streaming"
