#!/usr/bin/env python3
import argparse
import csv
import json
import re
from pathlib import Path


RE_IPC = re.compile(r"CPU 0 cumulative IPC:\s*([0-9.]+)\s+instructions:\s*(\d+)\s+cycles:\s*(\d+)")

WORKLOAD_LABELS = {
    "omnetpp": "omnetpp",
    "gemsfdtd": "gemsfdtd",
    "leslie3d": "leslie3d",
    "mcf": "mcf",
    "lbm": "lbm",
    "astar": "astar",
}

POLLUTER_PRIORITY = ["omnetpp", "mcf", "astar"]
VICTIM_PRIORITY = ["gemsfdtd", "leslie3d", "lbm"]


def infer_workload(name: str) -> str:
    lower_name = name.lower()
    for token, workload in WORKLOAD_LABELS.items():
        if token in lower_name:
            return workload
    return "unknown"


def infer_policy(name: str) -> str:
    lower_name = name.lower()
    if "no_pref" in lower_name:
        return "no_pref"
    if "orig" in lower_name:
        return "orig"
    return "unknown"


def parse_ipc(path: Path) -> float:
    text = path.read_text(errors="ignore")
    matches = list(RE_IPC.finditer(text))
    if not matches:
        return 0.0
    return float(matches[-1].group(1))


def load_existing_rows(path: Path):
    if not path.exists():
        return []

    rows = []
    with path.open() as f:
        for row in csv.DictReader(f):
            policy = row.get("policy", "")
            workload = row.get("workload", "")
            if policy not in {"no_pref", "orig"} or workload not in {"mcf", "lbm"}:
                continue
            rows.append(
                {
                    "file": row.get("file", ""),
                    "workload": workload,
                    "policy": policy,
                    "ipc": float(row["ipc"]),
                }
            )
    return rows


def load_screen_rows(log_paths):
    rows = []
    for path in log_paths:
        workload = infer_workload(path.name)
        policy = infer_policy(path.name)
        if workload == "unknown" or policy == "unknown":
            continue
        rows.append(
            {
                "file": path.name,
                "workload": workload,
                "policy": policy,
                "ipc": parse_ipc(path),
            }
        )
    return rows


def finalize_rows(rows):
    by_workload = {}
    for row in rows:
        by_workload.setdefault(row["workload"], {})[row["policy"]] = row

    finalized = []
    for workload, policy_rows in sorted(by_workload.items()):
        no_pref = policy_rows.get("no_pref", {}).get("ipc", 0.0)
        orig = policy_rows.get("orig", {}).get("ipc", 0.0)
        ratio = (orig / no_pref) if no_pref else 0.0

        role = "neutral"
        if no_pref and orig <= no_pref * 1.02:
            role = "polluter_candidate"
        if no_pref and orig >= no_pref * 1.10:
            role = "victim_candidate"

        for policy in ("no_pref", "orig"):
            if policy not in policy_rows:
                continue
            finalized.append(
                {
                    **policy_rows[policy],
                    "ratio_vs_no_pref": ratio if policy == "orig" else 1.0,
                    "role": role if policy == "orig" else "baseline",
                }
            )
    return finalized, by_workload


def choose_workloads(by_workload):
    ratios = {}
    for workload, policy_rows in by_workload.items():
        no_pref = policy_rows.get("no_pref", {}).get("ipc", 0.0)
        orig = policy_rows.get("orig", {}).get("ipc", 0.0)
        ratios[workload] = (orig / no_pref) if no_pref else 0.0

    qualified_polluters = {
        w for w, ratio in ratios.items() if ratio and ratio <= 1.02
    }
    qualified_victims = {
        w for w, ratio in ratios.items() if ratio and ratio >= 1.10
    }

    selected_polluter = next(
        (w for w in POLLUTER_PRIORITY if w in qualified_polluters),
        "mcf",
    )
    selected_victim = next(
        (w for w in VICTIM_PRIORITY if w in qualified_victims),
        "lbm",
    )

    victim_mix = []
    for workload in VICTIM_PRIORITY:
        if workload in qualified_victims or workload == "lbm":
            if workload not in victim_mix:
                victim_mix.append(workload)
        if len(victim_mix) == 3:
            break

    if selected_victim in victim_mix:
        victim_mix.remove(selected_victim)
    victim_mix.insert(0, selected_victim)
    while len(victim_mix) < 3:
        for fallback in VICTIM_PRIORITY:
            if fallback not in victim_mix:
                victim_mix.append(fallback)
                break

    return {
        "selected_polluter": selected_polluter,
        "selected_victim": selected_victim,
        "selected_victims_4core": victim_mix[:3],
        "qualified_polluters": sorted(qualified_polluters),
        "qualified_victims": sorted(qualified_victims),
        "ratios": ratios,
    }


def main():
    parser = argparse.ArgumentParser(description="Screen Phase 1.5 single-core candidates.")
    parser.add_argument("--existing-csv", default="results/single_core_metrics.csv")
    parser.add_argument("--input-dir", default="results/logs/phase15/screening")
    parser.add_argument("--out-csv", default="results/phase15_single_core_screening.csv")
    parser.add_argument("--out-json", default="results/phase15_selection.json")
    args = parser.parse_args()

    existing_rows = load_existing_rows(Path(args.existing_csv))
    input_dir = Path(args.input_dir)
    log_paths = sorted(input_dir.glob("*.txt"))
    screen_rows = load_screen_rows(log_paths)
    finalized_rows, by_workload = finalize_rows(existing_rows + screen_rows)
    selection = choose_workloads(by_workload)

    out_csv = Path(args.out_csv)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["file", "workload", "policy", "ipc", "ratio_vs_no_pref", "role"],
        )
        writer.writeheader()
        writer.writerows(finalized_rows)

    out_json = Path(args.out_json)
    out_json.parent.mkdir(parents=True, exist_ok=True)
    with out_json.open("w") as f:
        json.dump(selection, f, indent=2, sort_keys=True)


if __name__ == "__main__":
    main()
