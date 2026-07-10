#!/bin/bash
# Auto-analyze screenshot with Mimo Omni or Gemini fallback
SCREENSHOT="${1:-screenshots/debug_screenshot.png}"
OUTPUT="screenshots/analysis_$(date +%H%M%S).txt"

echo "=== Analysis $(date) ===" | tee "$OUTPUT"
echo "Screenshot: $SCREENSHOT" | tee -a "$OUTPUT"

# Try Mimo first, fall back to agy Gemini if Mimo fails
if /home/mbuthi/.opencode/bin/opencode run -m xiaomi/mimo-v2-omni \
    "Analyze this voxel planet game screenshot. Report: 1) Terrain quality (solid faces? height variation?) 2) Any rendering bugs (seams, holes, wrong colors) 3) Lighting 4) Overall visual quality 1-10" \
    -f "$SCREENSHOT" 2>> /tmp/voxov_analysis.log >> "$OUTPUT" 2>&1; then
    echo "Analyzed with Mimo Omni" | tee -a "$OUTPUT"
else
    echo "Mimo failed, trying Gemini..." | tee -a "$OUTPUT"
    agy --model "Gemini 3.5 Flash (Medium)" -p "Analyze voxel planet screenshot at $SCREENSHOT. Report terrain quality, rendering bugs, lighting, visual score 1-10." 2>> /tmp/voxov_analysis.log >> "$OUTPUT" 2>&1
    echo "Analyzed with Gemini" | tee -a "$OUTPUT"
fi
echo "=== Done ===" | tee -a "$OUTPUT"
