#!/usr/bin/env bash
set -euo pipefail

rm -rf build_web/
emcmake cmake -S . -B build_web
cmake --build build_web -j
