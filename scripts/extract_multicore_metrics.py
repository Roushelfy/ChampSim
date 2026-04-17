#!/usr/bin/env python3
import argparse
import csv
import re
from pathlib import Path


RE_IPC = re.compile(r"CPU (\d+) cumulative IPC:\s*([0-9.]+)\s+instructions:\s*(\d+)\s+cycles:\s*(\d+)")
RE_L2C_PF = re.compile(
    r"cpu(\d+)->cpu\d+_L2C PREFETCH REQUESTED:\s*(\d+)\s+ISSUED:\s*(\d+)\s+USEFUL:\s*(\d+)\s+USELESS:\s*(\d+)"
)
RE_LLC_MISS = re.compile(r"cpu(\d+)->LLC TOTAL\s+ACCESS:\s*(\d+)\s+HIT:\s*(\d+)\s+MISS:\s*(\d+)")
RE_ROW_HIT = re.compile(r"RQ ROW_BUFFER_HIT:\s*(\d+)")
RE_ROW_MISS = re.compile(r"ROW_BUFFER_MISS:\s*(\d+)")
RE_DBUS = re.compile(r"AVG DBUS CONGESTED CYCLE:\s*([0-9.]+)")
RE_KEYVAL = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")
POLICY_PREFIXES = [
    "gsp_headgate_crit_rescuebudget",
    "gsp_headgate_crit_blacklist",
    "gsp_headgate_crit_levelgate",
    "gsp_headgate_crit_confgate_c3",
    "gsp_headgate_crit_confgate_c2",
    "gsp_headgate_crit_confgate",
    "gsp_headgate_crit_rankcap",
    "gsp_headgate_crit_fastuse",
    "crit_rescuebudget",
    "crit_blacklist",
    "crit_levelgate",
    "crit_confgate",
    "crit_rankcap",
    "crit_fastuse",
    "gsp_headgate_dynboost_phase",
    "gsp_headgate_phaserotate",
    "gsp_headgate_dynrunway",
    "gsp_headgate_dynboost",
    "gsp_headgate_probe",
    "gsp_headgate",
    "gsp_util",
    "c1_gsp_tiered",
    "bwc",
    "orig",
    "fdp",
    "bop",
]


def resolve_repo_path(value):
    path = Path(value)
    if path.is_absolute():
        return path
    return Path(__file__).resolve().parents[1] / path


def load_single_core_baselines(path: Path):
    baselines = {}
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("policy") != "orig":
                continue
            workload = row.get("workload")
            if not workload:
                continue
            baselines[workload] = float(row["ipc"])
    return baselines


def infer_policy(name: str):
    lowered = name.lower()
    for prefix in POLICY_PREFIXES:
        if prefix in lowered:
            return prefix
    return "unknown"


def infer_workloads(name: str):
    lowered = name.lower()
    if "4xlbm" in lowered:
        return ["lbm", "lbm", "lbm", "lbm"]
    if "mcf_lbm" in lowered:
        return ["mcf", "lbm"]
    if "mcf_astar" in lowered:
        return ["mcf", "astar"]
    raise ValueError(f"Cannot infer workload mix from filename: {name}")


def parse_controller_lines(text: str):
    lines = []
    for line in text.splitlines():
        if line.startswith(("[SPP_", "[BWC", "[FDP]", "[SIG_UTIL]", "[HEADGATE]")):
            lines.append(line.strip())
    return " | ".join(lines)


def parse_controller_metrics(text: str):
    totals = {
        "coord_mini_epochs": 0,
        "coord_streaming_mini_epochs": 0,
        "coord_selected_boost_windows": 0,
        "coord_phase_owner_windows": 0,
        "coord_dynrunway_keep3_windows": 0,
        "coord_dynrunway_midwide_windows": 0,
        "coord_last_window_miss_latency_delta": 0,
        "bucket_issued_head": 0,
        "bucket_issued_mid": 0,
        "bucket_issued_tail": 0,
        "bucket_fill_head": 0,
        "bucket_fill_mid": 0,
        "bucket_fill_tail": 0,
        "bucket_useful_head": 0,
        "bucket_useful_mid": 0,
        "bucket_useful_tail": 0,
        "bucket_useless_head": 0,
        "bucket_useless_mid": 0,
        "bucket_useless_tail": 0,
        "timely_fill_age0": 0,
        "timely_fill_age1": 0,
        "timely_fill_age2": 0,
        "timely_fill_age3": 0,
        "timely_useful_age0": 0,
        "timely_useful_age1": 0,
        "timely_useful_age2": 0,
        "timely_useful_age3": 0,
        "timely_useless_age0": 0,
        "timely_useless_age1": 0,
        "timely_useless_age2": 0,
        "timely_useless_age3": 0,
    }

    coord_map = {
        "mini_epochs": "coord_mini_epochs",
        "streaming_mini_epochs": "coord_streaming_mini_epochs",
        "selected_boost_windows": "coord_selected_boost_windows",
        "phase_owner_windows": "coord_phase_owner_windows",
        "dynrunway_keep3_windows": "coord_dynrunway_keep3_windows",
        "dynrunway_midwide_windows": "coord_dynrunway_midwide_windows",
        "last_window_miss_latency_delta": "coord_last_window_miss_latency_delta",
    }
    bucket_map = {
        "issued_head": "bucket_issued_head",
        "issued_mid": "bucket_issued_mid",
        "issued_tail": "bucket_issued_tail",
        "fill_head": "bucket_fill_head",
        "fill_mid": "bucket_fill_mid",
        "fill_tail": "bucket_fill_tail",
        "useful_head": "bucket_useful_head",
        "useful_mid": "bucket_useful_mid",
        "useful_tail": "bucket_useful_tail",
        "useless_head": "bucket_useless_head",
        "useless_mid": "bucket_useless_mid",
        "useless_tail": "bucket_useless_tail",
    }
    timely_map = {
        "fill_age0": "timely_fill_age0",
        "fill_age1": "timely_fill_age1",
        "fill_age2": "timely_fill_age2",
        "fill_age3": "timely_fill_age3",
        "useful_age0": "timely_useful_age0",
        "useful_age1": "timely_useful_age1",
        "useful_age2": "timely_useful_age2",
        "useful_age3": "timely_useful_age3",
        "useless_age0": "timely_useless_age0",
        "useless_age1": "timely_useless_age1",
        "useless_age2": "timely_useless_age2",
        "useless_age3": "timely_useless_age3",
    }

    for line in text.splitlines():
        if line.startswith("[BWC_COORD]"):
            mapping = coord_map
        elif line.startswith("[BWC_BUCKETS]"):
            mapping = bucket_map
        elif line.startswith("[BWC_TIMELY]"):
            mapping = timely_map
        else:
            continue

        for key, raw in RE_KEYVAL.findall(line):
            if key == "cpu" or key not in mapping:
                continue
            totals[mapping[key]] += int(raw)

    return totals


def parse_file(path: Path, baselines):
    text = path.read_text(errors="ignore")
    roi_text = text.rsplit("Region of Interest Statistics", 1)[-1]

    ipcs = {}
    max_cycles = 0
    for match in RE_IPC.finditer(roi_text):
        cpu = int(match.group(1))
        ipcs[cpu] = float(match.group(2))
        max_cycles = max(max_cycles, int(match.group(4)))

    workloads = infer_workloads(path.name)
    weighted_speedup = 0.0
    harmonic_denom = 0.0
    for cpu, workload in enumerate(workloads):
        baseline = baselines[workload]
        ipc = ipcs.get(cpu, 0.0)
        weighted_speedup += ipc / baseline
        if ipc > 0:
            harmonic_denom += baseline / ipc

    pf_requested = 0
    pf_issued = 0
    pf_useful = 0
    total_llc_miss = 0
    for match in RE_L2C_PF.finditer(roi_text):
        pf_requested += int(match.group(2))
        pf_issued += int(match.group(3))
        pf_useful += int(match.group(4))
    for match in RE_LLC_MISS.finditer(roi_text):
        total_llc_miss += int(match.group(4))

    row_hit_match = RE_ROW_HIT.search(roi_text)
    row_miss_match = RE_ROW_MISS.search(roi_text)
    dbus_match = RE_DBUS.search(roi_text)

    row_hits = int(row_hit_match.group(1)) if row_hit_match else 0
    row_misses = int(row_miss_match.group(1)) if row_miss_match else 0
    row_hit_rate = (row_hits / (row_hits + row_misses)) if (row_hits + row_misses) else 0.0

    row = {
        "file": path.name,
        "policy": infer_policy(path.name),
        "workloads": "+".join(workloads),
        "num_cores": len(workloads),
        "weighted_speedup": weighted_speedup,
        "harmonic_speedup": (len(workloads) / harmonic_denom) if harmonic_denom else 0.0,
        "max_sim_cycles": max_cycles,
        "total_pf_requested": pf_requested,
        "total_pf_issued": pf_issued,
        "total_pf_useful": pf_useful,
        "pf_accuracy": (pf_useful / pf_issued) if pf_issued else 0.0,
        "dram_avg_dbus": float(dbus_match.group(1)) if dbus_match else 0.0,
        "rq_row_buffer_hit": row_hits,
        "rq_row_buffer_miss": row_misses,
        "row_hit_rate": row_hit_rate,
        "total_llc_miss": total_llc_miss,
        "controller_final": parse_controller_lines(roi_text),
    }
    row.update(parse_controller_metrics(roi_text))

    for cpu in range(len(workloads)):
        row[f"cpu{cpu}_ipc"] = ipcs.get(cpu, 0.0)

    return row


def main():
    parser = argparse.ArgumentParser(description="Extract weighted-speedup summaries from multicore ChampSim logs")
    parser.add_argument("--single-core-baselines", required=True, help="CSV from extract_single_core_metrics.py for the same campaign")
    parser.add_argument("--inputs", nargs="+", required=True, help="Multicore log files")
    parser.add_argument("--out-csv", required=True, help="Output CSV path")
    args = parser.parse_args()

    baseline_csv = resolve_repo_path(args.single_core_baselines)
    baselines = load_single_core_baselines(baseline_csv)
    rows = [parse_file(resolve_repo_path(path), baselines) for path in args.inputs]
    rows.sort(key=lambda row: (row["num_cores"], row["workloads"], row["policy"], row["file"]))

    out_csv = resolve_repo_path(args.out_csv)
    out_csv.parent.mkdir(parents=True, exist_ok=True)

    cpu_columns = sorted({key for row in rows for key in row if key.startswith("cpu") and key.endswith("_ipc")})
    fields = [
        "file",
        "policy",
        "workloads",
        "num_cores",
        *cpu_columns,
        "weighted_speedup",
        "harmonic_speedup",
        "max_sim_cycles",
        "total_pf_requested",
        "total_pf_issued",
        "total_pf_useful",
        "pf_accuracy",
        "dram_avg_dbus",
        "rq_row_buffer_hit",
        "rq_row_buffer_miss",
        "row_hit_rate",
        "total_llc_miss",
        "coord_mini_epochs",
        "coord_streaming_mini_epochs",
        "coord_selected_boost_windows",
        "coord_phase_owner_windows",
        "coord_dynrunway_keep3_windows",
        "coord_dynrunway_midwide_windows",
        "coord_last_window_miss_latency_delta",
        "bucket_issued_head",
        "bucket_issued_mid",
        "bucket_issued_tail",
        "bucket_fill_head",
        "bucket_fill_mid",
        "bucket_fill_tail",
        "bucket_useful_head",
        "bucket_useful_mid",
        "bucket_useful_tail",
        "bucket_useless_head",
        "bucket_useless_mid",
        "bucket_useless_tail",
        "timely_fill_age0",
        "timely_fill_age1",
        "timely_fill_age2",
        "timely_fill_age3",
        "timely_useful_age0",
        "timely_useful_age1",
        "timely_useful_age2",
        "timely_useful_age3",
        "timely_useless_age0",
        "timely_useless_age1",
        "timely_useless_age2",
        "timely_useless_age3",
        "controller_final",
    ]

    with out_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
