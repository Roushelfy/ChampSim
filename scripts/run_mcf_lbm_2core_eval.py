#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path


DEFAULT_MCF_ORIG = 0.09304738191331209
DEFAULT_LBM_ORIG = 0.8705538637792136


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the standard mcf+lbm 2-core quick evaluation and store a summary.")
    parser.add_argument("--binary", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--results-dir", required=True)
    parser.add_argument("--trace-root", default="/Users/kijunshi/Developers/ChampSim/traces")
    parser.add_argument("--warmup", type=int, default=1_000_000)
    parser.add_argument("--simulation", type=int, default=5_000_000)
    parser.add_argument("--baseline-mcf", type=float, default=DEFAULT_MCF_ORIG)
    parser.add_argument("--baseline-lbm", type=float, default=DEFAULT_LBM_ORIG)
    parser.add_argument("--env", action="append", default=[])
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    logs_dir = results_dir / "logs"
    json_dir = results_dir / "roi"
    summary_dir = results_dir / "summaries"
    logs_dir.mkdir(parents=True, exist_ok=True)
    json_dir.mkdir(parents=True, exist_ok=True)
    summary_dir.mkdir(parents=True, exist_ok=True)

    summary_path = summary_dir / f"{args.label}.json"
    log_path = logs_dir / f"{args.label}.txt"
    json_path = json_dir / f"{args.label}.json"

    cmd = [
        sys.executable,
        "scripts/quick_pair_eval.py",
        "--binary",
        args.binary,
        "--trace",
        str(Path(args.trace_root) / "429.mcf-51B.champsimtrace.xz"),
        "--trace",
        str(Path(args.trace_root) / "470.lbm-1274B.champsimtrace.xz"),
        "--workload",
        "mcf",
        "--workload",
        "lbm",
        "--baseline-ipc",
        f"mcf={args.baseline_mcf}",
        "--baseline-ipc",
        f"lbm={args.baseline_lbm}",
        "--warmup",
        str(args.warmup),
        "--simulation",
        str(args.simulation),
        "--label",
        args.label,
        "--log-path",
        str(log_path),
        "--json-path",
        str(json_path),
    ]
    for item in args.env:
        cmd.extend(["--env", item])

    proc = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, text=True)
    summary_path.write_text(proc.stdout)
    summary = json.loads(proc.stdout)
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
