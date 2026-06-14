#!/bin/bash
# ── Feature Test: Quick visual verification ───────────────────────────
# Tests a specific feature by running the game, taking a screenshot,
# and asking Mimo Omni for a pass/fail verdict.
#
# Usage:
#   ./test_feature.sh rendering    # Test if terrain renders
#   ./test_feature.sh walking      # Test if player walks on surface
#   ./test_feature.sh camera       # Test first-person camera
#   ./test_feature.sh blocks       # Test block break/place
#   ./test_feature.sh crosshair    # Test crosshair visibility
#   ./test_feature.sh hud          # Test debug HUD info

set -euo pipefail

GAME_BIN="./build/desktop/main/bin/voxov"
SCREENSHOT="screenshots/feature_test.png"
MIMO_MODEL="xiaomi/mimo-v2-omni"
FEATURE="${1:-rendering}"

# Feature-specific prompts
declare -A PROMPTS
PROMPTS[rendering]="Is solid voxel terrain visible in this screenshot? Answer YES or NO and describe what you see. The terrain should be green grass blocks on a spherical planet."
PROMPTS[walking]="Is the player standing on solid ground (not floating in space)? Look at the HUD - does it show 'Grounded: YES'? Answer YES or NO."
PROMPTS[camera]="Is this a first-person camera view (not third-person orbit)? The camera should be at eye level looking forward. Answer YES or NO."
PROMPTS[blocks]="Can you see any blocks being broken or placed? Is there a crosshair for aiming? Answer YES or NO."
PROMPTS[crosshair]="Is there a white crosshair (+) visible at the center of the screen? Answer YES or NO."
PROMPTS[hud]="Is debug information visible on screen (FPS, position, etc.)? List what info you can see. Answer YES or NO."
PROMPTS[colors]="Describe the terrain colors. Are there different shades of green/brown for height variation? Does it look natural?"
PROMPTS[seams]="Are there visible chunk boundary seams (lines between terrain chunks)? Answer YES and describe, or NO if terrain looks seamless."

# Build if needed
if [ ! -f "$GAME_BIN" ]; then
    echo "Building..."
    cmake --build build/desktop/main --parallel 2>&1 | tail -3
fi

echo "━━━ Feature Test: $FEATURE ━━━"
echo ""

# Run game briefly
echo "Running game (10s)..."
rm -f "$SCREENSHOT" "screenshots/voxov_debug.png"
timeout 13 "$GAME_BIN" > /dev/null 2>&1 &
GAME_PID=$!

# Wait for auto-screenshot (happens at frame 120, ~2s)
sleep 12

# Copy auto-screenshot to our test path
if [ -f "screenshots/voxov_debug.png" ]; then
    cp "screenshots/voxov_debug.png" "$SCREENSHOT"
fi

# Check for screenshot
if [ ! -f "$SCREENSHOT" ]; then
    echo "ERROR: No screenshot captured"
    kill $GAME_PID 2>/dev/null || true
    exit 1
fi

echo "Screenshot captured: $SCREENSHOT ($(du -h "$SCREENSHOT" | cut -f1))"
echo ""

# Wait for game to finish
wait $GAME_PID 2>/dev/null || true

# Get prompt for this feature
PROMPT="${PROMPTS[$FEATURE]:-${PROMPTS[rendering]}}"

echo "Asking $MIMO_MODEL..."
echo "Prompt: $PROMPT"
echo ""
echo "════════════════════════════════════════════════════════════"
echo ""

# Analyze
result=$(opencode run -m "$MIMO_MODEL" "$PROMPT" -f "$SCREENSHOT" 2>&1)
echo "$result"

echo ""
echo "════════════════════════════════════════════════════════════"

# Extract verdict
if echo "$result" | grep -qi "YES"; then
    echo ""
    echo "✓ VERDICT: PASS"
    exit 0
elif echo "$result" | grep -qi "NO"; then
    echo ""
    echo "✗ VERDICT: FAIL"
    exit 1
else
    echo ""
    echo "? VERDICT: UNCERTAIN"
    exit 2
fi
