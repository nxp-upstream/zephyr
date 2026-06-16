#!/usr/bin/env bash
# Copyright 2025 NXP
# SPDX-License-Identifier: Apache-2.0

# Bumble controller utilities
# Thread-safe: No global variables, all state passed via parameters

# Prevent multiple sourcing
if [[ -n "${BUMBLE_UTILS_LOADED:-}" ]]; then
    return 0
fi
readonly BUMBLE_UTILS_LOADED=1

# Ensure Bumble Python package is installed at the required version.
#
# This is needed by controllers.py (and related test scripts) which rely on Bumble.
#
# Behavior:
# - If Bumble is not installed: install Bumble==0.0.220
# - If Bumble is installed but version differs: uninstall, then install Bumble==0.0.220
#
# Args: [python_exe] (optional, default: python)
# Returns: 0 on success, 1 on failure
ensure_bumble_version() {
    local py="${1:-python}"
    local required_version="0.0.220"

    if ! command -v "$py" >/dev/null 2>&1; then
        echo "Error: '$py' not found in PATH; cannot verify/install Bumble" >&2
        return 1
    fi

    local installed_version=""

    # Query installed version (empty if not installed)
    installed_version=$("$py" -c "
import sys
try:
    import importlib.metadata as m
except Exception:
    m = None

def ver(name):
    if m is not None:
        try:
            return m.version(name)
        except Exception:
            return ''
    try:
        import pkg_resources
        try:
            return pkg_resources.get_distribution(name).version
        except Exception:
            return ''
    except Exception:
        return ''

print(ver('bumble'))
" 2>/dev/null)

    if [[ "$installed_version" == "$required_version" ]]; then
        return 0
    fi

    if [[ -z "$installed_version" ]]; then
        echo "Bumble not installed; installing Bumble==$required_version"
    else
        echo "Bumble version mismatch (installed: $installed_version, " \
             "required: $required_version); reinstalling"
        "$py" -m pip uninstall -y bumble >/dev/null 2>&1 || true
    fi

    # Ensure pip is available
    if ! "$py" -m pip --version >/dev/null 2>&1; then
        echo "Error: pip not available for '$py'; cannot install Bumble" >&2
        return 1
    fi

    # Install the pinned version
    if ! "$py" -m pip install --upgrade "Bumble==$required_version"; then
        echo "Error: Failed to install Bumble==$required_version" >&2
        return 1
    fi

    return 0
}

# Start bumble controllers with variable number of ports
# Args: log_file port1@bd_address1 [port2@bd_address2] [port3@bd_address3] ...
# Returns: PID on stdout, exit code 0 on success
start_bumble_controllers() {
    if [[ $# -lt 2 ]]; then
        echo "start_bumble_controllers requires at least 2 arguments: "\
                  "<log_file> port1@bd_address1 [port2@bd_address2...]" >&2
        return 1
    fi

    local log_file="$1"
    shift  # Remove first argument, remaining are ports

    local ports=("$@")
    local num_ports=${#ports[@]}

    # Ensure Bumble is present at the expected version before starting controllers.
    if ! ensure_bumble_version "python"; then
        return 1
    fi

    # Check if controllers.py exists
    local controllers_script="${ZEPHYR_BASE}/tests/bluetooth/classic/sim/common/controllers.py"
    if [[ ! -f "$controllers_script" ]]; then
        echo "controllers.py not found at $controllers_script" >&2
        return 1
    fi

    # Build transport arguments
    local transport_args=()
    for port in "${ports[@]}"; do
        transport_args+=("tcp-server:_:$port")
    done

    # Start bumble controllers asynchronously
    python "$controllers_script" "${transport_args[@]}" \
        > "$log_file" 2>&1 &

    local controller_pid=$!

    # Wait briefly to check if process started successfully
    sleep 2

    # Check if process is still running
    if ! kill -0 "$controller_pid" 2>/dev/null; then
        echo "Bumble controllers process failed to start or exited immediately" >&2
        cat "$log_file"
        return 1
    fi

    # Return PID on stdout
    echo "$controller_pid"
    return 0
}

# Stop bumble controllers and release port locks
# Args: pid
# Returns: 0 on success, 1 on failure
stop_bumble_controllers() {
    local pid="$1"

    if [[ -z "$pid" ]]; then
        return 0
    fi

    # Check if process exists before attempting to kill
    if ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    # Kill the process with extended timeout for cleanup
    if ! kill_process_graceful "$pid" "bumble controllers" 10; then
        echo "Warning: Failed to gracefully stop bumble controllers (PID: $pid)" >&2
        # Don't exit immediately, allow cleanup to continue
        return 1
    fi

    return 0
}
