#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

import run_singlecore_general_selector as common


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = PROJECT_ROOT / "bin" / "champsim_adaptive_selector"


def parse_env_updates(env_items):
    env = {}
    for item in env_items:
        if "=" not in item:
            raise ValueError(f"expected KEY=VALUE, got: {item}")
        key, value = item.split("=", 1)
        env[key] = value
    return env


def main():
    parser = argparse.ArgumentParser(description="Run the sliding-window adaptive selector on one or more known workloads.")
    parser.add_argument("--workloads", nargs="+", default=sorted(common.DEFAULT_TRACES))
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--warmup", type=int, default=1_000_000)
    parser.add_argument("--simulation", type=int, default=5_000_000)
    parser.add_argument("--results-dir", default=str(PROJECT_ROOT / "results" / "cycle_sliding_window_selector_20260420" / "main_thread"))
    parser.add_argument("--label", default="adaptive")
    parser.add_argument("--env", nargs="*", default=[])
    args = parser.parse_args()

    binary_path = Path(args.binary)
    results_dir = Path(args.results_dir)
    env_updates = parse_env_updates(args.env)

    aggregate = {
        "label": args.label,
        "binary": str(binary_path),
        "env": env_updates,
        "workloads": {},
    }

    for workload in args.workloads:
        trace_path = common.default_trace_path(workload)
        out_prefix = results_dir / "runs" / f"{args.label}_{workload}"
        summary = common.run_binary(
            binary_path=binary_path,
            trace_path=trace_path,
            warmup=args.warmup,
            simulation=args.simulation,
            out_prefix=out_prefix,
            env_updates=env_updates,
        )
        aggregate["workloads"][workload] = summary

    aggregate["sum_ipc"] = sum(item["ipc"] for item in aggregate["workloads"].values())
    output_path = results_dir / "summaries" / f"{args.label}.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(aggregate, indent=2, sort_keys=True) + "\n")
    print(json.dumps(aggregate, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
