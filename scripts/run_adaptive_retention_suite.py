#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path

import adaptive_targets as targets
import singlecore_eval_common as common


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def parse_env_updates(env_items):
    env = {}
    for item in env_items:
        if "=" not in item:
            raise ValueError(f"expected KEY=VALUE, got: {item}")
        key, value = item.split("=", 1)
        env[key] = value
    return env


def main():
    parser = argparse.ArgumentParser(description="Run the adaptive selector retention suite across single-core and 2-core workloads.")
    parser.add_argument("--single-binary", required=True)
    parser.add_argument("--pair-binary", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--results-dir", required=True)
    parser.add_argument("--warmup", type=int, default=1_000_000)
    parser.add_argument("--simulation", type=int, default=5_000_000)
    parser.add_argument("--retention-target", type=float, default=0.85)
    parser.add_argument("--workloads", nargs="+", default=["astar", "mcf", "lbm", "bzip2"])
    parser.add_argument("--pair-workloads", nargs=2, action="append", default=None)
    parser.add_argument("--env", nargs="*", default=[])
    args = parser.parse_args()

    env_updates = parse_env_updates(args.env)
    results_dir = Path(args.results_dir)
    runs_dir = results_dir / "runs"
    summaries_dir = results_dir / "summaries"
    runs_dir.mkdir(parents=True, exist_ok=True)
    summaries_dir.mkdir(parents=True, exist_ok=True)

    aggregate = {
        "label": args.label,
        "single_binary": args.single_binary,
        "pair_binary": args.pair_binary,
        "env": env_updates,
        "retention_target": args.retention_target,
        "single_core": {},
        "pairs": {},
    }

    all_pass = True
    min_single_retention = None

    for workload in args.workloads:
        trace_path = common.default_trace_path(workload)
        out_prefix = runs_dir / f"{args.label}_{workload}"
        summary = common.run_binary(
            binary_path=Path(args.single_binary),
            trace_path=trace_path,
            warmup=args.warmup,
            simulation=args.simulation,
            out_prefix=out_prefix,
            env_updates=env_updates,
        )

        baseline = targets.SINGLE_ORIG[workload]
        best = targets.SINGLE_RETAINED[workload]
        threshold = targets.minimum_acceptable(baseline, best, args.retention_target)
        retention = targets.retention_fraction(summary["ipc"], baseline, best)
        meets = summary["ipc"] >= threshold
        all_pass = all_pass and meets
        min_single_retention = retention if min_single_retention is None else min(min_single_retention, retention)

        aggregate["single_core"][workload] = {
            **summary,
            "baseline_orig": baseline,
            "best_retained": best,
            "minimum_acceptable_ipc": threshold,
            "gain_retention_fraction": retention,
            "meets_retention_target": meets,
        }

    pair_cases = [tuple(item) for item in (args.pair_workloads or targets.PAIR_CASES)]
    min_pair_retention = None

    for pair_workloads in pair_cases:
        pair_results_dir = results_dir / "pair" / targets.pair_key(pair_workloads)
        pair_key = targets.pair_key(pair_workloads)
        pair_cmd = [
            sys.executable,
            str(PROJECT_ROOT / "scripts" / "run_pair_eval.py"),
            "--binary",
            args.pair_binary,
            "--label",
            f"{args.label}_{pair_key.replace('+', '_')}",
            "--results-dir",
            str(pair_results_dir),
            "--workload",
            pair_workloads[0],
            "--workload",
            pair_workloads[1],
            "--warmup",
            str(args.warmup),
            "--simulation",
            str(args.simulation),
        ]
        for key, value in env_updates.items():
            pair_cmd.extend(["--env", f"{key}={value}"])

        pair_proc = subprocess.run(pair_cmd, check=True, stdout=subprocess.PIPE, text=True)
        pair_summary = json.loads(pair_proc.stdout)
        pair_baseline = targets.PAIR_BASELINES[pair_key]["weighted_speedup"]
        pair_best = targets.PAIR_RETAINED[pair_key]["weighted_speedup"]
        pair_threshold = targets.minimum_acceptable(pair_baseline, pair_best, args.retention_target)
        pair_retention = targets.retention_fraction(pair_summary["weighted_speedup"], pair_baseline, pair_best)
        pair_meets = pair_summary["weighted_speedup"] >= pair_threshold
        all_pass = all_pass and pair_meets
        min_pair_retention = pair_retention if min_pair_retention is None else min(min_pair_retention, pair_retention)

        aggregate["pairs"][pair_key] = {
            **pair_summary,
            "baseline_weighted_speedup": pair_baseline,
            "best_retained_weighted_speedup": pair_best,
            "minimum_acceptable_weighted_speedup": pair_threshold,
            "gain_retention_fraction": pair_retention,
            "meets_retention_target": pair_meets,
        }

    aggregate["passes_all"] = all_pass
    aggregate["minimum_single_retention_fraction"] = min_single_retention
    aggregate["minimum_pair_retention_fraction"] = min_pair_retention
    aggregate["minimum_overall_retention_fraction"] = min(
        [
            *(item["gain_retention_fraction"] for item in aggregate["single_core"].values()),
            *(item["gain_retention_fraction"] for item in aggregate["pairs"].values()),
        ]
    )

    output_path = summaries_dir / f"{args.label}.json"
    output_path.write_text(json.dumps(aggregate, indent=2, sort_keys=True) + "\n")
    print(json.dumps(aggregate, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
