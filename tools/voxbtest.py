#!/usr/bin/python3.13
"""
voxbtest — Gameplay screenshot + AI analysis tool for Voxov.

Launches the game, sends WASD inputs, captures screenshots at intervals,
analyzes each with the xiaomi mimo vision model, and produces a markdown report.

Requirements:
  - ffmpeg (X11 screenshot via x11grab)
  - python-xlib (X11 keyboard input)
  - opencode CLI with mimo-v2.5-pro model

Usage:
  python3 tools/voxbtest.py [--binary PATH] [--duration SECS] [--interval SECS]
"""

import argparse
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

# ── Config ────────────────────────────────────────────────────────────────

DEFAULT_BINARY = "build/desktop/bin/voxov"
DEFAULT_DURATION = 20
DEFAULT_INTERVAL = 3
SCREENSHOT_DIR = "tools/screenshots"
REPORT_DIR = "tools/reports"
MIMO_MODEL = "opencode-go/mimo-v2.5-pro"

# WASD movement sequence: (key, hold_seconds)
MOVE_SEQUENCE = [
    ("w", 1.5),
    ("w", 1.5),
    ("a", 1.0),
    ("w", 1.5),
    ("d", 1.0),
    ("s", 1.0),
    ("w", 2.0),
    ("w", 1.5),
]

DISPLAY = os.environ.get("DISPLAY", ":0")


def run_cmd(cmd: list[str], timeout: int = 10) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        print(f"ERROR: command not found: {cmd[0]}")
        sys.exit(1)
    except subprocess.TimeoutExpired:
        print(f"WARNING: command timed out: {' '.join(cmd)}")
        return subprocess.CompletedProcess(cmd, 1, "", "timeout")


def check_deps() -> tuple[bool, bool]:
    """Returns (ok, has_input)."""
    ok = True
    has_input = False

    # ffmpeg required for screenshots
    result = run_cmd(["which", "ffmpeg"])
    if result.returncode != 0:
        print("ERROR: ffmpeg not found.")
        ok = False

    # opencode required for AI analysis
    result = run_cmd(["which", "opencode"])
    if result.returncode != 0:
        print("ERROR: opencode not found.")
        ok = False

    # python-xlib for keyboard input
    try:
        import Xlib  # noqa: F401
        has_input = True
    except ImportError:
        print("WARNING: python-xlib not found. Screenshots only, no WASD input.")

    return ok, has_input


def get_screen_size() -> tuple[int, int]:
    """Get screen size from xrandr or fallback."""
    result = run_cmd(["xrandr"])
    for line in result.stdout.splitlines():
        if " connected " in line and "x" in line:
            # e.g. "eDP-1 connected primary 1366x768+0+0"
            for part in line.split():
                if "x" in part and "+" in part:
                    size = part.split("+")[0]
                    try:
                        w, h = size.split("x")
                        return int(w), int(h)
                    except ValueError:
                        pass
    return 1366, 768  # fallback


def take_screenshot(output_path: str, screen_size: tuple[int, int]) -> bool:
    """Capture screenshot using GNOME screenshot portal (D-Bus)."""
    try:
        import dbus
        import dbus.mainloop.glib
        from gi.repository import GLib

        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        bus = dbus.SessionBus()
        proxy = bus.get_object(
            'org.freedesktop.portal.Desktop',
            '/org/freedesktop/portal/desktop',
        )
        iface = dbus.Interface(proxy, 'org.freedesktop.portal.Screenshot')

        result_uri = [None]
        loop = GLib.MainLoop()

        def on_response(response, result):
            if response == 0:
                result_uri[0] = result.get('uri', '')
            loop.quit()

        iface.connect_to_signal('Response', on_response)
        iface.Screenshot('', {'interactive': dbus.Boolean(False)})
        loop.run()

        if result_uri[0]:
            uri = result_uri[0]
            if uri.startswith('file://'):
                src = uri[7:]
                import shutil
                shutil.copy2(src, output_path)
                return True
            else:
                print(f"  WARNING: unexpected URI scheme: {uri}")
                return False
        return False

    except Exception as e:
        print(f"  WARNING: portal screenshot failed: {e}")
        # Fallback: ffmpeg x11grab (may show blank on GNOME Wayland)
        w, h = screen_size
        result = run_cmd([
            "ffmpeg", "-y",
            "-f", "x11grab",
            "-video_size", f"{w}x{h}",
            "-i", DISPLAY,
            "-frames:v", "1",
            "-update", "1",
            output_path,
        ])
        return Path(output_path).exists()


def send_key_press(key: str, hold_seconds: float = 0.5):
    """Send key press/release via python-xlib."""
    try:
        import Xlib
        import Xlib.display
        import Xlib.X

        d = Xlib.display.Display(DISPLAY)
        root = d.screen().root

        # Map WASD to X11 keysyms
        keysym_map = {
            "w": 0x0077,  # XK_w
            "a": 0x0061,  # XK_a
            "s": 0x0073,  # XK_s
            "d": 0x0064,  # XK_d
        }
        keysym = keysym_map.get(key.lower())
        if keysym is None:
            return

        keycode = d.keysym_to_keycode(keysym)
        if keycode == 0:
            return

        # Key press event
        event = Xlib.protocol.event.KeyPress(
            time=int(time.time() * 1000) & 0xFFFFFFFF,
            root=root,
            window=root,
            same_screen=1,
            child=Xlib.X.NONE,
            root_x=0, root_y=0, event_x=0, event_y=0,
            state=0,
            detail=keycode,
        )
        root.send_event(event, propagate=True)
        d.sync()

        time.sleep(hold_seconds)

        # Key release event
        event = Xlib.protocol.event.KeyRelease(
            time=int(time.time() * 1000) & 0xFFFFFFFF,
            root=root,
            window=root,
            same_screen=1,
            child=Xlib.X.NONE,
            root_x=0, root_y=0, event_x=0, event_y=0,
            state=0,
            detail=keycode,
        )
        root.send_event(event, propagate=True)
        d.sync()
        d.close()

    except Exception as e:
        print(f"  WARNING: key send failed: {e}")


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
        prompt,
    ], timeout=120)

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
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        "# Voxov Gameplay Test Report",
        "",
        f"- **Date**: {now}",
        f"- **Binary**: `{binary}`",
        f"- **Duration**: {duration:.1f}s",
        f"- **Screenshots**: {len(screenshots)}",
        f"- **AI Model**: {MIMO_MODEL}",
        "",
        "---",
        "",
    ]

    lines.append("## Summary\n")
    bug_count = sum(
        1 for a in analyses
        if any(w in a.get("analysis", "").lower()
               for w in ["bug", "issue", "broken", "missing", "artifact", "glitch"])
    )
    lines.append(f"- Total screenshots analyzed: {len(analyses)}")
    lines.append(f"- Screenshots with potential issues: {bug_count}")
    lines.append("")

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

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        f.write("\n".join(lines))
    print(f"\nReport written to: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Voxov gameplay test tool")
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION)
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL)
    parser.add_argument("--no-ai", action="store_true",
                        help="Skip AI analysis (screenshot only)")
    parser.add_argument("--output", default=None)
    args = parser.parse_args()

    ok, has_input = check_deps()
    if not ok:
        sys.exit(1)

    binary = os.path.abspath(args.binary)
    if not Path(binary).exists():
        print(f"ERROR: binary not found: {binary}")
        sys.exit(1)

    screen_size = get_screen_size()
    ts = datetime.now().strftime("%Y%m%d-%H%M%S")
    ss_dir = Path(SCREENSHOT_DIR) / ts
    ss_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.output or str(Path(REPORT_DIR) / f"report-{ts}.md")

    print(f"=== Voxov Gameplay Test ===")
    print(f"Binary:   {binary}")
    print(f"Screen:   {screen_size[0]}x{screen_size[1]}")
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
    time.sleep(4)

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
            current_input = "none"
            if has_input and move_idx < len(MOVE_SEQUENCE):
                key, hold = MOVE_SEQUENCE[move_idx]
                print(f"  [{elapsed:.1f}s] Sending key: {key} (hold {hold}s)")
                send_key_press(key, hold)
                current_input = key
                move_idx += 1
            elif has_input:
                move_idx = 0

            time.sleep(0.5)

            # Take screenshot
            elapsed = time.time() - start_time
            ss_path = str(ss_dir / f"frame-{len(screenshots):03d}.png")
            phase = f"t={elapsed:.1f}s, input={current_input}"

            if take_screenshot(ss_path, screen_size):
                ss_info = {
                    "path": ss_path,
                    "time": elapsed,
                    "phase": phase,
                    "input": current_input,
                }
                screenshots.append(ss_info)
                print(f"  [{elapsed:.1f}s] Screenshot: {ss_path}")

                if not args.no_ai:
                    print(f"  [{elapsed:.1f}s] Analyzing with mimo...")
                    analysis = analyze_screenshot(ss_path, phase)
                    analyses.append({"analysis": analysis})
                    first_line = analysis.split("\n")[0][:120]
                    print(f"  [{elapsed:.1f}s] -> {first_line}")
                else:
                    analyses.append({"analysis": "[AI analysis skipped]"})
            else:
                print(f"  [{elapsed:.1f}s] Screenshot failed")

            time.sleep(max(0, args.interval - 1.5))

    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        print("Stopping game...")
        game_proc.terminate()
        try:
            game_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            game_proc.kill()
            game_proc.wait()

    total_time = time.time() - start_time
    generate_report(screenshots, analyses, report_path, total_time, binary)
    print(f"\nDone. {len(screenshots)} screenshots captured.")


if __name__ == "__main__":
    main()
