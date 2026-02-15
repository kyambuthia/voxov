#!/usr/bin/env bash
set -euo pipefail

cd android/
./gradlew clean
./gradlew assembleDebug
