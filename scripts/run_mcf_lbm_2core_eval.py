#!/usr/bin/env python3

import argparse
import subprocess
import sys


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the retained mcf+lbm 2-core quick evaluation and store a summary.")
    parser.add_argument("--binary", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--results-dir", required=True)
    parser.add_argument("--warmup", type=int, default=1_000_000)
    parser.add_argument("--simulation", type=int, default=5_000_000)
    parser.add_argument("--env", action="append", default=[])
    args = parser.parse_args()

    cmd = [
        sys.executable,
        "scripts/run_pair_eval.py",
        "--binary",
        args.binary,
        "--workload",
        "mcf",
        "--workload",
        "lbm",
        "--label",
        args.label,
        "--results-dir",
        args.results_dir,
        "--warmup",
        str(args.warmup),
        "--simulation",
        str(args.simulation),
    ]
    for item in args.env:
        cmd.extend(["--env", item])

    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
