#!/usr/bin/env python3
import argparse
import csv
import re
from pathlib import Path


RE_IPC = re.compile(r"CPU (\d+) cumulative IPC:\s*([0-9.]+)\s+instructions:\s*(\d+)\s+cycles:\s*(\d+)")
RE_L2C_PF = re.compile(
    r"cpu(\d+)->cpu\d+_L2C PREFETCH REQUESTED:\s*(\d+)\s+ISSUED:\s*(\d+)\s+USEFUL:\s*(\d+)\s+USELESS:\s*(\d+)"
)
RE_LLC_TOTAL = re.compile(r"cpu(\d+)->LLC TOTAL\s+ACCESS:\s*(\d+)\s+HIT:\s*(\d+)\s+MISS:\s*(\d+)")
RE_RQ_STATS = re.compile(
    r"Channel 0 RQ ROW_BUFFER_HIT:\s*(\d+)\s*\n\s*ROW_BUFFER_MISS:\s*(\d+)"
)
RE_DBUS = re.compile(r"AVG DBUS CONGESTED CYCLE:\s*([0-9.]+)")
RE_CONTROLLER = re.compile(r"^\[(?:BWC(?:_[A-Z]+)*|FDP|SPP_ORIG|BOP)\].*$", re.MULTILINE)
RE_FAILURE = re.compile(
    r"(Assertion|assert|core dumped|invalid pointer|Aborted|panic:|DEADLOCK!)",
    re.IGNORECASE,
)


def load_single_core_baselines(path: Path):
    baselines = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            if row["policy"] != "orig":
                continue
            baselines[row["workload"]] = float(row["ipc"])
    return baselines


def parse_last_by_cpu(pattern, text, value_keys):
    parsed = {}
    for match in pattern.finditer(text):
        cpu = int(match.group(1))
        parsed[cpu] = {
            key: match.group(idx)
            for idx, key in enumerate(value_keys, start=2)
        }
    return parsed


def parse_log(path: Path, workloads, baselines):
    if not path.exists():
        return {
            "status": "missing",
            "max_sim_cycles": 0,
            "total_pf_issued": 0,
            "total_pf_useful": 0,
            "total_llc_miss": 0,
            "missing_baselines": "",
        }

    text = path.read_text(errors="ignore")
    cpu_stats = parse_last_by_cpu(
        RE_IPC,
        text,
        ["ipc", "instructions", "cycles"],
    )
    pf_stats = parse_last_by_cpu(
        RE_L2C_PF,
        text,
        ["pf_requested", "pf_issued", "pf_useful", "pf_useless"],
    )
    llc_stats = parse_last_by_cpu(
        RE_LLC_TOTAL,
        text,
        ["llc_access", "llc_hit", "llc_miss"],
    )

    row = {
        "status": "complete" if len(cpu_stats) >= len(workloads) else "incomplete",
        "max_sim_cycles": 0,
        "total_pf_issued": 0,
        "total_pf_useful": 0,
        "total_llc_miss": 0,
        "missing_baselines": "",
    }

    if RE_FAILURE.search(text):
        row["status"] = "failed"

    ws_terms = []
    hs_terms = []
    retentions = []
    missing_baselines = []
    for cpu, workload in enumerate(workloads):
        cpu_key = f"cpu{cpu}"
        ipc = float(cpu_stats.get(cpu, {}).get("ipc", 0.0))
        instructions = int(cpu_stats.get(cpu, {}).get("instructions", 0))
        cycles = int(cpu_stats.get(cpu, {}).get("cycles", 0))
        pf_issued = int(pf_stats.get(cpu, {}).get("pf_issued", 0))
        pf_useful = int(pf_stats.get(cpu, {}).get("pf_useful", 0))
        llc_miss = int(llc_stats.get(cpu, {}).get("llc_miss", 0))
        baseline = baselines.get(workload, 0.0)
        if baseline <= 0.0:
            missing_baselines.append(workload)
        retention = (ipc / baseline) if baseline else 0.0
        pf_accuracy = (pf_useful / pf_issued) if pf_issued else 0.0

        row[f"{cpu_key}_workload"] = workload
        row[f"{cpu_key}_ipc"] = ipc
        row[f"{cpu_key}_instructions"] = instructions
        row[f"{cpu_key}_cycles"] = cycles
        row[f"{cpu_key}_pf_issued"] = pf_issued
        row[f"{cpu_key}_pf_useful"] = pf_useful
        row[f"{cpu_key}_pf_accuracy"] = pf_accuracy
        row[f"{cpu_key}_llc_miss"] = llc_miss
        row[f"{cpu_key}_retention_vs_orig"] = retention

        row["max_sim_cycles"] = max(row["max_sim_cycles"], cycles)
        row["total_pf_issued"] += pf_issued
        row["total_pf_useful"] += pf_useful
        row["total_llc_miss"] += llc_miss

        if baseline and ipc:
            ws_terms.append(ipc / baseline)
            hs_terms.append(baseline / ipc)
        retentions.append(retention)

    if row["status"] == "complete" and missing_baselines:
        row["status"] = "baseline_missing"
    row["missing_baselines"] = ";".join(sorted(set(missing_baselines)))

    row["avg_ipc"] = (
        sum(row[f"cpu{cpu}_ipc"] for cpu in range(len(workloads))) / len(workloads)
        if workloads
        else 0.0
    )
    row["weighted_speedup"] = sum(ws_terms)
    row["harmonic_speedup"] = (len(hs_terms) / sum(hs_terms)) if hs_terms else 0.0
    row["pf_accuracy"] = (
        row["total_pf_useful"] / row["total_pf_issued"] if row["total_pf_issued"] else 0.0
    )
    row["victim_min_retention"] = min(retentions[1:], default=0.0)
    row["victim_avg_retention"] = (
        sum(retentions[1:]) / len(retentions[1:]) if len(retentions) > 1 else 0.0
    )
    row["polluter_workload"] = workloads[0] if workloads else ""
    row["polluter_pf_issued"] = row.get("cpu0_pf_issued", 0)
    row["polluter_retention_vs_orig"] = row.get("cpu0_retention_vs_orig", 0.0)

    rq_stats = RE_RQ_STATS.findall(text)
    dbus_values = RE_DBUS.findall(text)
    controller_lines = RE_CONTROLLER.findall(text)
    row["rq_row_buffer_hit"] = int(rq_stats[-1][0]) if rq_stats else 0
    row["rq_row_buffer_miss"] = int(rq_stats[-1][1]) if rq_stats else 0
    row["dram_avg_dbus"] = float(dbus_values[-1]) if dbus_values else 0.0
    row["controller_final"] = " | ".join(controller_lines[-len(workloads):])
    return row


def main():
    parser = argparse.ArgumentParser(description="Extract Phase 1.5 multicore metrics from a manifest.")
    parser.add_argument("--manifest", default="results/logs/phase15/manifest.csv")
    parser.add_argument("--single-core-csv", default="results/phase15_single_core_screening.csv")
    parser.add_argument("--out-csv", default="results/phase15_multicore_metrics.csv")
    parser.add_argument("--run-name-regex", default=None)
    parser.add_argument("--status-filter", default=None, help="Comma-separated statuses to keep, e.g. complete,failed")
    args = parser.parse_args()

    baselines = load_single_core_baselines(Path(args.single_core_csv))
    manifest_path = Path(args.manifest)
    run_name_re = re.compile(args.run_name_regex) if args.run_name_regex else None
    allowed_status = None
    if args.status_filter:
        allowed_status = {token.strip() for token in args.status_filter.split(",") if token.strip()}

    rows = []
    with manifest_path.open() as f:
        for manifest_row in csv.DictReader(f):
            if run_name_re and not run_name_re.search(manifest_row["run_name"]):
                continue
            workloads = [w for w in manifest_row["workloads"].split(";") if w]
            parsed = parse_log(Path(manifest_row["log"]), workloads, baselines)
            if allowed_status and parsed["status"] not in allowed_status:
                continue
            rows.append(
                {
                    **manifest_row,
                    **parsed,
                }
            )

    fieldnames = [
        "run_name",
        "policy",
        "policy_family",
        "variant",
        "candidate_id",
        "split",
        "scenario",
        "setting",
        "stable_run",
        "config",
        "binary",
        "log",
        "warmup_instructions",
        "simulation_instructions",
        "workloads",
        "traces",
        "status",
        "missing_baselines",
        "avg_ipc",
        "weighted_speedup",
        "harmonic_speedup",
        "max_sim_cycles",
        "total_pf_issued",
        "total_pf_useful",
        "pf_accuracy",
        "dram_avg_dbus",
        "rq_row_buffer_hit",
        "rq_row_buffer_miss",
        "total_llc_miss",
        "victim_min_retention",
        "victim_avg_retention",
        "polluter_workload",
        "polluter_pf_issued",
        "polluter_retention_vs_orig",
        "cpu0_workload",
        "cpu0_ipc",
        "cpu0_pf_issued",
        "cpu0_pf_useful",
        "cpu0_pf_accuracy",
        "cpu0_retention_vs_orig",
        "cpu1_workload",
        "cpu1_ipc",
        "cpu1_pf_issued",
        "cpu1_pf_useful",
        "cpu1_pf_accuracy",
        "cpu1_retention_vs_orig",
        "cpu2_workload",
        "cpu2_ipc",
        "cpu2_pf_issued",
        "cpu2_pf_useful",
        "cpu2_pf_accuracy",
        "cpu2_retention_vs_orig",
        "cpu3_workload",
        "cpu3_ipc",
        "cpu3_pf_issued",
        "cpu3_pf_useful",
        "cpu3_pf_accuracy",
        "cpu3_retention_vs_orig",
        "controller_final",
    ]

    out_csv = Path(args.out_csv)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
