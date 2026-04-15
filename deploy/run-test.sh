#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
#
# wardtest integration test against reffs NFSv4.2
#
# Usage:
#   ./deploy/run-test.sh [--reffs-dir /path/to/reffs] [OPTIONS]
#
# Options:
#   --reffs-dir PATH    Path to reffs source tree (default: ~/reffs)
#   --iterations N      Iterations per client (default: 200)
#   --clients N         Writer threads per client container (default: 2)
#   --codec TYPE        xor or rs (default: xor)
#   --k N               Data shards (default: 4)
#   --m N               Parity shards (default: 1)
#   --verify-time N     Seconds for verify-only phase (default: 30)
#   --port N            Host port for NFS server (default: 12049)

set -euo pipefail

REFFS_DIR="${HOME}/reffs"
ITERATIONS=200
CLIENTS=2
CODEC="xor"
K=4
M=1
VERIFY_TIME=30
PORT=12049
WARDTEST_DIR="$(cd "$(dirname "$0")/.." && pwd)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --reffs-dir)  REFFS_DIR="$2"; shift 2 ;;
        --iterations) ITERATIONS="$2"; shift 2 ;;
        --clients)    CLIENTS="$2"; shift 2 ;;
        --codec)      CODEC="$2"; shift 2 ;;
        --k)          K="$2"; shift 2 ;;
        --m)          M="$2"; shift 2 ;;
        --verify-time) VERIFY_TIME="$2"; shift 2 ;;
        --port)       PORT="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ ! -d "${REFFS_DIR}/src" ]]; then
    echo "Error: reffs source not found at ${REFFS_DIR}"
    echo "Use --reffs-dir to specify the path"
    exit 1
fi

echo "=== wardtest integration test ==="
echo "  reffs:      ${REFFS_DIR}"
echo "  wardtest:   ${WARDTEST_DIR}"
echo "  codec:      ${CODEC} k=${K} m=${M}"
echo "  iterations: ${ITERATIONS} per client"
echo "  clients:    ${CLIENTS} threads per container"
echo "  port:       ${PORT}"
echo ""

# --- Step 1: Build reffs server image ---
echo "--- Building reffs server image ---"
docker build -t wt-server \
    -f "${WARDTEST_DIR}/deploy/Dockerfile.server" \
    "${REFFS_DIR}" 2>&1 | tail -5

# --- Step 2: Build wardtest ---
echo "--- Building wardtest ---"
cd "${WARDTEST_DIR}"
mkdir -p m4 && autoreconf -fi >/dev/null 2>&1
mkdir -p /tmp/wt-build && cd /tmp/wt-build
"${WARDTEST_DIR}/configure" --silent
make -j"$(nproc)" --silent
WARDTEST_BIN=/tmp/wt-build/src/wardtest
echo "  binary: ${WARDTEST_BIN}"

# --- Step 3: Start server ---
echo "--- Starting reffs server ---"
docker rm -f wt-server 2>/dev/null || true
docker run -d --name wt-server \
    --privileged \
    -p "${PORT}:2049" \
    -v "${WARDTEST_DIR}/deploy/server.toml:/etc/reffs/reffsd.toml:ro" \
    wt-server

# Wait for NFS to be ready
echo -n "  Waiting for NFS..."
for i in $(seq 1 30); do
    if rpcinfo -t 127.0.0.1 nfs 4 -n "${PORT}" >/dev/null 2>&1; then
        echo " ready"
        break
    fi
    if [[ $i -eq 30 ]]; then
        echo " TIMEOUT"
        docker logs wt-server 2>&1 | tail -20
        docker rm -f wt-server 2>/dev/null
        exit 1
    fi
    echo -n "."
    sleep 1
done

# --- Step 4: Mount NFS ---
MOUNT_DIR=$(mktemp -d /tmp/wt-mnt-XXXXXX)
echo "--- Mounting NFS at ${MOUNT_DIR} ---"
sudo mount -o vers=4.2,sec=sys,hard,timeo=600,port="${PORT}" \
    127.0.0.1:/ "${MOUNT_DIR}"

# Create wardtest directories on NFS
sudo mkdir -p "${MOUNT_DIR}/wardtest"/{data,meta,history}
sudo chmod 777 "${MOUNT_DIR}/wardtest"/{data,meta,history}

cleanup() {
    echo ""
    echo "--- Cleanup ---"
    sudo umount "${MOUNT_DIR}" 2>/dev/null || true
    rmdir "${MOUNT_DIR}" 2>/dev/null || true
    docker rm -f wt-server 2>/dev/null || true
}
trap cleanup EXIT

# --- Step 5: Run writers ---
COMMON_ARGS="--data ${MOUNT_DIR}/wardtest/data \
    --meta ${MOUNT_DIR}/wardtest/meta \
    --history ${MOUNT_DIR}/wardtest/history \
    --clients ${CLIENTS} \
    --codec ${CODEC} --k ${K} --m ${M} \
    --report 10"

echo "--- Running writer 1 (${ITERATIONS} iterations) ---"
"${WARDTEST_BIN}" ${COMMON_ARGS} --iterations "${ITERATIONS}" &
PID1=$!

echo "--- Running writer 2 (${ITERATIONS} iterations, seed=0xdeadbeef) ---"
"${WARDTEST_BIN}" ${COMMON_ARGS} --iterations "${ITERATIONS}" \
    --seed 0xdeadbeef &
PID2=$!

# Wait for writers
FAIL=0
wait "${PID1}" || FAIL=1
wait "${PID2}" || FAIL=1

if [[ ${FAIL} -ne 0 ]]; then
    echo ""
    echo "*** WRITER FAILED ***"
    echo "Check ${MOUNT_DIR}/wardtest/ for diagnostics"
    exit 2
fi

echo ""
echo "--- Writers complete ---"

# --- Step 6: Verify-only phase ---
echo "--- Running verify-only for ${VERIFY_TIME}s ---"
timeout "${VERIFY_TIME}" "${WARDTEST_BIN}" ${COMMON_ARGS} \
    --iterations 0 --verify-only --report 5 || true

echo ""

# --- Step 7: Report ---
echo "=== Results ==="
echo "  Data files:  $(ls "${MOUNT_DIR}/wardtest/data/" | wc -l)"
echo "  Meta files:  $(ls "${MOUNT_DIR}/wardtest/meta/" | grep -c '\.meta$' || echo 0)"
echo "  History:     $(wc -l "${MOUNT_DIR}/wardtest/history/"*.log 2>/dev/null | tail -1 || echo "0 total")"
echo "  Clients log:"
cat "${MOUNT_DIR}/wardtest/meta/.wardtest_clients" 2>/dev/null | sed 's/^/    /'
echo ""

if [[ -f "${MOUNT_DIR}/wardtest/meta/.wardtest_stop" ]]; then
    echo "*** CORRUPTION DETECTED ***"
    cat "${MOUNT_DIR}/wardtest/meta/.wardtest_stop"
    exit 2
fi

echo "=== PASS: all verifications clean ==="
exit 0
