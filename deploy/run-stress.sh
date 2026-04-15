#!/bin/bash
# SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
#
# Run a 2-minute wardtest stress against a mount point.
#
# Creates <mountpoint>/loghyr/{data,hist,meta} and launches one
# writer thread per CPU core for 120 seconds.
#
# Usage: run-stress.sh <mountpoint>

set -euo pipefail

DURATION=120
CLIENTS=$(nproc)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Argument ---
if [[ $# -lt 1 ]]; then
	echo "Usage: $(basename "$0") <mountpoint>" >&2
	exit 1
fi

MOUNT="$1"

if [[ ! -d "${MOUNT}" ]]; then
	echo "$(basename "$0"): not a directory: ${MOUNT}" >&2
	exit 1
fi

# --- Find binary ---
WARDTEST="${SCRIPT_DIR}/../build/src/wardtest"
if [[ ! -x "${WARDTEST}" ]]; then
	WARDTEST=$(command -v wardtest 2>/dev/null || true)
fi
if [[ -z "${WARDTEST}" || ! -x "${WARDTEST}" ]]; then
	echo "$(basename "$0"): wardtest binary not found" >&2
	echo "  Build with: cd build && make -j\$(nproc)" >&2
	exit 1
fi
WARDTEST="$(realpath "${WARDTEST}")"

# --- Create directories ---
BASE="${MOUNT}/loghyr"
DATA="${BASE}/data"
META="${BASE}/meta"
HIST="${BASE}/hist"

mkdir -p "${DATA}" "${META}" "${HIST}"

# --- Run ---
echo "wardtest stress run"
echo "  binary:    ${WARDTEST}"
echo "  base:      ${BASE}"
echo "  clients:   ${CLIENTS}"
echo "  duration:  ${DURATION}s"
echo ""

"${WARDTEST}" \
	--data    "${DATA}" \
	--meta    "${META}" \
	--history "${HIST}" \
	--clients "${CLIENTS}" \
	--duration "${DURATION}" \
	--report 10

echo ""
echo "done"
