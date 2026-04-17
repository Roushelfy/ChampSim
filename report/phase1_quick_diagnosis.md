# Phase 1 Quick Diagnosis
## Short-Window Exploratory Rerun (`1M` warmup / `5M` simulation)

This note is a quick-scale restart of the Phase 1 analysis. It does **not** replace the longer `50M` / `20M` results in [phase1.md](/Users/shinkijun/Developers/ChampSim/report/phase1.md). The goal here is narrower:

- add `473.astar` as a **single-core-only** midpoint workload,
- rerun only the **existing WS mixes** (`mcf+lbm`, `4×lbm`) at short scale,
- quantify where the current logic fails before implementing a new controller.

All new outputs for this pass live under `results/quick/`.

## Quick Experiment Setup

- Window: `1M` warmup + `5M` simulation for every run.
- Phase A single-core matrix:
  - `mcf`: `orig`, `fdp`, `bwc`
  - `lbm`: `orig`, `fdp`, `bwc`
  - `bzip2`: `orig`
  - `astar`: `no_pref`, `orig`, `fdp`, `bwc`, `bop`
- Phase B WS mix:
  - `mcf+lbm`: `orig`, `fdp`, `bwc`
- Phase C WS mix:
  - `4×lbm`: `orig`, `fdp`, `bwc`

Primary quick outputs:

- [results/quick/phase_a_single_core_metrics.csv](/Users/shinkijun/Developers/ChampSim/results/quick/phase_a_single_core_metrics.csv)
- [results/quick/phase_b_mcf_lbm_2core_metrics.csv](/Users/shinkijun/Developers/ChampSim/results/quick/phase_b_mcf_lbm_2core_metrics.csv)
- [results/quick/phase_c_lbm_4core_metrics.csv](/Users/shinkijun/Developers/ChampSim/results/quick/phase_c_lbm_4core_metrics.csv)
- [results/quick/phase_strategy_gsp_metrics.csv](/Users/shinkijun/Developers/ChampSim/results/quick/phase_strategy_gsp_metrics.csv)

## `astar` Single-Core Classification

### Anchor comparison (`orig` only)

| Workload | IPC | PF Accuracy | Demand Stall % | L1D MPKI | L2C MPKI | LLC MPKI | L1D Lat | L2C Lat | LLC Lat | DBUS | Row-Hit Rate |
|----------|-----|-------------|----------------|----------|----------|----------|---------|---------|---------|------|--------------|
| `mcf` | 0.09268 | 0.0178% | 95.91 | 156.43 | 86.45 | 74.97 | 160.7 | 191.8 | 216.6 | 7.601 | 0.142% |
| `astar` | 0.1162 | 3.78% | 94.95 | 97.13 | 88.62 | 71.83 | 280.7 | 332.5 | 348.5 | 9.062 | 2.604% |
| `lbm` | 0.8688 | 4.88% | 78.54 | 46.94 | 82.38 | 49.70 | 314.0 | 536.8 | 627.4 | 7.460 | 6.292% |
| `bzip2` | 2.144 | 5.38% | 26.96 | 2.71 | 5.71 | 2.34 | 83.92 | 132.9 | 156.6 | 2.918 | 14.884% |

### `astar` policy sweep

| Policy | IPC | Delta vs `orig` | PF Accuracy | Demand Stall % | L1D Lat | L2C Lat | LLC Lat | DBUS | Row-Hit Rate | PF Issued |
|--------|-----|-----------------|-------------|----------------|---------|---------|---------|------|--------------|-----------|
| `no_pref` | 0.1138 | -2.07% | 0.00% | 95.07 | 290.4 | 336.1 | 339.2 | 9.253 | 2.473% | 0 |
| `orig` | 0.1162 | baseline | 3.78% | 94.95 | 280.7 | 332.5 | 348.5 | 9.062 | 2.604% | 286,756 |
| `fdp` | 0.1145 | -1.46% | 9.05% | 95.03 | 286.1 | 337.3 | 345.6 | 9.187 | 2.651% | 124,807 |
| `bwc` | 0.1146 | -1.38% | 7.09% | 95.03 | 287.2 | 335.9 | 345.0 | 9.308 | 2.666% | 81,728 |
| `bop` | 0.1169 | +0.60% | 0.00% | 94.92 | 279.5 | 323.0 | 351.7 | 8.678 | 2.397% | 97,724 |

`astar` is therefore a real midpoint workload, not a duplicate of the existing anchors:

- It is **memory-stall dominated like `mcf`**: `94.95%` demand-stall vs `95.91%` for `mcf`.
- It has **materially more prefetch utility than `mcf`**: `3.78%` PF accuracy vs `0.0178%`.
- It is **far less streaming than `lbm`**: row-hit rate `2.60%` vs `6.29%`, and IPC is still two orders of magnitude lower than `bzip2`.
- Within the current SPP family, `orig` remains the best operating point. Both `fdp` and `bwc` improve measured accuracy, but both still reduce IPC and slightly raise DBUS. This is the same qualitative failure mode as the long-window result.

## `mcf+lbm` 2-Core Quick WS

| Policy | `mcf` IPC | `lbm` IPC | WS | Delta vs `orig` | Global DBUS | Row-Hit Rate | Final Controller State |
|--------|-----------|-----------|----|-----------------|-------------|--------------|------------------------|
| `orig` | 0.03248 | 0.8144 | 1.2878 | baseline | 7.344 | 7.401% | `orig`, `orig` |
| `fdp` | 0.04728 | 0.6191 | 1.2227 | -5.06% | 7.363 | 4.644% | `L1`, `L1` |
| `bwc` | 0.03181 | 0.8192 | 1.2861 | -0.13% | 7.420 | 7.523% | `mcf -> L1 / issue_period=4`, `lbm -> L3 / issue_period=1` |

Key quantitative observations:

- `FDP` helps `mcf` IPC by `+45.6%`, but it does so by **crushing `lbm`**:
  - `lbm` IPC: `0.8144 -> 0.6191` (`-24.0%`)
  - `lbm` PF issued: `7,866,320 -> 653,528` (`-91.7%`)
  - Result: WS drops by `-5.06%`
- `BWC` protects `lbm` but still does not move WS:
  - `mcf` PF issued: `515,899 -> 131,805` (`-74.5%`)
  - `lbm` PF issued: `7,866,320 -> 8,096,492` (`+2.93%`)
  - `mcf` IPC: `0.03248 -> 0.03181` (`-2.06%`)
  - `lbm` IPC: `0.8144 -> 0.8192` (`+0.59%`)
  - Result: WS is effectively flat (`-0.13%`)

This directly answers the main Phase 1 question for the heterogeneous mix:

- `FDP` improves the wrong core too aggressively.
- `BWC` identifies the polluter better than `FDP`, but the **measured bandwidth relief does not translate into `mcf` speedup**.

## `4×lbm` 4-Core Quick WS

| Policy | WS | Delta vs `orig` | Global DBUS | Row-Hit Rate | Total PF Issued | Final Levels |
|--------|----|-----------------|-------------|--------------|-----------------|--------------|
| `orig` | 1.0632 | baseline | 4.468 | 4.766% | 1,454,031 | all `L3` |
| `fdp` | 1.0578 | -0.51% | 5.807 | 5.606% | 226,961 | all `L1` |
| `bwc` | 1.0661 | +0.27% | 4.462 | 4.742% | 1,453,328 | all `L3`, `issue_period=1` |

This is still a no-op in the sense that matters architecturally:

- `BWC` leaves the machine essentially in the unthrottled state:
  - all four cores remain at `Level 3`
  - all four cores keep `issue_period=1`
  - total PF issued changes by only `-0.05%`
  - DBUS is unchanged (`4.468 -> 4.462`)
- `FDP` throttles hard, but still hurts WS (`-0.51%`)

The pressure summaries explain why `BWC` never reacts:

- `avg_epoch_mshr_util`: `0.3639 - 0.3659`
- `max_epoch_mshr_util`: `0.5`
- `avg_epoch_llc_rq_util`: `0.0032 - 0.0036`
- `max_epoch_llc_rq_util`: `0.21875 - 0.234375`
- `frac_epoch_*_ge_thresh`: all `0`

Those values stay far below the current BWC throttle thresholds:

- `BWC_THROTTLE_MSHR = 0.85`
- `BWC_THROTTLE_LLC_RQ = 0.80`

So under `4×lbm`, the current “bandwidth-aware” path sees no reason to move at all.

## Quantitative Diagnosis

### 1. Where does `astar` sit?

`astar` is the right midpoint for this project.

- It is not compute-bound like `bzip2`.
- It is not as prefetch-hostile as `mcf`.
- It is still much more latency-sensitive than `lbm`.

The short-window data says `astar` is best described as a **memory-stall-heavy intermediate workload with some real prefetch utility, but not enough robustness to survive extra throttling**.

### 2. Why does `mcf+lbm` WS barely improve?

The bottleneck is not “too many prefetches” by itself. It is **who gets throttled, and whether the reduced traffic actually lowers effective demand latency for `mcf`**.

- `FDP` mostly behaves like accuracy throttling.
  - It leaves `mcf` PF volume almost unchanged (`+0.31%`) because `mcf` still produces high-confidence patterns.
  - It over-throttles `lbm`, which is the useful streaming workload.
  - This improves `mcf` but hurts `lbm` much more, so WS drops.
- `BWC` behaves better than `FDP` in this heterogeneous case, but the gain stalls out.
  - It cuts `mcf` PF volume by `74.5%`.
  - It preserves `lbm`.
  - Yet `mcf` IPC still slightly regresses.

The single-core quick runs explain that last point:

- `mcf` under both `FDP` and `BWC` sees:
  - `IPC` down by about `2.37%`
  - `DBUS` up by `6.3-6.8%`
  - row-hit rate down by `28-30%`
  - L1D/L2C miss latency up (`160.7/191.8 -> 167.9/200.9` for `BWC`)

So even in the short window, the current throttles are removing traffic in a way that **hurts effective memory latency**. The controller can suppress `mcf` prefetches, but it still does not understand which prefetches are accidentally helping the memory system.

### 3. Why is `4×lbm` basically a no-op for BWC?

Because the current BWC decision path does not actually see enough pressure to fire.

The core update rule in [spp_bwc.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_bwc/spp_bwc.cc:251) samples:

- `intern_->get_mshr_occupancy_ratio()`
- shared LLC RQ occupancy
- prefetch accuracy

and throttles only if:

- `llc_rq_util > 0.80`, or
- `mshr_util > 0.85`, or
- `accuracy < 1%`

In `4×lbm`, accuracy is not that low, and the pressure signals never approach the thresholds. So every core stays at `L3`.

This means the current logic misses **symmetric, system-wide saturation** even though that is exactly the case where a bandwidth-aware controller should matter most.

### 4. What is wrong with the current logic?

The core problem is:

> The current controller still relies on **utility-blind accuracy fallback** to act in heterogeneous mixes, while its **per-core end-of-epoch pressure signals stay far below threshold in symmetric saturation**, so it neither preserves the right prefetches nor detects the right contention mode.

Two concrete consequences follow:

- In `mcf+lbm`, the useful action comes mostly from the `accuracy < 1%` escape hatch on the `mcf` side, not from measured queue pressure.
- In `4×lbm`, pressure never crosses the chosen thresholds, so the “bandwidth-aware” path never engages at all.

## Current Logic Problems

- `confidence` and `accuracy` are still poor proxies for **utility**.
  - `astar` shows this clearly: `fdp` and `bwc` raise PF accuracy, but IPC still falls.
- The current pressure sensors are too **local**, too **thresholded**, and too **late**.
  - In `4×lbm`, all average and max pressure summaries stay below threshold, so no control action occurs.
- The current actuator can remove PF traffic without producing the desired latency relief.
  - `mcf` under `BWC` is the clearest example: PF volume collapses, but IPC does not improve.

## Next Strategy Candidates

The next step should still be new control logic, but not by retuning the same rule set. The strongest candidates remain:

1. **Global / shared-resource-aware throttling**
   - Make the controller optimize system outcome rather than per-core local heuristics.
   - Relevant references:
     - [HPAC (MICRO 2009)](https://hps.ece.utexas.edu/pub/ebrahimi_micro09.pdf)
     - [Prefetch-Aware Shared Resource Management (ISCA 2011)](https://people.inf.ethz.ch/omutlu/pub/prefetchaware-shared-resources_isca11.pdf)
2. **Demand-vs-prefetch scheduling at LLC / memory controller**
   - Target the actual interference point instead of only suppressing generation.
   - Relevant reference:
     - [Prefetch-Aware DRAM Controller (MICRO 2008)](https://people.inf.ethz.ch/omutlu/pub/prefetch-dram_micro08.pdf)
3. **Utility filtering beyond confidence**
   - Keep potentially useful coverage while rejecting traffic that hurts shared performance.
   - Relevant reference:
     - [Perceptron-Based Prefetch Filtering (ISCA 2019)](https://people.engr.tamu.edu/djimenez/pdfs/ppf_isca2019.pdf)

## Strategy Exploration And One Implemented Variant

The diagnosis above points to **two different failure modes**, so the strategy search also split into two tracks:

1. **Symmetric shared-pressure blind spot**
   - Relevant strategy family:
     - [HPAC (MICRO 2009)](https://hps.ece.utexas.edu/pub/ebrahimi_micro09.pdf)
     - [Prefetch-Aware Shared Resource Management (ISCA 2011)](https://people.inf.ethz.ch/omutlu/pub/prefetchaware-shared-resources_isca11.pdf)
   - Why it fits:
     - The current BWC never reacts in `4×lbm` because no single core crosses the local threshold, even though the system is collectively MSHR-busy.
   - What it suggests architecturally:
     - use a **global interference signal** or a shared-resource-aware throttle, not only a per-core queue threshold.

2. **Latency / row-buffer help vs. pure traffic suppression**
   - Relevant strategy family:
     - [Prefetch-Aware DRAM Controller (MICRO 2008)](https://people.inf.ethz.ch/omutlu/pub/prefetch-dram_micro08.pdf)
   - Why it fits:
     - `mcf` does not improve just because PF volume falls; what matters is whether demand latency and row-buffer behavior actually improve.
   - What it suggests architecturally:
     - schedule or drop prefetches closer to the LLC/MC interference point instead of only suppressing generation at SPP.

3. **Utility filtering beyond confidence / accuracy**
   - Relevant strategy family:
     - [Perceptron-Based Prefetch Filtering (ISCA 2019)](https://people.engr.tamu.edu/djimenez/pdfs/ppf_isca2019.pdf)
   - Why it fits:
     - `astar` and `mcf` both show that higher measured accuracy does not guarantee higher IPC.
   - What it suggests architecturally:
     - separate **utility** from raw confidence and reject prefetches that hurt shared performance even if they look locally accurate.

### Implemented candidate: Global Symmetric-Pressure (GSP) issue floor

For this pass, I implemented the most direct fix for the `4×lbm` blind spot inside [spp_bwc.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_bwc/spp_bwc.cc) / [spp_bwc.h](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_bwc/spp_bwc.h):

- each BWC instance now publishes its last epoch's `accuracy`, `MSHR util`, and `LLC RQ util` into shared per-process state;
- each epoch, the controller computes a **global average / max pressure snapshot** across active cores;
- if the run looks like a **4-core symmetric pressure regime**:
  - `active_cores >= 4`
  - no core is in the `accuracy < 1%` escape-hatch case
  - global average `MSHR util >= 0.30`
- then BWC keeps its normal level logic but forces a conservative **global issue floor**:
  - `issue_period = max(issue_period, 2)`

This is intentionally a small, measurement-friendly implementation:

- it changes only the actuator, not the SPP confidence / fill thresholds;
- it is scoped to the symmetric many-core case so it does not intentionally retune the heterogeneous `mcf+lbm` path;
- it logs `symmetric_mode_epochs` and the observed global pressure maxima so we can verify whether the new mode really engaged.

An earlier draft that allowed the mode at `>= 2` active cores overreached and quickly made `mcf+lbm` much slower, so the final version was tightened to `>= 4` cores. That is itself a useful exploration result: **a global throttle can detect the right regime, but if it is not carefully scoped it will spill into heterogeneous mixes and hurt the already-fragile `mcf` case**.

### Implemented result: `4×lbm`

| Policy | WS | Delta vs `orig` | Delta vs old `bwc` | DBUS | Row-Hit Rate | Total PF Issued | Final Action |
|--------|----|-----------------|--------------------|------|--------------|-----------------|--------------|
| `orig` | 1.0632 | baseline | -0.27% | 4.468 | 4.766% | 1,454,031 | no throttling |
| old `bwc` | 1.0661 | +0.27% | baseline | 4.462 | 4.742% | 1,453,328 | all `L3`, `issue_period=1` |
| `bwc_gsp` | 1.0634 | +0.02% | -0.25% | 4.497 | 4.837% | 978,726 | all `L3`, `issue_period=2` |

What changed with `bwc_gsp`:

- The old blind spot is genuinely fixed at the **sensing / actuation trigger** level.
  - old `bwc`: all four cores stayed at `issue_period=1`
  - `bwc_gsp`: all four cores finished at `issue_period=2`
  - `symmetric_mode_epochs`: `69-71` epochs per core
- Prefetch traffic dropped materially.
  - total PF issued: `1,453,328 -> 978,726` (`-32.7%`)
- The new global counters confirm the symmetric condition was visible even though the old local thresholds still did not fire.
  - max global average `MSHR util`: about `0.477-0.484`
  - max global average `LLC RQ util`: about `0.055-0.074`

But the performance result is weak:

- WS is only `+0.02%` vs `orig`
- and `-0.25%` vs the old `bwc` quick result
- DBUS is slightly **higher** than both `orig` and old `bwc`

So the implemented strategy successfully answers one question but fails another:

- **Yes**, the current BWC can be taught to notice the symmetric saturation regime.
- **No**, a uniform global issue floor by itself is not enough to convert that detection into clear WS gain.

### What the implementation result means

This implementation narrows the design space for Phase 2:

- A pure **global rate limiter** is too blunt.
  - It can reduce traffic, but it does not know which prefetches are worth keeping.
- The next useful step is probably **not** more threshold tuning on the same actuator.
  - The `4×lbm` result says the sensor was part of the problem, but not the whole problem.
- The strongest follow-up candidates are now:
  - shared-resource-aware throttling that distinguishes interfering vs. victim cores, not just “all cores issue less”;
  - demand-vs-prefetch scheduling at the LLC / memory controller;
  - utility filtering that preserves timely streaming prefetches while rejecting low-value traffic.

In other words, the exploration result is:

> The current logic's blind spot in symmetric saturation is real and fixable, but simply forcing a global issue floor does not deliver enough WS benefit. The next controller needs **better utility discrimination**, not just better pressure detection.

## Scale Sensitivity Note

These quick results are for diagnosis only.

- For `mcf+lbm`, the absolute WS values are about `+2.6%` higher than the long-window numbers, but the **ordering is unchanged**: `orig ≈ bwc > fdp`.
- For `4×lbm`, the absolute WS values are about `1.4-1.8%` lower than the long-window numbers, but the **qualitative conclusion is unchanged**: `fdp` is worse, and `bwc` remains effectively the same as `orig`.
- For single-core trends, the short-window rerun preserves the same directional conclusions:
  - `mcf`: `orig > fdp ≈ bwc`
  - `lbm`: `orig ≈ bwc >> fdp`
  - `astar`: `orig > fdp ≈ bwc`

So this quick pass is good enough to diagnose the current logic, but not to replace the final-scale results in `phase1.md`.
