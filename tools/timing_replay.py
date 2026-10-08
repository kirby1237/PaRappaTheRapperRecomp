"""Replay local Stage 1 slot 1 under several timing settings and retain evidence.

Requires build-timing/game-local.toml and the locally captured slot 1. Neither
the disc nor savestate is distributed. This is a timing probe, not an autoplay
route: the repeated Triangle taps deliberately sample many musical phases.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import time

from timing_lab import Client


def receipt(client, op, slot=1):
    generation = client.call("savestate_status")["generation"]
    client.call("savestate", op=op, slot=slot)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        status = client.call("savestate_status")
        if status["generation"] > generation and not status["pending"]:
            if not status["last_ok"]:
                raise RuntimeError(status)
            return status
        time.sleep(0.05)
    raise TimeoutError("Savestate completion receipt missing")


def run(root, name, early, late, offset, frames, buffer_ms=None):
    import os
    build = root / "build-timing"
    evidence = root / "analysis" / "replay" / name
    evidence.mkdir(parents=True, exist_ok=True)
    state = build / "mods" / "state.toml"
    state.write_text(f'''format_version = 2

[[package]]
id = "parappa.accessibility.timing"
version = "0.1.0"

[[feature]]
package_id = "parappa.accessibility.timing"
id = "judgement"
enabled = true
[feature.values]
early_ms = "{early}"
late_ms = "{late}"
offset_ms = "{offset}"
''')
    trace = evidence / "judgements.csv"
    env = dict(os.environ, PARAPPA_TIMING_TRACE=str(trace))
    config = build / "game-local.toml"
    if buffer_ms is not None:
        config = build / f"game-audio{buffer_ms}.toml"
        config.write_text((build / "game-local.toml").read_text() + f"\n[audio]\nbuffer_ms = {buffer_ms}\n")
    args = [str(build / "PaRappaTheRapper_Recompiled.exe"),
            "--game", str(config), "--debug-port", "9453",
            "--hidden-window", "--no-launcher", "--memcard-dir", "timing-saves"]
    with (evidence / "stdout.log").open("w") as stdout, (evidence / "stderr.log").open("w") as stderr:
        process = subprocess.Popen(args, cwd=build, env=env, stdout=stdout, stderr=stderr,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            client = Client()
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"Runtime exited ({process.returncode}); see {evidence}")
                try:
                    if client.call("frame")["frame"] >= 120:
                        break
                except (OSError, ConnectionError):
                    pass
                time.sleep(0.1)
            else:
                raise TimeoutError("Runtime did not finish booting")
            restore = receipt(client, "load")
            patch = client.call("read_ram", addr="0x80014718", len=4)
            if patch["hex"].lower() != "2118a300":
                raise RuntimeError(f"Timing mod did not apply: {patch}")
            steps = [(2, 0xefff), (15, 0xffff)] * (frames // 17)
            start = client.route(steps)
            client.wait_route(timeout=60)
            end = client.call("frame")
            result = dict(settings=dict(early_ms=early, late_ms=late, offset_ms=offset),
                          restore=restore, patch=patch, route_start=start, route_end=end,
                          counters=client.call("mod_counters"), audio=client.call("audio_stats"),
                          latency=client.call("latency"),
                          context=client.call("read_ram", addr="0x801c3640", len=0x90),
                          screenshot=client.call("screenshot", path=str(evidence / "screen.png")))
            (evidence / "receipt.json").write_text(json.dumps(result, indent=2))
            if buffer_ms is not None:
                actual = result["audio"]["out"]["target_ms"]
                if actual != buffer_ms:
                    raise RuntimeError(f"Requested {buffer_ms} ms audio target, got {actual}; see {evidence}")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    if not trace.exists():
        raise RuntimeError(f"Plugin trace not created; see {evidence}")
    rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(trace.open())]
    judged = [r for r in rows if r["converted"]]
    if not judged:
        raise RuntimeError(f"No judgement hooks fired; see {evidence}")
    for row in judged:
        biased = row["input_tick"] - row["offset_ticks"] + row["stock_half"] + row["extra_early"]
        # Nonnegative Stage 1 song positions; retail divides with truncation.
        assert biased >= 0
        assert row["slot"] == (biased % 384) // 24, row
        assert row["phase"] == biased % 24, row
    summary = dict(name=name, settings=result["settings"], inputs=len(rows), converted=len(judged),
                   in_window=sum(r["phase"] <= 2*r["stock_half"] + r["extra_early"] + r["extra_late"] for r in judged),
                   player_turn=sum(r["turn_gate"] == 2 for r in judged),
                   stock_halves=sorted(set(r["stock_half"] for r in judged)),
                   tempos=sorted(set(r["tempo"] for r in judged)),
                   hook_arithmetic_verified=True)
    (evidence / "summary.json").write_text(json.dumps(summary, indent=2))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=1020)
    parser.add_argument("--case", choices=["all", "forgiving", "audio60"], default="all")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    state = root / "build-timing/mods/state.toml"
    original = state.read_bytes() if state.exists() else None
    summaries = []
    try:
        cases = [("stock",0,0,0), ("forgiving",10,10,0),
                 ("late-only",0,15,0), ("offset100",0,0,100)]
        if args.case != "all":
            cases = [(args.case, 10, 10, 0)]
        for case in cases:
            summary = run(root, *case, args.frames, buffer_ms=60 if args.case == "audio60" else None)
            summaries.append(summary)
            print(json.dumps(summary), flush=True)
    finally:
        if original is not None:
            state.write_bytes(original)
        else:
            state.write_text("format_version = 2\n")
        destination = root / f"analysis/replay/summary-{args.case}.json"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()
