#!/bin/bash
# Visual debugging workflow: run game, take screenshot, analyze with Mimo Omni
# Usage: ./debug_analyze.sh [prompt]
#
# Examples:
#   ./debug_analyze.sh                          # Default analysis
#   ./debug_analyze.sh "Is the player on the ground?"
#   ./debug_analyze.sh "Describe the terrain colors and height variation"
#   ./debug_analyze.sh "Can you see any chunk boundary seams?"

set -e

SCREENSHOT="screenshots/voxov_debug.png"
GAME_BIN="./build/desktop/main/bin/voxov"
DEFAULT_PROMPT="Analyze this voxel planet game screenshot. Describe: 1) Camera perspective and what the player sees 2) Terrain colors and height variation 3) Any visual bugs (seams, missing textures, z-fighting) 4) Is the crosshair visible? 5) HUD debug info visible? 6) Overall game state assessment"

PROMPT="${1:-$DEFAULT_PROMPT}"

# Build if needed
if [ ! -f "$GAME_BIN" ]; then
    echo "Building game..."
    cmake --build build/desktop/main --parallel
fi

# Remove old screenshot
rm -f "$SCREENSHOT"

echo "Starting game (15 seconds)..."
echo "Move around with WASD, look with mouse, then the screenshot will be taken."
echo ""

# Run game in background
timeout 18 "$GAME_BIN" &
GAME_PID=$!

# Wait for chunks to load and player to settle
sleep 14

# Take screenshot
echo "Taking screenshot..."
if [ -f "$SCREENSHOT" ]; then
    echo "Screenshot saved: $SCREENSHOT ($(du -h "$SCREENSHOT" | cut -f1))"
else
    echo "WARNING: Screenshot not found at $SCREENSHOT"
    echo "The game's auto-screenshot may not have triggered yet."
fi

# Wait for game to finish
wait $GAME_PID 2>/dev/null || true

echo ""
echo "Analyzing with Mimo Omni..."
echo "Prompt: $PROMPT"
echo "---"

# Analyze with Mimo Omni
opencode run -m "xiaomi/mimo-v2-omni" "$PROMPT" -f "$SCREENSHOT" 2>&1
