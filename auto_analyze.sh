#!/bin/bash
# Auto-analyze screenshot with Mimo Omni — called by game on F5 press
SCREENSHOT="${1:-screenshots/debug_screenshot.png}"
OUTPUT="screenshots/analysis_$(date +%H%M%S).txt"

echo "=== Mimo Omni Analysis $(date) ===" > "$OUTPUT"
echo "Screenshot: $SCREENSHOT" >> "$OUTPUT"
echo "" >> "$OUTPUT"

opencode run -m xiaomi/mimo-v2-omni \
    "Analyze this voxel planet game screenshot. Report: 1) Terrain quality (solid faces? height variation?) 2) Any rendering bugs (seams, holes, wrong colors) 3) Lighting (too dark? shadows?) 4) Overall visual quality 1-10" \
    -f "$SCREENSHOT" 2>&1 >> "$OUTPUT"

echo "" >> "$OUTPUT"
echo "Analysis saved: $OUTPUT"
