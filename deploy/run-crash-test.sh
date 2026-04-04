#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# wardtest crash test -- periodically SIGKILL the reffs server while
# 4 clients hammer it over NFSv4.2.  Clients use hard mounts so they
# block during server downtime and resume when it restarts.
#
# Tests: server crash recovery, NFS reconnection, data integrity
# through unclean shutdown.
#
# Usage:
#   sudo ./deploy/run-crash-test.sh [--reffs-dir ~/reffs] [OPTIONS]
#
# Options:
#   --reffs-dir PATH    Path to reffs source (default: ~/reffs)
#   --duration N        Total test duration in seconds (default: 300)
#   --kill-interval N   Seconds between server kills (default: 30)
#   --port N            NFS port (default: 12049)

set -euo pipefail

REFFS_DIR="${REFFS_DIR:-/home/loghyr/reffs}"
DURATION=300
KILL_INTERVAL=30
PORT=12049
WARDTEST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WARDTEST_BIN=""
REFFSD_PID=""
MOUNT_DIR=""
CLIENT_PIDS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --reffs-dir)      REFFS_DIR="$2"; shift 2 ;;
        --duration)       DURATION="$2"; shift 2 ;;
        --kill-interval)  KILL_INTERVAL="$2"; shift 2 ;;
        --port)           PORT="$2"; shift 2 ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
done

REFFSD="${REFFS_DIR}/build/src/reffsd"
if [[ ! -x "${REFFSD}" ]]; then
    echo "Error: reffsd not found at ${REFFSD}"
    echo "Build reffs first: cd ~/reffs/build && make -j\$(nproc)"
    exit 1
fi

# Build wardtest if needed
WARDTEST_BIN="${WARDTEST_DIR}/build/src/wardtest"
if [[ ! -x "${WARDTEST_BIN}" ]]; then
    echo "--- Building wardtest ---"
    cd "${WARDTEST_DIR}"
    mkdir -p m4 && autoreconf -fi >/dev/null 2>&1
    mkdir -p build && cd build
    ../configure --enable-optimize --silent
    make -j"$(nproc)" --silent
fi

echo "=== wardtest crash test ==="
echo "  duration:      ${DURATION}s"
echo "  kill interval: ${KILL_INTERVAL}s"
echo "  port:          ${PORT}"
echo ""

# -- Setup --
mkdir -p /tmp/reffs_crash/{data,state}
MOUNT_DIR=$(mktemp -d /tmp/wt-crash-XXXXXX)

cat > /tmp/reffs_crash/config.toml << EOF
[server]
port           = ${PORT}
bind           = "*"
role           = "standalone"
minor_versions = [1, 2]
grace_period   = 5
workers        = 4

[backend]
type       = "ram"
path       = "/tmp/reffs_crash/data"
state_file = "/tmp/reffs_crash/state"

[[export]]
path        = "/"
clients     = "*"
access      = "rw"
root_squash = false
flavors     = ["sys"]
EOF

cleanup() {
    echo ""
    echo "--- Cleanup ---"
    for pid in "${CLIENT_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    if [[ -n "${REFFSD_PID}" ]]; then
        kill "${REFFSD_PID}" 2>/dev/null || true
        wait "${REFFSD_PID}" 2>/dev/null || true
    fi
    umount "${MOUNT_DIR}" 2>/dev/null || true
    rmdir "${MOUNT_DIR}" 2>/dev/null || true
    rm -rf /tmp/reffs_crash
}
trap cleanup EXIT

start_server() {
    "${REFFSD}" --config /tmp/reffs_crash/config.toml &
    REFFSD_PID=$!
    # Wait for port
    for i in $(seq 1 30); do
        if ss -tln | grep -q ":${PORT} " 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    echo "Error: reffsd didn't start"
    return 1
}

# -- Start server --
echo "--- Starting reffsd ---"
start_server

# -- Mount --
echo "--- Mounting at ${MOUNT_DIR} ---"
mount -o vers=4.2,sec=sys,hard,timeo=600,port="${PORT}" \
    127.0.0.1:/ "${MOUNT_DIR}"
mkdir -p "${MOUNT_DIR}/wardtest"/{data,meta,history}
chmod 777 "${MOUNT_DIR}/wardtest"/{data,meta,history}

# -- Start 4 clients --
COMMON="--data ${MOUNT_DIR}/wardtest/data \
    --meta ${MOUNT_DIR}/wardtest/meta \
    --history ${MOUNT_DIR}/wardtest/history \
    --duration ${DURATION} --clients 2 --report 30"

echo "--- Starting 4 wardtest clients (${DURATION}s each) ---"
"${WARDTEST_BIN}" ${COMMON} --seed 0x11111111 &
CLIENT_PIDS+=($!)
"${WARDTEST_BIN}" ${COMMON} --seed 0x22222222 &
CLIENT_PIDS+=($!)
"${WARDTEST_BIN}" ${COMMON} --seed 0x33333333 &
CLIENT_PIDS+=($!)
"${WARDTEST_BIN}" ${COMMON} --seed 0x44444444 &
CLIENT_PIDS+=($!)

# -- Crash loop --
START=$(date +%s)
KILLS=0

echo "--- Crash loop: killing server every ${KILL_INTERVAL}s ---"
while true; do
    ELAPSED=$(( $(date +%s) - START ))
    REMAINING=$(( DURATION - ELAPSED ))
    if [[ ${REMAINING} -le ${KILL_INTERVAL} ]]; then
        echo "[${ELAPSED}s] Final stretch -- letting clients finish"
        break
    fi

    sleep "${KILL_INTERVAL}"

    # Check if clients are still alive
    ALIVE=0
    for pid in "${CLIENT_PIDS[@]}"; do
        kill -0 "$pid" 2>/dev/null && ALIVE=$((ALIVE + 1))
    done
    if [[ ${ALIVE} -eq 0 ]]; then
        echo "[${ELAPSED}s] All clients exited"
        break
    fi

    KILLS=$((KILLS + 1))
    echo "[${ELAPSED}s] SIGKILL #${KILLS} (${ALIVE} clients alive)"
    kill -9 "${REFFSD_PID}" 2>/dev/null || true
    wait "${REFFSD_PID}" 2>/dev/null || true
    sleep 2
    echo "[${ELAPSED}s] Restarting server..."
    start_server
    echo "[${ELAPSED}s] Server back (PID ${REFFSD_PID})"
done

# -- Wait for clients --
echo "--- Waiting for clients to finish ---"
FAIL=0
for i in "${!CLIENT_PIDS[@]}"; do
    wait "${CLIENT_PIDS[$i]}" || FAIL=1
    CODE=$?
    echo "  client $((i+1)): exit ${CODE}"
done

# -- Verify phase: full read of all surviving stripes --
echo "--- Verify phase: checking all surviving data ---"
"${WARDTEST_BIN}" \
    --data "${MOUNT_DIR}/wardtest/data" \
    --meta "${MOUNT_DIR}/wardtest/meta" \
    --history "${MOUNT_DIR}/wardtest/history" \
    --duration 30 --clients 4 --verify-only --report 10 2>&1
VERIFY_EXIT=$?

# -- Check for corruption --
echo ""
if [[ -f "${MOUNT_DIR}/wardtest/meta/.wardtest_stop" ]]; then
    echo "=== CORRUPTION DETECTED ==="
    cat "${MOUNT_DIR}/wardtest/meta/.wardtest_stop"
    exit 2
fi

if [[ ${VERIFY_EXIT} -ne 0 ]]; then
    echo "=== FAIL: verify phase failed (exit ${VERIFY_EXIT}) ==="
    exit 2
fi

echo "  Server killed ${KILLS} times during test"
echo "  Verify phase: PASS"
echo ""

if [[ ${FAIL} -ne 0 ]]; then
    echo "=== WARN: writer errors during crash recovery (expected) ==="
fi

echo "=== PASS: ${KILLS} forced crashes, zero corruption ==="
