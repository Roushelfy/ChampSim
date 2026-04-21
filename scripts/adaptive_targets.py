#!/usr/bin/env python3

from __future__ import annotations


SINGLE_ORIG = {
    "astar": 0.11624407059812944,
    "mcf": 0.09304738191331209,
    "lbm": 0.8705538637792136,
    "bzip2": 2.132624022725237,
}

# Current retained single-core adaptive selector results.
SINGLE_RETAINED = {
    "astar": 0.11769683762267738,
    "mcf": 0.0955303095422819,
    "lbm": 0.8874912094048452,
    "bzip2": 2.150898860454012,
}

PAIR_BASELINES = {
    "astar+lbm": {
        "weighted_speedup": 1.3562017695341675,
        "baseline_ipc": {
            "astar": SINGLE_ORIG["astar"],
            "lbm": SINGLE_ORIG["lbm"],
        },
    },
    "bzip2+lbm": {
        "weighted_speedup": 1.9383916294876093,
        "baseline_ipc": {
            "bzip2": SINGLE_ORIG["bzip2"],
            "lbm": SINGLE_ORIG["lbm"],
        },
    },
    "mcf+lbm": {
        "weighted_speedup": 1.282646900577691,
        "baseline_ipc": {
            "mcf": SINGLE_ORIG["mcf"],
            "lbm": SINGLE_ORIG["lbm"],
        },
    },
}

# Current retained adaptive pair truth that future cycles must preserve.
PAIR_RETAINED = {
    "astar+lbm": {
        "weighted_speedup": 1.3751206958751774,
    },
    "bzip2+lbm": {
        "weighted_speedup": 1.9496149302999837,
    },
    "mcf+lbm": {
        "weighted_speedup": 1.297473125485411,
    },
}


def retention_fraction(value: float, baseline: float, best: float) -> float:
    gain = best - baseline
    if gain == 0:
        return 1.0
    return (value - baseline) / gain


def minimum_acceptable(baseline: float, best: float, retention_target: float) -> float:
    return baseline + retention_target * (best - baseline)


def pair_key(workloads: list[str] | tuple[str, ...]) -> str:
    return "+".join(workloads)


PAIR_CASES = [tuple(key.split("+")) for key in PAIR_BASELINES]
