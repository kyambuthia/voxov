#!/bin/bash
# ── Automated Visual Testing with Mimo Omni ──────────────────────────
# Runs the game, takes screenshots at key moments, analyzes each with
# Mimo Omni, and produces a structured test report.
#
# Usage:
#   ./auto_test.sh                    # Full test suite
#   ./auto_test.sh --quick            # Quick test (5s game, 1 screenshot)
#   ./auto_test.sh --feature walking  # Test specific feature
#   ./auto_test.sh --iterate 3        # Run 3 iterations, analyze each

set -euo pipefail

GAME_BIN="./build/desktop/main/bin/voxov"
SCREENSHOT_DIR="screenshots/auto_test"
REPORT_FILE="test_report.md"
MIMO_MODEL="xiaomi/mimo-v2-omni"
GAME_DURATION=20
QUICK_MODE=false
FEATURE=""
ITERATIONS=1

# Parse args
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick) QUICK_MODE=true; GAME_DURATION=8; shift ;;
        --feature) FEATURE="$2"; shift 2 ;;
        --iterate) ITERATIONS="$2"; shift 2 ;;
        --duration) GAME_DURATION="$2"; shift 2 ;;
        *) shift ;;
    esac
done

# Ensure build exists
if [ ! -f "$GAME_BIN" ]; then
    echo "Building game..."
    cmake --build build/desktop/main --parallel 2>&1 | tail -3
fi

mkdir -p "$SCREENSHOT_DIR"

# ── Test prompts for Mimo Omni ────────────────────────────────────────
analyze_screenshot() {
    local screenshot="$1"
    local context="$2"
    
    local prompt="You are a QA tester for a voxel planet game. Analyze this screenshot and answer these questions:

1. RENDERING: Is terrain visible? What colors? Any visual glitches (z-fighting, seams, missing geometry)?
2. CAMERA: Is this first-person? Is the view correct (not upside down, not inside geometry)?
3. GAMEPLAY: Can you see a crosshair? Is the player on solid ground or floating?
4. HUD: Is debug info visible? What does it show?
5. ISSUES: List any bugs or problems you see.
6. SEVERITY: Rate the visual quality 1-10 (10=perfect).

Context: $context
Be specific and technical. If something looks wrong, describe exactly what."

    opencode run -m "$MIMO_MODEL" "$prompt" -f "$screenshot" 2>&1
}

# ── Run game and capture screenshots ──────────────────────────────────
run_game_with_screenshots() {
    local iteration="$1"
    local prefix="${SCREENSHOT_DIR}/iter${iteration}"
    
    echo "=== Iteration $iteration: Running game for ${GAME_DURATION}s ==="
    
    # Remove old screenshots for this iteration
    rm -f "${prefix}"_*.png
    
    # Run game in background
    timeout $((GAME_DURATION + 3)) "$GAME_BIN" 2>&1 | while IFS= read -r line; do
        # Capture auto-screenshot log
        if echo "$line" | grep -q "Auto-screenshot saved"; then
            local src="screenshots/voxov_debug.png"
            if [ -f "$src" ]; then
                cp "$src" "${prefix}_initial.png"
                echo "  Captured: initial screenshot"
            fi
        fi
        
        # Log game output for context
        if echo "$line" | grep -qE "Frame [0-9]+:"; then
            echo "$line" >> "${prefix}_gamelog.txt"
        fi
    done &
    GAME_PID=$!
    
    # Wait for game to start and load chunks
    sleep 5
    
    # Take screenshot at gameplay state
    local src="screenshots/voxov_debug.png"
    if [ -f "$src" ]; then
        cp "$src" "${prefix}_gameplay.png"
        echo "  Captured: gameplay screenshot"
    fi
    
    # Wait a bit more for exploration
    sleep 8
    
    if [ -f "$src" ]; then
        cp "$src" "${prefix}_exploration.png"
        echo "  Captured: exploration screenshot"
    fi
    
    # Wait for game to finish
    wait $GAME_PID 2>/dev/null || true
    
    echo "  Game session complete"
}

# ── Main test loop ────────────────────────────────────────────────────
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║       Automated Visual Testing with Mimo Omni              ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Configuration:"
echo "  Game duration: ${GAME_DURATION}s per iteration"
echo "  Iterations: $ITERATIONS"
echo "  Model: $MIMO_MODEL"
echo "  Screenshot dir: $SCREENSHOT_DIR"
echo ""

# Initialize report
cat > "$REPORT_FILE" << EOF
# Automated Visual Test Report
**Date:** $(date)
**Game:** $GAME_BIN
**Model:** $MIMO_MODEL
**Iterations:** $ITERATIONS

---

EOF

# Run iterations
for i in $(seq 1 "$ITERATIONS"); do
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  ITERATION $i / $ITERATIONS"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Run game and capture
    run_game_with_screenshots "$i"
    
    # Analyze each screenshot
    echo ""
    echo "  Analyzing screenshots with $MIMO_MODEL..."
    
    for screenshot in "${SCREENSHOT_DIR}/iter${i}"_*.png; do
        if [ ! -f "$screenshot" ]; then
            continue
        fi
        
        local_name=$(basename "$screenshot")
        echo ""
        echo "  ── Analyzing: $local_name ──"
        
        context="Iteration $i, screenshot: $local_name, game duration: ${GAME_DURATION}s"
        
        # Get analysis
        analysis=$(analyze_screenshot "$screenshot" "$context" 2>&1)
        
        # Append to report
        cat >> "$REPORT_FILE" << EOF

## Iteration $i: $local_name
![screenshot]($screenshot)

$analysis

---

EOF
        
        echo "  Analysis complete for $local_name"
        
        # Brief pause to avoid rate limiting
        sleep 2
    done
done

# ── Summary ───────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                    Test Complete                            ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Report saved to: $REPORT_FILE"
echo "Screenshots in: $SCREENSHOT_DIR/"
echo ""
echo "To review:"
echo "  cat $REPORT_FILE"
echo ""
echo "To re-analyze a specific screenshot:"
echo "  opencode run -m $MIMO_MODEL \"Describe what you see\" -f <screenshot>"
