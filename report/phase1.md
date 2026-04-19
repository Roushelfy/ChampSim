# Phase 1 Progress Report — Syncup #1
## OpenEvolve-Optimized SPP Controller Evolution in ChampSim
### 15-740 Computer Architecture · Spring 2026

---

## 0. April 17 Scope Update

The project has since narrowed to a cleaner mainline:

- final-report scope is now **SPP only**
- `bwc_local` remains the hand-designed baseline
- `gsp_tiered_seed` is the OpenEvolve seed family
- the temporary local OpenEvolve-compatible harness has been retired from the main workflow
- the official OpenEvolve repo is now the only search entrypoint

So this document should now be read as the historical Phase 1 diagnosis that motivated the
controller-evolution direction, not as the final execution methodology.

---

## 1. Project Overview & Motivation

Modern hardware prefetchers dramatically improve cache hit rates for regular, strided workloads,
but they degrade performance under memory bandwidth pressure by evicting useful lines and
monopolizing DRAM bandwidth with inaccurate requests. The dominant industrial response —
**Feedback-Directed Prefetching (FDP)** (Srinath et al., HPCA 2007) — throttles aggressiveness
based on **accuracy** (useful prefetches / issued prefetches) using statically hand-tuned
confidence thresholds.

Our thesis is that **accuracy is a lagging, indirect signal**. By the time accuracy degrades,
bandwidth damage has already been done. More critically, accuracy cannot distinguish between
*demand-serving capacity* and *wasted bandwidth*: a prefetcher with 0.01% accuracy may still
be providing meaningful DRAM row-buffer warm-up effects that benefit latency even as it saturates
the memory controller queue.

**Our goal**: replace hand-tuned accuracy thresholds with an **OpenEvolve-optimized
bandwidth-aware controller** that observes direct microarchitectural pressure signals —
L2C/LLC MSHR occupancy and DRAM Read Queue (RQ) fill level — and uses an LLM-evolved policy
function to select throttle levels in real time.

Phase 1 (Weeks 1–2) establishes the infrastructure and FDP baseline required to demonstrate
this gap. The mcf experimental results below provide compelling empirical evidence that
accuracy-based throttling is fundamentally insufficient.

---

## 2. Engineering Achievements (Weeks 1–2)

### 2.1 ChampSim Infrastructure Setup

- Cloned ChampSim from the upstream GitHub repository and resolved vcpkg submodule
  initialization on WSL2 Ubuntu 22.04.
- Configured a single-core baseline using the `spp_dev` prefetcher at L2C via a custom
  `spp_config.json`. Verified end-to-end simulation with a 401.bzip2 trace (1M warmup +
  5M simulation instructions).
- Confirmed trace availability: `401.bzip2-226B.champsimtrace.xz` (751 MB) and
  `429.mcf-51B.champsimtrace.xz` (1.3 GB) from the DPC-3 public trace repository.

### 2.2 FDP Static-Threshold Controller (spp_dev.cc / spp_dev.h)

Implemented a complete FDP controller embedded within the existing `spp_dev` prefetcher module,
with the following key design decisions:

#### Epoch-Based Feedback Tracking

```
FDP_EPOCH_SIZE = 500   (demand LOAD accesses to L2C triggering prefetcher_cache_operate)
FDP_ACC_HIGH   = 0.80  (accuracy → increment aggressiveness level)
FDP_ACC_LOW    = 0.50  (accuracy → decrement aggressiveness level)
```

Per-epoch counters (`fdp_epoch_pf_issued`, `fdp_epoch_pf_useful`) accumulate across all
SPP-issued prefetches (both LLC-fill and L2C-fill candidates), not just those hitting in
the demand filter. The epoch fires when `fdp_access_count` reaches `FDP_EPOCH_SIZE` inside
`prefetcher_cache_operate`.

**Critical calibration insight**: ChampSim's `should_activate_prefetcher()` in `cache.cc`
gates `prefetcher_cache_operate` to demand LOADs only — SPP's own fill traffic does **not**
trigger the callback. For bzip2 5M (simulation phase), only ~1,875 demand calls occur.
`FDP_EPOCH_SIZE = 500` was chosen to give ~3–4 epochs for functional verification.

#### 5-Level Aggressiveness Mapping

| Level | `pf_threshold` | `fill_threshold` | Notes |
|-------|---------------|-----------------|-------|
| 1     | 80            | 90              | Most conservative |
| 2     | 60            | 90              | |
| 3     | 25            | 90              | **Default (SPP original)** |
| 4     | 15            | 75              | |
| 5     | 5             | 50              | Most aggressive |

#### Robust C++ Engineering

Three notable implementation decisions prevent subtle correctness bugs:

1. **Zero-issue epoch guard**: `if (fdp_epoch_pf_issued == 0) return;` prevents a silent
   epoch with no prefetches from being treated as 100% accuracy and falsely incrementing
   the level.

2. **Per-instance state (no multi-core bleed)**: All FDP fields (`fdp_level`,
   `fdp_epoch_pf_issued`, `fill_threshold`, `pf_threshold`) are declared as non-static
   instance members of `spp_dev`. Each L2C cache object owns its own copy. In multicore
   simulations, cores do not share throttle state.

3. **Zero-overhead dual-binary evaluation via `if constexpr`**:
   ```cpp
   // spp_dev.h
   static constexpr bool FDP_ENABLED = true;  // false → baseline binary

   // spp_dev.cc
   void spp_dev::fdp_update_epoch() {
     if constexpr (!FDP_ENABLED) return;  // compiled out entirely
     ...
   }
   ```
   Two separate binaries (`champsim_baseline`, `champsim_fdp`) are built from the same
   source by toggling this flag, eliminating any instrumentation overhead or code divergence
   between experimental conditions.

4. **Parameter-passing fix**: `pf_threshold` is passed by value to `PATTERN_TABLE::read_pattern()`
   to avoid a dangling `_parent->pf_threshold` read through a nested class pointer, which
   would be undefined behavior if the parent is moved.

---

## 3. Experimental Methodology & Results

### 3.1 Functional Verification — 401.bzip2 (5M Instructions)

**Purpose**: Confirm FDP epochs fire and correctly throttle a compute-bound workload with
low SPP accuracy.

**Configuration**: 1M warmup + 5M simulation, `FDP_EPOCH_SIZE = 500`.

**FDP Epoch Trace** (captured from runtime debug output):

| Epoch | Issued | Useful | Accuracy | Level Before → After |
|-------|--------|--------|----------|-----------------------|
| 1     | 3,714  | 275    | 7.4%     | 3 → 2                 |
| 2     | 2,520  | 369    | 14.6%    | 2 → 1                 |
| 3     | 959    | 367    | 38.3%    | 1 → 1 (clamped)       |
| 4     | 1,059  | 369    | 34.8%    | 1 → 1 (clamped)       |

**Result**: `[FDP] Final level=1  pf_threshold=80  fill_threshold=90`

The controller correctly identified bzip2's consistently low SPP accuracy (well below
`ACC_LOW = 0.50`) and ratcheted aggressiveness from the default Level 3 down to the minimum
Level 1 within two epochs. Functional verification **passed**.

### 3.2 Architectural Evaluation — 429.mcf (50M Instructions)

**Purpose**: Measure the performance impact of FDP throttling on a canonical
memory-bandwidth-intensive workload (minimum-cost flow solver; SPEC CPU2006).

**Configuration**: 10M warmup + 50M simulation, `FDP_EPOCH_SIZE = 500`.
Two separate binaries from identical source; baseline has `FDP_ENABLED = false` (thresholds
permanently frozen at Level 3 defaults).

| Metric | Baseline (SPP, no FDP) | FDP (SPP + FDP, Level 1) | Δ |
|--------|------------------------|--------------------------|---|
| **IPC** | **0.1212** | **0.1165** | **−3.88%** |
| Simulation Cycles | 412,479,890 | 429,241,406 | +4.06% |
| L2C Prefetch Issued | 4,739,040 | 4,728,524 | −0.22% |
| L2C Prefetch Useful | 690 | 783 | +13.5% |
| L2C Prefetch Accuracy | 0.0146% | 0.0166% | +0.002pp |
| L2C Total Accesses | 12,204,884 | 12,194,770 | −0.08% |
| LLC Total Accesses | 6,503,027 | 5,173,042 | **−20.4%** |
| LLC Total Misses | 4,385,561 | 3,680,657 | −16.1% |
| DRAM RQ Row-Buffer Hit | 6,802 | 3,769 | **−44.6%** |
| DRAM RQ Row-Buffer Miss | 4,334,790 | 3,669,815 | −15.3% |
| FDP Final Level | 3 (frozen) | 1 | — |

---

## 4. Key Insights & Architectural Pathologies

### 4.1 Why Did IPC Drop Despite Near-Zero Accuracy? (The Row-Buffer Warm-Up Effect)

At 0.0146% accuracy, virtually every prefetch SPP issues for mcf is "wasted" from a cache
reuse perspective — it fetches a line that no demand request will consume before eviction.
Yet removing or throttling these prefetches **hurts** performance by 3.88%.

The mechanism is revealed by the DRAM row-buffer hit statistics. Baseline SPP issues
4.74 million prefetches with coherent stride patterns that happen to exhibit *locality in
DRAM address space*, consistently accessing the same DRAM rows as subsequent demand misses.
This primes the DRAM row buffer, converting row-buffer misses to hits for demand traffic.

When FDP throttles to Level 1 (`pf_threshold = 80`), the DRAM RQ Row-Buffer Hit count drops
from **6,802 to 3,769 (−44.6%)**. Even though fewer total DRAM transactions are issued
(3,669,815 vs. 4,334,790 row-buffer misses), the *effective latency per demand miss increases*
because row-buffer warm-up is lost. mcf's IPC is so low (~0.12) and so entirely determined by
DRAM latency that even a modest increase in average DRAM latency translates directly to a
measurable cycle count increase.

This is a well-characterized but underappreciated phenomenon: **prefetcher accuracy at the
cache level does not capture memory-level parallelism overlap or DRAM row-buffer warm-up
effects**. A prefetch can be "useless" by every cache metric and still improve IPC by
structuring DRAM access patterns.

### 4.2 Why Did FDP Fail to Reduce Bandwidth? (Confidence ≠ Usefulness for mcf)

FDP's throttle mechanism raises the minimum confidence threshold (`pf_threshold: 25 → 80`
at Level 1), implicitly assuming that low-confidence prefetches are the inaccurate ones.
For mcf, this assumption fails catastrophically.

Even at `pf_threshold = 80`, SPP still issues **4,728,524 prefetches** — only 10,516 fewer
(−0.22%) than baseline. The root cause: SPP's confidence model measures *pattern consistency*
in its Signature and Pattern Tables, not *demand reuse likelihood*. mcf's minimum-cost flow
kernel traverses a large, sparse graph with consistent but widely-strided pointer chains.
These strides produce stable, high-confidence SPP signatures despite no subsequent demand reuse.

The confidence score is high because the *pattern is regular*; the accuracy is near zero because
the *accessed data is not reused*. These two properties are orthogonal, but FDP's static
threshold conflates them.

### 4.3 The Fundamental Insufficiency of Accuracy-Based Throttling

These two pathologies expose a structural limitation of accuracy-based throttling:

1. **Signal lag**: Accuracy is retrospective. It reports how many past prefetches were useful
   but cannot predict whether the current memory controller queue is saturated or whether
   bandwidth reduction would benefit demand traffic.

2. **Wrong abstraction**: The relevant resource for memory-bound workloads is **DRAM bandwidth
   and MSHR occupancy**, not prefetch accuracy. A controller observing accuracy but not queue
   depth will misallocate bandwidth in both directions.

3. **Confidence ≠ usefulness**: SPP confidence measures pattern consistency, not demand reuse.
   Confidence thresholds cannot filter high-confidence-but-useless prefetches that dominate
   bandwidth on irregular memory-bound workloads.

Collectively, these observations constitute a **highly valuable negative result**: they provide
direct empirical evidence — not merely theoretical argument — that accuracy-based throttling is
insufficient for bandwidth contention scenarios, and that direct observation of DRAM and MSHR
pressure signals is required. This is precisely the gap our bandwidth-aware controller targets.

---

## 5. Current Mainline After Phase 1.5

### 5.1 What Changed

Phase 1.5 and the subsequent code merge reshaped the project in three important ways:

1. The practical comparison set converged to `orig / fdp / bwc_local / gsp_tiered_seed`.
2. The best diagnostic mixes became heterogeneous and near-symmetric multicore mixes rather
   than a single single-core pathology.
3. The OpenEvolve path migrated from a local bootstrap harness to the official
   `algorithmicsuperintelligence/openevolve` repository.

### 5.2 Current Experimental Story

The main narrative now is:

- **Phase 1**: accuracy-based throttling is insufficient; mcf exposes why.
- **Phase 1.5**: hand-designed BWC can help on some mixes but remains brittle.
- **Current step**: evolve the `gsp_tiered_seed` controller family with official OpenEvolve
  while keeping predictor logic fixed.

The search space is deliberately narrow: global-pressure thresholds, tier floors,
hysteresis, and minimum-active-core gating.

### 5.3 Immediate Next Steps

- Freeze official nominal baselines on the fixed train/validation manifests.
- Run an OpenEvolve pilot on the `train` profile.
- Keep only complete long-run results for the final report.
- Use stress calibration only as a post-search validation layer, not as the primary search loop.

---

## Appendix: Simulation Configuration

| Parameter | Value |
|-----------|-------|
| Simulator | ChampSim (upstream, March 2026) |
| CPU | 1 core, OoO, 352-entry ROB, 128-entry LQ, 72-entry SQ |
| L1D | 64 sets × 12 ways, 5-cycle latency |
| L2C | 1024 sets × 8 ways, 10-cycle latency, **SPP prefetcher** |
| LLC | 2048 sets × 16 ways, 20-cycle latency, no prefetcher, LRU replacement |
| DRAM | DDR4-3200, 1 channel, 8 bankgroups × 4 banks, tCAS/tRCD/tRP = 24 |
| bzip2 trace | 401.bzip2-226B (DPC-3), 1M warmup + 5M simulation |
| mcf trace | 429.mcf-51B (DPC-3), 10M warmup + 50M simulation |
| FDP epoch size | 500 demand-LOAD triggers per epoch |
| FDP levels | 5 levels (pf_threshold ∈ {80, 60, 25, 15, 5}) |
