#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Build and run the wardtest 10k Docker test.
#
# Usage:
#   ./deploy/run-docker.sh [--reffs-dir ~/reffs]

set -euo pipefail

REFFS_DIR="${HOME}/reffs"
WARDTEST_DIR="$(cd "$(dirname "$0")/.." && pwd)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --reffs-dir) REFFS_DIR="$2"; shift 2 ;;
        *) echo "Usage: $0 [--reffs-dir PATH]"; exit 1 ;;
    esac
done

if [[ ! -f "${REFFS_DIR}/configure.ac" ]]; then
    echo "Error: reffs source not found at ${REFFS_DIR}"
    exit 1
fi

echo "=== wardtest 10k Docker test ==="
echo "  reffs:    ${REFFS_DIR}"
echo "  wardtest: ${WARDTEST_DIR}"
echo ""

# -- Step 1: Build reffs server image --
echo "--- Building wt-server image from reffs ---"
docker build -t wt-server \
    -f "${WARDTEST_DIR}/deploy/Dockerfile.server" \
    "${REFFS_DIR}" 2>&1 | tail -5
echo ""

# -- Step 2: Build wardtest client image + run compose --
echo "--- Starting 1 server + 4 clients (10k iterations) ---"
export WARDTEST_DIR
cd "${WARDTEST_DIR}"
docker compose -f deploy/docker-compose.yml up --build --abort-on-container-exit 2>&1
EXIT=$?

echo ""
echo "--- Collecting results ---"

# Check if any client exited non-zero
FAILED=0
for c in wt-client1 wt-client2 wt-client3 wt-client4; do
    CODE=$(docker inspect --format='{{.State.ExitCode}}' "$c" 2>/dev/null || echo "?")
    if [[ "$CODE" != "0" ]]; then
        echo "  $c: FAILED (exit $CODE)"
        FAILED=1
    else
        echo "  $c: PASS"
    fi
done

echo ""

# -- Cleanup --
docker compose -f deploy/docker-compose.yml down -v 2>/dev/null

if [[ $FAILED -ne 0 ]]; then
    echo "=== FAIL: one or more clients detected corruption ==="
    exit 2
fi

echo "=== PASS: 10k iterations, 4 clients, zero corruption ==="
exit 0
