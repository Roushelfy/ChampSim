#!/usr/bin/env python3

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import singlecore_eval_common as common

CONTROLLER_RE = re.compile(r"^\[(?:ADAPTIVE_SELECTOR|BWC(?:_[A-Z]+)*|BWC_GSP_TIERED|FDP|SPP_ORIG|BOP)\].*$", re.MULTILINE)
TRACE_WORKLOAD_RE = re.compile(r"^\d+\.([A-Za-z0-9_]+)")
CPU_IPC_RE = re.compile(r"CPU (\d+) cumulative IPC:\s*([0-9.]+)\s+instructions:\s*(\d+)\s+cycles:\s*(\d+)")
CPU_RUN_RE = re.compile(r"CPU (\d+) runs (\S+)")


def parse_kv(items, value_type):
    parsed = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"expected KEY=VALUE, got: {item}")
        key, value = item.split("=", 1)
        parsed[key] = value_type(value)
    return parsed


def cache_total_misses(cache_stats, cpu_idx):
    total = 0
    for key, value in cache_stats.items():
        if isinstance(value, dict) and "miss" in value:
            misses = value["miss"]
            if isinstance(misses, list):
                if cpu_idx < len(misses):
                    total += misses[cpu_idx]
            else:
                total += misses
    return total


def infer_workload(trace_path):
    name = Path(trace_path).name
    match = TRACE_WORKLOAD_RE.match(name)
    if match:
        return match.group(1)
    stem = name
    for suffix in (".champsimtrace.xz", ".champsimtrace"):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
            break
    return stem


def parse_cpu_ipc(stdout_text):
    parsed = {}
    for match in CPU_IPC_RE.finditer(stdout_text):
        cpu = int(match.group(1))
        parsed[cpu] = {
            "printed_ipc": float(match.group(2)),
            "instructions": int(match.group(3)),
            "cycles": int(match.group(4)),
        }
    return parsed


def parse_cpu_runs(stdout_text):
    parsed = {}
    for match in CPU_RUN_RE.finditer(stdout_text):
        cpu = int(match.group(1))
        parsed[cpu] = match.group(2)
    return parsed


def load_roi(json_path):
    phases = json.loads(Path(json_path).read_text())
    if not phases:
        raise ValueError(f"no simulation phases in {json_path}")
    phase = phases[-1]
    if "roi" not in phase:
        raise ValueError(f"missing roi section in {json_path}")
    return phase["roi"]


def summarize_run(roi, workloads, baselines, stdout_text):
    cpu_ipc = parse_cpu_ipc(stdout_text)
    cpu_runs = parse_cpu_runs(stdout_text)
    dram = roi["DRAM"][0] if roi.get("DRAM") else {}
    llc = roi["LLC"]
    resolved_workloads = []
    for idx, workload in enumerate(workloads):
        if idx in cpu_runs:
            resolved_workloads.append(infer_workload(cpu_runs[idx]))
        else:
            resolved_workloads.append(workload)

    summary = {
        "ipcs": [],
        "instructions": [],
        "cycles": [],
        "pf_issued": [],
        "pf_useful": [],
        "pf_accuracy": [],
        "llc_miss": [],
        "retention_vs_orig": [],
        "weighted_speedup": None,
        "controller_lines": CONTROLLER_RE.findall(stdout_text),
        "resolved_workloads": resolved_workloads,
        "dram": {
            "rq_row_buffer_hit": dram.get("RQ ROW_BUFFER_HIT", 0),
            "rq_row_buffer_miss": dram.get("RQ ROW_BUFFER_MISS", 0),
            "avg_dbus_congested_cycle": dram.get("AVG DBUS CONGESTED CYCLE", 0.0),
        },
    }

    ws = 0.0
    have_all_baselines = True
    for idx, workload in enumerate(resolved_workloads):
        l2 = roi[f"cpu{idx}_L2C"]
        if idx in cpu_ipc:
            instructions = cpu_ipc[idx]["instructions"]
            cycles = cpu_ipc[idx]["cycles"]
            ipc = instructions / cycles
        elif idx < len(roi["cores"]):
            core = roi["cores"][idx]
            instructions = core["instructions"]
            cycles = core["cycles"]
            ipc = instructions / cycles
        else:
            raise ValueError(f"missing core stats for cpu{idx}")
        pf_issued = l2["prefetch issued"]
        pf_useful = l2["useful prefetch"]
        baseline = baselines.get(workload)

        summary["ipcs"].append(ipc)
        summary.setdefault("printed_ipcs", []).append(cpu_ipc.get(idx, {}).get("printed_ipc"))
        summary["instructions"].append(instructions)
        summary["cycles"].append(cycles)
        summary["pf_issued"].append(pf_issued)
        summary["pf_useful"].append(pf_useful)
        summary["pf_accuracy"].append((pf_useful / pf_issued) if pf_issued else 0.0)
        summary["llc_miss"].append(cache_total_misses(roi["LLC"], idx))

        if baseline is None or baseline <= 0.0:
            have_all_baselines = False
            summary["retention_vs_orig"].append(None)
        else:
            retention = ipc / baseline
            summary["retention_vs_orig"].append(retention)
            ws += retention

    if have_all_baselines:
        summary["weighted_speedup"] = ws

    return summary


def main():
    parser = argparse.ArgumentParser(description="Run a ChampSim binary and summarize IPC / WS for quick pair evaluation.")
    parser.add_argument("--binary", required=True, help="Path to the ChampSim binary")
    parser.add_argument("--trace", action="append", required=True, help="Trace path, pass once per core")
    parser.add_argument("--workload", action="append", default=[], help="Workload name, pass once per core")
    parser.add_argument("--baseline-ipc", action="append", default=[], help="Baseline IPC as workload=value")
    parser.add_argument("--env", action="append", default=[], help="Environment override as KEY=VALUE")
    parser.add_argument("--warmup", type=int, default=500000)
    parser.add_argument("--simulation", type=int, default=2500000)
    parser.add_argument("--label", default="")
    parser.add_argument("--log-path")
    parser.add_argument("--json-path")
    parser.add_argument("--summary-path")
    args = parser.parse_args()

    workloads = args.workload or [infer_workload(trace) for trace in args.trace]
    if len(workloads) != len(args.trace):
        raise ValueError("number of workloads must match number of traces")

    baselines = parse_kv(args.baseline_ipc, float)
    env_updates = parse_kv(args.env, str)

    log_path = Path(args.log_path) if args.log_path else Path(tempfile.mkstemp(prefix="quick_pair_eval_", suffix=".txt")[1])
    json_path = Path(args.json_path) if args.json_path else Path(tempfile.mkstemp(prefix="quick_pair_eval_", suffix=".json")[1])

    cmd = [
        args.binary,
        "--warmup-instructions",
        str(args.warmup),
        "--simulation-instructions",
        str(args.simulation),
        "--json",
        str(json_path),
        *args.trace,
    ]

    env = os.environ.copy()
    env.update(env_updates)

    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, check=False)
    log_path.write_text(proc.stdout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        raise SystemExit(proc.returncode)

    roi = load_roi(json_path)
    summary = summarize_run(roi, workloads, baselines, proc.stdout)
    summary.update(
        {
            "label": args.label,
            "binary": args.binary,
            "binary_sha256": common.file_sha256(args.binary),
            "source_fingerprints": common.source_fingerprints(),
            "workloads": workloads,
            "traces": args.trace,
            "warmup": args.warmup,
            "simulation": args.simulation,
            "env": env_updates,
            "log_path": str(log_path),
            "json_path": str(json_path),
        }
    )

    output = json.dumps(summary, indent=2, sort_keys=True)
    print(output)
    if args.summary_path:
        Path(args.summary_path).write_text(output + "\n")


if __name__ == "__main__":
    main()
