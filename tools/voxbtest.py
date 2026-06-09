#!/usr/bin/env python3
"""
voxbtest — Gameplay screenshot + AI analysis tool for Voxov.

Launches the game, sends WASD inputs, captures screenshots at intervals,
analyzes each with the xiaomi mimo vision model, and produces a markdown report.

Requirements:
  - grim (Wayland screenshot)
  - wtype (Wayland keyboard input)
  - opencode CLI with mimo-v2.5-pro model

Usage:
  python3 tools/voxbtest.py [--binary PATH] [--duration SECS] [--interval SECS]
"""

import argparse
import base64
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

# ── Config ────────────────────────────────────────────────────────────────

DEFAULT_BINARY = "build/desktop/bin/voxov"
DEFAULT_DURATION = 20        # seconds to run
DEFAULT_INTERVAL = 3         # seconds between screenshots
SCREENSHOT_DIR = "tools/screenshots"
REPORT_DIR = "tools/reports"
MIMO_MODEL = "opencode-go/mimo-v2.5-pro"

# WASD movement sequence: each entry is (key, hold_seconds)
MOVE_SEQUENCE = [
    ("w", 1.5),   # forward
    ("w", 1.5),   # forward more
    ("a", 1.0),   # strafe left
    ("w", 1.5),   # forward
    ("d", 1.0),   # strafe right
    ("s", 1.0),   # backward
    ("w", 2.0),   # forward
    ("w", 1.5),   # forward
]


def run_cmd(cmd: list[str], timeout: int = 10) -> subprocess.CompletedProcess:
    """Run a command and return the result."""
    try:
        return subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout
        )
    except FileNotFoundError:
        print(f"ERROR: command not found: {cmd[0]}")
        sys.exit(1)
    except subprocess.TimeoutExpired:
        print(f"WARNING: command timed out: {' '.join(cmd)}")
        return subprocess.CompletedProcess(cmd, 1, "", "timeout")


def check_deps() -> tuple[bool, bool]:
    """Check that required tools are installed. Returns (ok, has_input)."""
    ok = True
    has_input = False
    for tool in ["grim", "opencode"]:
        result = run_cmd(["which", tool])
        if result.returncode != 0:
            print(f"ERROR: {tool} not found. Install it first.")
            ok = False
    # Input tools are optional — tool works without them (screenshots only)
    for tool in ["wtype", "xdotool"]:
        result = run_cmd(["which", tool])
        if result.returncode == 0:
            has_input = True
            break
    if not has_input:
        print("WARNING: no input tool (wtype/xdotool). Screenshots only, no WASD.")
    return ok, has_input


def take_screenshot(output_path: str) -> bool:
    """Capture a screenshot using grim."""
    result = run_cmd(["grim", output_path])
    if result.returncode != 0:
        print(f"  WARNING: grim failed: {result.stderr.strip()}")
        return False
    return Path(output_path).exists()


def send_key(key: str, hold_seconds: float = 0.5):
    """Send a key press via wtype (Wayland)."""
    # Key down
    run_cmd(["wtype", "-M", "shift"] if key.isupper() else ["wtype", key])
    time.sleep(hold_seconds)
    # wtype doesn't have explicit keyup; re-pressing sends release


def send_key_tap(key: str):
    """Send a single key tap via wtype."""
    run_cmd(["wtype", key])


def analyze_screenshot(image_path: str, phase: str) -> str:
    """Send a screenshot to mimo for visual analysis."""
    prompt = (
        f"You are a game QA tester analyzing a screenshot from a voxel planet game (Voxov). "
        f"The game uses a cube-sphere planet with block-based terrain. "
        f"Current phase: {phase}.\n\n"
        f"Analyze this screenshot and report:\n"
        f"1. Is the terrain/surface visible? Describe what you see.\n"
        f"2. Are there any visual bugs (holes, missing faces, wrong colors, z-fighting, seams)?\n"
        f"3. Is the camera positioned correctly (on/near the planet surface)?\n"
        f"4. Does the voxel surface look correct (flat faces, proper lighting, no artifacts)?\n"
        f"5. Any other observations (performance hints, UI elements, etc.)?\n\n"
        f"Be specific and technical. If everything looks correct, say so."
    )

    result = run_cmd([
        "opencode", "run",
        "-m", MIMO_MODEL,
        "-f", image_path,
        prompt
    ], timeout=60)

    if result.returncode != 0:
        return f"[Analysis failed: {result.stderr.strip()}]"
    return result.stdout.strip()


def generate_report(
    screenshots: list[dict],
    analyses: list[dict],
    output_path: str,
    duration: float,
    binary: str,
):
    """Generate a markdown report."""
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        f"# Voxov Gameplay Test Report",
        f"",
        f"- **Date**: {now}",
        f"- **Binary**: `{binary}`",
        f"- **Duration**: {duration:.1f}s",
        f"- **Screenshots**: {len(screenshots)}",
        f"- **AI Model**: {MIMO_MODEL}",
        f"",
        f"---",
        f"",
    ]

    # Summary
    lines.append("## Summary\n")
    bug_count = sum(1 for a in analyses if "bug" in a.get("analysis", "").lower()
                    or "issue" in a.get("analysis", "").lower()
                    or "broken" in a.get("analysis", "").lower()
                    or "missing" in a.get("analysis", "").lower())
    lines.append(f"- Total screenshots analyzed: {len(analyses)}")
    lines.append(f"- Screenshots with potential issues: {bug_count}")
    lines.append("")

    # Per-screenshot analysis
    lines.append("## Screenshot Analysis\n")
    for i, (ss, analysis) in enumerate(zip(screenshots, analyses)):
        lines.append(f"### Screenshot {i+1}: {ss['phase']}")
        lines.append(f"- **Time**: {ss['time']:.1f}s")
        lines.append(f"- **File**: `{ss['path']}`")
        lines.append(f"- **Input**: {ss['input']}")
        lines.append("")
        lines.append("**AI Analysis:**")
        lines.append("")
        lines.append(analysis.get("analysis", "[No analysis]"))
        lines.append("")
        lines.append("---")
        lines.append("")

    # Write report
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        f.write("\n".join(lines))
    print(f"\nReport written to: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Voxov gameplay test tool")
    parser.add_argument("--binary", default=DEFAULT_BINARY,
                        help=f"Path to voxov binary (default: {DEFAULT_BINARY})")
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL,
                        help=f"Seconds between screenshots (default: {DEFAULT_INTERVAL})")
    parser.add_argument("--no-ai", action="store_true",
                        help="Skip AI analysis (screenshot only)")
    parser.add_argument("--output", default=None,
                        help="Output report path (default: tools/reports/report-<timestamp>.md)")
    args = parser.parse_args()

    # Check dependencies
    ok, has_input = check_deps()
    if not ok:
        sys.exit(1)

    binary = os.path.abspath(args.binary)
    if not Path(binary).exists():
        print(f"ERROR: binary not found: {binary}")
        print("Build first: cmake --build build/desktop --parallel")
        sys.exit(1)

    # Setup output dirs
    ts = datetime.now().strftime("%Y%m%d-%H%M%S")
    ss_dir = Path(SCREENSHOT_DIR) / ts
    ss_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.output or str(Path(REPORT_DIR) / f"report-{ts}.md")

    print(f"=== Voxov Gameplay Test ===")
    print(f"Binary:   {binary}")
    print(f"Duration: {args.duration}s")
    print(f"Interval: {args.interval}s")
    print(f"Screenshots: {ss_dir}")
    print(f"Report:  {report_path}")
    print()

    # Launch game
    print("Launching game...")
    game_proc = subprocess.Popen(
        [binary],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    time.sleep(4)  # wait for init

    if game_proc.poll() is not None:
        stdout = game_proc.stdout.read().decode() if game_proc.stdout else ""
        stderr = game_proc.stderr.read().decode() if game_proc.stderr else ""
        print(f"ERROR: game exited early (code {game_proc.returncode})")
        print(f"stdout: {stdout[:500]}")
        print(f"stderr: {stderr[:500]}")
        sys.exit(1)

    screenshots = []
    analyses = []
    start_time = time.time()
    move_idx = 0

    try:
        while time.time() - start_time < args.duration:
            elapsed = time.time() - start_time

            # Send movement input
            if has_input and move_idx < len(MOVE_SEQUENCE):
                key, hold = MOVE_SEQUENCE[move_idx]
                print(f"  [{elapsed:.1f}s] Sending key: {key} (hold {hold}s)")
                send_key(key, hold)
                move_idx += 1
            elif has_input:
                # Loop back
                move_idx = 0

            time.sleep(0.5)  # let the frame render

            # Take screenshot
            elapsed = time.time() - start_time
            ss_path = str(ss_dir / f"frame-{len(screenshots):03d}.png")
            phase = f"t={elapsed:.1f}s, input={MOVE_SEQUENCE[(move_idx-1) % len(MOVE_SEQUENCE)][0] if move_idx > 0 else 'init'}"

            if take_screenshot(ss_path):
                ss_info = {
                    "path": ss_path,
                    "time": elapsed,
                    "phase": phase,
                    "input": MOVE_SEQUENCE[(move_idx-1) % len(MOVE_SEQUENCE)][0] if move_idx > 0 else "none",
                }
                screenshots.append(ss_info)
                print(f"  [{elapsed:.1f}s] Screenshot: {ss_path}")

                # AI analysis
                if not args.no_ai:
                    print(f"  [{elapsed:.1f}s] Analyzing with mimo...")
                    analysis = analyze_screenshot(ss_path, phase)
                    analyses.append({"analysis": analysis})
                    # Print first line of analysis
                    first_line = analysis.split("\n")[0][:120]
                    print(f"  [{elapsed:.1f}s] → {first_line}")
                else:
                    analyses.append({"analysis": "[AI analysis skipped]"})
            else:
                print(f"  [{elapsed:.1f}s] Screenshot failed")

            # Wait for interval
            time.sleep(max(0, args.interval - 1.5))

    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        # Stop game
        print("Stopping game...")
        game_proc.terminate()
        try:
            game_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            game_proc.kill()
            game_proc.wait()

    # Generate report
    total_time = time.time() - start_time
    generate_report(screenshots, analyses, report_path, total_time, binary)
    print(f"\nDone. {len(screenshots)} screenshots captured.")


if __name__ == "__main__":
    main()
