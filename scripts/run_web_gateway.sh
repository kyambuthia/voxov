#!/usr/bin/env bash
set -euo pipefail

listen_port="${1:-8080}"
emscripten_root="$(em-config EMSCRIPTEN_ROOT)"
proxy_source="${emscripten_root}/tools/websocket_to_posix_proxy"
proxy_build="${VOXOV_WEB_PROXY_BUILD_DIR:-build/websocket-proxy}"

if [[ ! -f "${proxy_source}/CMakeLists.txt" ]]; then
  echo "Emscripten websocket_to_posix_proxy source was not found: ${proxy_source}" >&2
  exit 1
fi

cmake -S "${proxy_source}" -B "${proxy_build}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${proxy_build}" --parallel 2
exec "${proxy_build}/websocket_to_posix_proxy" "${listen_port}"
