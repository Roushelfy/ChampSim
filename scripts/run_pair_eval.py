#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path

import adaptive_targets as targets
import singlecore_eval_common as common


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a standard 2-core quick evaluation for an arbitrary workload pair and store a summary.")
    parser.add_argument("--binary", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--results-dir", required=True)
    parser.add_argument("--trace-root", default="/Users/kijunshi/Developers/ChampSim/traces")
    parser.add_argument("--warmup", type=int, default=1_000_000)
    parser.add_argument("--simulation", type=int, default=5_000_000)
    parser.add_argument("--workload", action="append", required=True, help="Pass exactly twice, once per core.")
    parser.add_argument("--baseline-ipc", action="append", default=[], help="Override baseline IPC as workload=value")
    parser.add_argument("--env", action="append", default=[])
    args = parser.parse_args()

    if len(args.workload) != 2:
        raise ValueError("--workload must be passed exactly twice")

    pair_key = targets.pair_key(args.workload)
    baseline_ipc = dict(targets.PAIR_BASELINES.get(pair_key, {}).get("baseline_ipc", {}))
    for item in args.baseline_ipc:
        if "=" not in item:
            raise ValueError(f"expected KEY=VALUE, got: {item}")
        key, value = item.split("=", 1)
        baseline_ipc[key] = float(value)

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
    ]
    for workload in args.workload:
        trace_name = Path(common.DEFAULT_TRACES[workload]).name
        cmd.extend(["--trace", str(Path(args.trace_root) / trace_name)])
        cmd.extend(["--workload", workload])
    for workload in args.workload:
        if workload not in baseline_ipc:
            raise KeyError(f"missing baseline IPC for workload: {workload}")
        cmd.extend(["--baseline-ipc", f"{workload}={baseline_ipc[workload]}"])

    cmd.extend(
        [
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
    )
    for item in args.env:
        cmd.extend(["--env", item])

    proc = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, text=True)
    summary_path.write_text(proc.stdout)
    summary = json.loads(proc.stdout)
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
