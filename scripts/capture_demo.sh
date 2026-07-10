#!/usr/bin/env bash
# Run voxov with scripted WASD movement for terrain inspection captures.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT}/build/desktop/main/bin/voxov"
LOG="${ROOT}/screenshots/capture_demo.log"

mkdir -p "${ROOT}/screenshots"
: > "${LOG}"

export VOXOV_CAPTURE_DEMO=1
export VOXOV_SKIP_ANALYZE=1

echo "=== Voxov capture demo $(date -Is) ===" | tee -a "${LOG}"
echo "Binary: ${BIN}" | tee -a "${LOG}"

if [[ ! -x "${BIN}" ]]; then
  echo "Building voxov..." | tee -a "${LOG}"
  cmake --build "${ROOT}/build/desktop/main" --parallel | tee -a "${LOG}"
fi

timeout 90 "${BIN}" 2>&1 | tee -a "${LOG}"
echo "=== Done $(date -Is) ===" | tee -a "${LOG}"