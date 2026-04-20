#!/usr/bin/env python3

import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


TRACE_WORKLOAD_RE = re.compile(r"^\d+\.([A-Za-z0-9_]+)")
CPU_IPC_RE = re.compile(r"CPU (\d+) cumulative IPC:\s*([0-9.]+)\s+instructions:\s*(\d+)\s+cycles:\s*(\d+)")
CONTROLLER_RE = re.compile(r"^\[(?:ADAPTIVE_SELECTOR|BWC(?:_[A-Z]+)*|BWC_GSP_TIERED|FDP|SPP_ORIG|BOP)\].*$", re.MULTILINE)

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRACES = {
    "mcf": "traces/429.mcf-51B.champsimtrace.xz",
    "lbm": "traces/470.lbm-1274B.champsimtrace.xz",
    "astar": "traces/473.astar-359B.champsimtrace.xz",
    "bzip2": "traces/401.bzip2-226B.champsimtrace.xz",
}
BASELINES = {
    "astar": 0.11624407059812944,
    "mcf": 0.09304738191331209,
    "lbm": 0.8705538637792136,
    "bzip2": 2.132624022725237,
}
BESTS = {
    "astar": 0.11689840080543869,
    "mcf": 0.09483537300389441,
    "lbm": 0.8728014354438657,
    "bzip2": 2.1518726883512267,
}
MIN_ACCEPTABLE = {
    "astar": 0.11670210174324592,
    "mcf": 0.09429897567671972,
    "lbm": 0.8721271639444701,
    "bzip2": 2.14609808866343,
}


def infer_workload(trace_path):
    name = Path(trace_path).name
    match = TRACE_WORKLOAD_RE.match(name)
    if match:
        return match.group(1)
    stem = name
    for suffix in (".champsimtrace.xz", ".champsimtrace"):
        if stem.endswith(suffix):
            return stem[: -len(suffix)]
    return stem


def default_trace_path(workload):
    if workload not in DEFAULT_TRACES:
        raise KeyError(f"unknown workload: {workload}")
    return PROJECT_ROOT / DEFAULT_TRACES[workload]


def load_roi(json_path):
    phases = json.loads(Path(json_path).read_text())
    if not phases:
        raise ValueError(f"no simulation phases found in {json_path}")
    phase = phases[-1]
    if "roi" not in phase:
        raise ValueError(f"missing roi in {json_path}")
    return phase["roi"]


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


def cache_total_misses(cache_stats, cpu_idx):
    total = 0
    for value in cache_stats.values():
        if isinstance(value, dict) and "miss" in value:
            misses = value["miss"]
            if isinstance(misses, list):
                if cpu_idx < len(misses):
                    total += misses[cpu_idx]
            else:
                total += misses
    return total


def file_sha256(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as infile:
        while True:
            chunk = infile.read(1024 * 1024)
            if not chunk:
                break
            hasher.update(chunk)
    return hasher.hexdigest()


def source_fingerprints():
    fingerprints = {}
    tracked = [
        PROJECT_ROOT / "prefetcher" / "adaptive_selector" / "adaptive_selector.cc",
        PROJECT_ROOT / "prefetcher" / "adaptive_selector" / "adaptive_selector.h",
        PROJECT_ROOT / "prefetcher" / "spp_orig" / "spp_orig.cc",
        PROJECT_ROOT / "prefetcher" / "spp_orig" / "spp_orig.h",
        PROJECT_ROOT / "prefetcher" / "spp_dev" / "spp_dev.cc",
        PROJECT_ROOT / "prefetcher" / "spp_dev" / "spp_dev.h",
        PROJECT_ROOT / "prefetcher" / "bop" / "bop.cc",
        PROJECT_ROOT / "prefetcher" / "bop" / "bop.h",
        PROJECT_ROOT / "configs" / "adaptive_selector_config.json",
    ]
    for path in tracked:
        if path.exists():
            fingerprints[str(path.relative_to(PROJECT_ROOT))] = file_sha256(path)
    return fingerprints


def summarize_singlecore_run(roi, stdout_text, trace_path, binary_path, warmup, simulation, label, env_updates):
    cpu_ipc = parse_cpu_ipc(stdout_text)
    if 0 in cpu_ipc:
        instructions = cpu_ipc[0]["instructions"]
        cycles = cpu_ipc[0]["cycles"]
        ipc = instructions / cycles
        printed_ipc = cpu_ipc[0]["printed_ipc"]
    else:
        core = roi["cores"][0]
        instructions = core["instructions"]
        cycles = core["cycles"]
        ipc = instructions / cycles
        printed_ipc = None

    kinst = instructions / 1000.0 if instructions else 1.0
    l2 = roi["cpu0_L2C"]
    dtlb = roi["cpu0_DTLB"]
    dram = roi["DRAM"][0] if roi.get("DRAM") else {}
    llc_miss = cache_total_misses(roi["LLC"], 0)
    pf_issued = l2["prefetch issued"]
    pf_useful = l2["useful prefetch"]
    pf_accuracy = (pf_useful / pf_issued) if pf_issued else 0.0
    row_hits = dram.get("RQ ROW_BUFFER_HIT", 0)
    row_misses = dram.get("RQ ROW_BUFFER_MISS", 0)
    row_total = row_hits + row_misses
    workload = infer_workload(trace_path)

    summary = {
        "label": label,
        "binary": str(binary_path),
        "binary_sha256": file_sha256(binary_path),
        "source_fingerprints": source_fingerprints(),
        "workload": workload,
        "trace": str(trace_path),
        "warmup": warmup,
        "simulation": simulation,
        "env": env_updates,
        "controller_lines": CONTROLLER_RE.findall(stdout_text),
        "instructions": instructions,
        "cycles": cycles,
        "ipc": ipc,
        "printed_ipc": printed_ipc,
        "pf_issued": pf_issued,
        "pf_useful": pf_useful,
        "pf_accuracy": pf_accuracy,
        "llc_miss": llc_miss,
        "dtlb_load_miss": dtlb["LOAD"]["miss"][0],
        "dram": {
            "avg_dbus_congested_cycle": dram.get("AVG DBUS CONGESTED CYCLE", 0.0),
            "rq_row_buffer_hit": row_hits,
            "rq_row_buffer_miss": row_misses,
            "row_hit_rate": (row_hits / row_total) if row_total else 0.0,
        },
        "features": {
            "ipc": ipc,
            "pf_issued_per_kinst": pf_issued / kinst,
            "pf_useful_per_kinst": pf_useful / kinst,
            "pf_accuracy": pf_accuracy,
            "llc_miss_per_kinst": llc_miss / kinst,
            "dtlb_load_miss_mpki": dtlb["LOAD"]["miss"][0] / kinst,
            "avg_dbus_congested_cycle": dram.get("AVG DBUS CONGESTED CYCLE", 0.0),
            "row_hit_rate": (row_hits / row_total) if row_total else 0.0,
        },
    }

    baseline = BASELINES.get(workload)
    best = BESTS.get(workload)
    minimum = MIN_ACCEPTABLE.get(workload)
    if baseline is not None:
        summary["delta_vs_orig"] = ipc - baseline
    if best is not None:
        denom = best - baseline
        summary["gain_retention_vs_best"] = 1.0 if denom <= 0.0 else (ipc - baseline) / denom
    if minimum is not None:
        summary["meets_minimum_acceptable"] = ipc >= minimum
        summary["minimum_acceptable_ipc"] = minimum

    return summary


def run_binary(binary_path, trace_path, warmup, simulation, out_prefix, env_updates):
    binary_path = Path(binary_path)
    trace_path = Path(trace_path)
    out_prefix = Path(out_prefix)
    out_prefix.parent.mkdir(parents=True, exist_ok=True)
    json_path = out_prefix.with_suffix(".json")
    log_path = out_prefix.with_suffix(".log")

    cmd = [
        str(binary_path),
        "--warmup-instructions",
        str(warmup),
        "--simulation-instructions",
        str(simulation),
        "--json",
        str(json_path),
        str(trace_path),
    ]
    env = os.environ.copy()
    env.update(env_updates)
    completed = subprocess.run(cmd, check=False, capture_output=True, text=True, env=env)
    stdout_text = completed.stdout + completed.stderr
    log_path.write_text(stdout_text)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)
    roi = load_roi(json_path)
    return summarize_singlecore_run(roi, stdout_text, trace_path, binary_path, warmup, simulation, out_prefix.stem, env_updates)
