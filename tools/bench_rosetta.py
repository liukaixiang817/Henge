#!/usr/bin/env python3
"""Compare the inline APFLAGS path with the C reference, checking every run."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/test_rosetta")
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--output", default="build/rosetta-apflags-benchmark.json")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    rows = []
    for round_number in range(args.rounds):
        modes = ("reference", "inline") if round_number % 2 == 0 else ("inline", "reference")
        for mode in modes:
            command = [args.binary] + (["reference"] if mode == "reference" else [])
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if result.returncode == 77:
                print(result.stdout, end="")
                return 77
            if result.returncode:
                raise RuntimeError(f"{command} failed: {result.stdout}{result.stderr}")
            match = re.search(r"Arithmetic execution .*: ([0-9.]+) ms", result.stdout)
            if not match:
                raise RuntimeError(f"Missing execution timing: {result.stdout}")
            rows.append({"round": round_number, "mode": mode, "ms": float(match[1])})
    medians = {mode: statistics.median(row["ms"] for row in rows if row["mode"] == mode)
               for mode in ("inline", "reference")}
    report = {
        "scope": "196608 ADD/SUB/AND guest cases; includes translation, excludes oracle and process startup",
        "rows": rows,
        "median_ms": medians,
        "speedup": medians["reference"] / medians["inline"],
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: value for key, value in report.items() if key != "rows"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
