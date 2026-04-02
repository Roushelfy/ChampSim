#!/usr/bin/env python3
import argparse
import csv
import re
from pathlib import Path


RE_IPC = re.compile(r"CPU 0 cumulative IPC:\s*([0-9.]+)\s+instructions:\s*(\d+)\s+cycles:\s*(\d+)")
RE_L1D = re.compile(r"cpu0->cpu0_L1D TOTAL\s+ACCESS:\s*(\d+)\s+HIT:\s*(\d+)\s+MISS:\s*(\d+)")
RE_L2C = re.compile(r"cpu0->cpu0_L2C TOTAL\s+ACCESS:\s*(\d+)\s+HIT:\s*(\d+)\s+MISS:\s*(\d+)")
RE_LLC = re.compile(r"cpu0->LLC TOTAL\s+ACCESS:\s*(\d+)\s+HIT:\s*(\d+)\s+MISS:\s*(\d+)")
RE_L2C_PF = re.compile(
    r"cpu0->cpu0_L2C PREFETCH REQUESTED:\s*(\d+)\s+ISSUED:\s*(\d+)\s+USEFUL:\s*(\d+)\s+USELESS:\s*(\d+)"
)
RE_DBUS = re.compile(r"AVG DBUS CONGESTED CYCLE:\s*([0-9.]+)")
RE_QUEUE_OCC = re.compile(r"cpu0->cpu0_L2C AVERAGE UPPER RQ OCCUPANCY:\s*([0-9.eE+\-]+)\s+entries\s+\(([0-9.eE+\-]+)% of capacity\)")
RE_DEMAND_STALL = re.compile(r"CPU 0 Demand Stall Cycles:\s*(\d+)\s+\(([0-9.eE+\-]+)% of cycles\)")


WORKLOADS = ["mcf", "lbm", "bzip2"]
POLICIES = ["no_pref", "orig", "fdp", "bwc", "bop"]
CANONICAL_FILES = [
    "no_pref_mcf_50M.txt",
    "orig_mcf_50M.txt",
    "fdp_mcf_50M.txt",
    "bwc2_mcf_50M.txt",
    "bop_mcf_50M.txt",
    "no_pref_lbm_50M.txt",
    "orig_lbm_50M.txt",
    "fdp_lbm_50M.txt",
    "bwc_lbm_50M.txt",
    "bop_lbm_50M.txt",
    "no_pref_bzip2_5M.txt",
    "orig_bzip2_5M.txt",
    "fdp_epoch10k_bzip2.txt",
    "bwc_bzip2_5M.txt",
    "bop_bzip2_5M.txt",
]


def infer_tags(path: Path):
    name = path.name.lower()

    workload = "unknown"
    for w in WORKLOADS:
        if w in name:
            workload = w
            break

    policy = "unknown"
    if "no_pref" in name:
        policy = "no_pref"
    elif "orig" in name or "baseline" in name:
        policy = "orig"
    elif "fdp" in name:
        policy = "fdp"
    elif "bwc" in name:
        policy = "bwc"
    elif "bop" in name:
        policy = "bop"

    return workload, policy


def parse_int(m, idx, default=0):
    return int(m.group(idx)) if m else default


def parse_float(m, idx, default=0.0):
    return float(m.group(idx)) if m else default


def parse_file(path: Path):
    text = path.read_text(errors="ignore")

    ipc_m = RE_IPC.search(text)
    l1d_m = RE_L1D.search(text)
    l2c_m = RE_L2C.search(text)
    llc_m = RE_LLC.search(text)
    pf_m = RE_L2C_PF.search(text)
    dbus_m = RE_DBUS.search(text)
    queue_m = RE_QUEUE_OCC.search(text)
    demand_stall_m = RE_DEMAND_STALL.search(text)

    instructions = parse_int(ipc_m, 2)
    l1d_miss = parse_int(l1d_m, 3)
    l2c_miss = parse_int(l2c_m, 3)
    llc_miss = parse_int(llc_m, 3)

    issued = parse_int(pf_m, 2)
    useful = parse_int(pf_m, 3)

    workload, policy = infer_tags(path)

    return {
        "file": path.name,
        "workload": workload,
        "policy": policy,
        "ipc": parse_float(ipc_m, 1),
        "instructions": instructions,
        "cycles": parse_int(ipc_m, 3),
        "l1d_miss": l1d_miss,
        "l2c_miss": l2c_miss,
        "llc_miss": llc_miss,
        "l1d_mpki": (l1d_miss * 1000.0 / instructions) if instructions else 0.0,
        "l2c_mpki": (l2c_miss * 1000.0 / instructions) if instructions else 0.0,
        "llc_mpki": (llc_miss * 1000.0 / instructions) if instructions else 0.0,
        "pf_requested": parse_int(pf_m, 1),
        "pf_issued": issued,
        "pf_useful": useful,
        "pf_useless": parse_int(pf_m, 4),
        "pf_accuracy": (useful / issued) if issued else 0.0,
        # Coverage proxy: useful prefetches relative to L1D demand misses.
        "pf_coverage_proxy": (useful / l1d_miss) if l1d_miss else 0.0,
        # Bandwidth proxy from ChampSim DRAM summary.
        "offchip_bw_proxy_avg_dbus_congested_cycle": parse_float(dbus_m, 1),
        # Queue occupancy exported from L2C as average upper RQ occupancy (% of capacity).
        "queue_occupancy": (parse_float(queue_m, 2) / 100.0) if queue_m else "NA",
        "demand_stall_cycles": parse_int(demand_stall_m, 1, default="NA") if demand_stall_m else "NA",
    }


def main():
    project_root = Path(__file__).resolve().parents[1]
    default_input_dir = project_root / "results" / "logs" / "single_core"
    default_out_csv = project_root / "results" / "single_core_metrics.csv"
    default_out_gaps = project_root / "results" / "single_core_gaps.md"

    parser = argparse.ArgumentParser(description="Extract single-core metrics from ChampSim logs")
    parser.add_argument("--inputs", nargs="+", help="Optional explicit input ChampSim output files")
    parser.add_argument("--input-dir", default=str(default_input_dir), help="Canonical single-core log directory")
    parser.add_argument("--out-csv", default=str(default_out_csv), help="Output CSV path")
    parser.add_argument("--out-gaps", default=str(default_out_gaps), help="Output gap report path")
    args = parser.parse_args()

    if args.inputs:
        paths = [Path(p) for p in args.inputs]
    else:
        input_dir = Path(args.input_dir)
        paths = [input_dir / f for f in CANONICAL_FILES]

    rows = [parse_file(p) for p in paths if p.exists()]
    workload_order = {w: i for i, w in enumerate(WORKLOADS)}
    policy_order = {p: i for i, p in enumerate(POLICIES)}
    rows.sort(
        key=lambda r: (
            workload_order.get(r["workload"], 999),
            policy_order.get(r["policy"], 999),
            r["file"],
        )
    )

    out_csv = Path(args.out_csv)
    out_csv.parent.mkdir(parents=True, exist_ok=True)

    fields = [
        "file",
        "workload",
        "policy",
        "ipc",
        "instructions",
        "cycles",
        "l1d_miss",
        "l2c_miss",
        "llc_miss",
        "l1d_mpki",
        "l2c_mpki",
        "llc_mpki",
        "pf_requested",
        "pf_issued",
        "pf_useful",
        "pf_useless",
        "pf_accuracy",
        "pf_coverage_proxy",
        "offchip_bw_proxy_avg_dbus_congested_cycle",
        "queue_occupancy",
        "demand_stall_cycles",
    ]

    with out_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    present = {(r["workload"], r["policy"]) for r in rows if r["workload"] != "unknown" and r["policy"] != "unknown"}
    missing = []
    for w in WORKLOADS:
        for p in POLICIES:
            if (w, p) not in present:
                missing.append((w, p))

    gaps_path = Path(args.out_gaps)
    gaps_path.parent.mkdir(parents=True, exist_ok=True)
    with gaps_path.open("w") as f:
        f.write("# Single-Core Coverage Gaps\n\n")
        f.write("## Required matrix\n")
        f.write("- Workloads: mcf, lbm, bzip2\n")
        f.write("- Policies: no_pref, orig, fdp, bwc, bop\n\n")
        if missing:
            f.write("## Missing entries\n")
            for w, p in missing:
                f.write(f"- {w} x {p}\n")
        else:
            f.write("## Missing entries\n- None\n")

        f.write("\n## Notes\n")
        f.write("- queue_occupancy comes from L2C 'AVERAGE UPPER RQ OCCUPANCY' (% of capacity).\n")
        f.write("- demand_stall_cycles comes from CPU 'Demand Stall Cycles'.\n")
        f.write("- If either field is NA for a row, that log was likely generated before this instrumentation was added.\n")
        f.write("- off-chip bandwidth currently uses AVG DBUS CONGESTED CYCLE as a proxy.\n")


if __name__ == "__main__":
    main()