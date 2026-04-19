# Phase 1 Bottleneck Follow-up
## Astar Expansion And Weighted-Speedup Diagnosis

This note extends the current Phase 1 benchmark suite with `473.astar` and follows up on the central question from the proposal: why does the current hand-designed throttling logic fail to move weighted speedup consistently?

The current evidence points to two distinct regimes rather than one generic "bandwidth problem":

1. `mcf`-like latency-bound runs regress because throttling removes useful prefetch side-effects at DRAM, especially row-buffer locality.
2. `lbm`-like symmetric throughput-bound runs are not being throttled by BWC because the controller only samples pressure once at the epoch boundary, which can miss short-lived but important congestion spikes.

This report therefore treats the WS problem as a workload-classification problem rather than a single tuning bug.

## 1. Updated Workload Spectrum

The original single-core suite already spanned:

- `401.bzip2`: compute-bound, light memory pressure.
- `429.mcf`: latency-bound pointer chasing with near-zero useful prefetch accuracy.
- `470.lbm`: high-MLP streaming behavior with bandwidth sensitivity.

`473.astar` is added here as the mid-spectrum case. The exploratory `SPP_Orig` run already showed that it is not simply another copy of either `mcf` or `lbm`:

- IPC: `0.1301`
- Demand stall: `94.07%` of cycles
- PF accuracy: `2.87%`
- L1D miss latency: `241` cycles
- AVG DBUS congested: `11.58`

These numbers place `astar` between `mcf` and `lbm` in prefetch usefulness and miss latency, while remaining heavily demand-stall dominated.

## 2. Quantitative Bottleneck Diagnosis

### 2.1 `mcf`: latency and row-buffer assistance dominate

The strongest existing single-core negative result remains `mcf`.

Compared with `SPP_Orig`, both FDP and BWC reduce or suppress prefetch traffic without improving throughput:

- `SPP_Orig`: IPC `0.1212`, demand stall `387,318,696` cycles, L1D miss latency `139.4`, L2C miss latency `185.2`
- `SPP_FDP`: IPC `0.1165`, demand stall `403,966,767` cycles, L1D miss latency `148.2`, L2C miss latency `197.7`
- `SPP_BWC`: IPC `0.1164`, demand stall `404,329,876` cycles, L1D miss latency `148.3`, L2C miss latency `197.8`

The key quantitative signal is not average queue occupancy. It is the loss of row-buffer locality:

- `SPP_Orig` RQ row-buffer hits: `6,802`
- `SPP_FDP` RQ row-buffer hits: `3,769`
- `SPP_BWC` RQ row-buffer hits: `3,784`

That is roughly a `44%` hit-rate collapse after throttling. At the same time:

- LLC miss latency is almost unchanged (`219.8` vs `219.6/219.7`)
- demand stall cycles increase by about `4.3%`
- L1D/L2C miss latency rises by about `6-7%`

The conclusion is that `mcf` is not primarily bottlenecked by sustained average queue fullness. It is bottlenecked by serialized demand latency, and the current throttles destroy one of the few accidental latency-hiding mechanisms still helping the workload.

### 2.2 `astar`: mid-spectrum case under evaluation

The full `astar` single-core matrix is used to classify whether `astar` behaves more like:

- a milder `mcf`-style latency-bound case,
- a lower-coverage `lbm`-style streaming case, or
- a distinct mixed regime where prefetch filtering and bandwidth-aware throttling should both matter.

The final classification table below is filled from the new canonical single-core batch:

| Workload | Policy | IPC | PF Accuracy | Demand Stall % | L1D MPKI | L2C MPKI | LLC MPKI | L1D Lat | L2C Lat | LLC Lat | DBUS | Row-Hit Rate |
|----------|--------|-----|-------------|----------------|----------|----------|----------|---------|---------|---------|------|--------------|
| `mcf` | `orig` | 0.1212 | 0.015% | 93.90 | 156.01 | 158.10 | 87.71 | 139.4 | 185.2 | 219.8 | 7.753 | 0.157% |
| `astar` | `orig` | 0.1301 | 2.87% | 94.07 | 96.58 | 101.52 | 66.00 | 241.0 | 306.8 | 329.8 | 11.58 | 2.356% |
| `lbm` | `orig` | 0.8482 | 5.08% | 79.06 | 51.17 | 89.70 | 48.36 | 319.5 | 547.1 | 637.0 | 7.524 | 6.583% |
| `bzip2` | `orig` | 2.146 | 5.18% | 26.92 | 2.67 | 5.67 | 2.24 | 82.5 | 130.9 | 154.0 | 2.908 | 16.740% |

The completed `astar` policy sweep sharpens the classification:

| Policy | IPC | Delta vs `orig` | PF Accuracy | Demand Stall % | L1D MPKI | L2C MPKI | LLC MPKI | L1D Lat | L2C Lat | LLC Lat | DBUS | Row-Hit Rate |
|--------|-----|-----------------|-------------|----------------|----------|----------|----------|---------|---------|---------|------|--------------|
| `no_pref` | 0.1250 | -3.92% | 0.00% | 94.32 | 98.81 | 61.86 | 57.66 | 250.9 | 313.6 | 319.3 | 12.10 | 2.310% |
| `orig` | 0.1301 | baseline | 2.87% | 94.07 | 96.58 | 101.52 | 66.00 | 241.0 | 306.8 | 329.8 | 11.58 | 2.356% |
| `fdp` | 0.1259 | -3.23% | 4.61% | 94.27 | 97.05 | 81.91 | 58.58 | 247.5 | 313.7 | 323.8 | 12.08 | 2.438% |
| `bwc` | 0.1255 | -3.54% | 5.74% | 94.29 | 97.49 | 69.58 | 58.51 | 248.7 | 312.9 | 322.3 | 12.05 | 2.403% |
| `bop` | 0.1369 | +5.23% | 0.00% | 93.75 | 96.90 | 80.60 | 67.54 | 234.5 | 292.2 | 339.9 | 9.856 | 2.114% |

Three conclusions follow:

- `astar` is much closer to the memory-stall-heavy end of the spectrum than to `bzip2`: its demand-stall fraction (`94.07%`) is essentially as high as `mcf`, but its PF accuracy and row-hit rate are materially higher than `mcf`.
- Within the SPP family, `orig` remains the best `astar` operating point. Both `fdp` and `bwc` increase measured PF accuracy, but they still reduce IPC by `3.2-3.5%` and raise DBUS pressure, which means their extra filtering is reducing useful coverage or timeliness more than it removes harmful traffic.
- `astar` is therefore a meaningful midpoint for this project: unlike `mcf`, it does have some prefetch utility, but like `mcf`, it is still fragile to over-throttling. That makes it a good discriminator for whether a new controller understands *utility* instead of just *aggressiveness*.

### 2.3 2-core WS follow-up

The new 2-core experiments focus on the two most informative mixes:

- `mcf + astar`
- `astar + lbm`

These are intended to answer two questions:

1. Does `astar` behave as a polluter, a victim, or a mixed-role workload under contention?
2. Does the current BWC logic preserve or sacrifice weighted speedup when the co-runner is not as extreme as `mcf` or `lbm`?

The ROI IPC-based weighted speedup for each policy is summarized after the new runs complete:

| Mix | Policy | CPU0 IPC | CPU1 IPC | Weighted Speedup | Main Change Driver |
|-----|--------|----------|----------|------------------|--------------------|
| `mcf + astar` | `orig` | TBD | TBD | TBD | TBD |
| `mcf + astar` | `fdp` | TBD | TBD | TBD | TBD |
| `mcf + astar` | `bwc` | TBD | TBD | TBD | TBD |
| `astar + lbm` | `orig` | TBD | TBD | TBD | TBD |
| `astar + lbm` | `fdp` | TBD | TBD | TBD | TBD |
| `astar + lbm` | `bwc` | TBD | TBD | TBD | TBD |

## 3. Why BWC Misses `4×lbm`

Existing `4×lbm` results already show that BWC is a no-op:

- `SPP_Orig` WS: `1.0831`
- `SPP_BWC` WS: `1.0831`
- all BWC controller instances stay at Level 3

The new instrumentation is meant to make the cause measurable instead of speculative:

- epoch-average L2C MSHR utilization
- epoch-max L2C MSHR utilization
- epoch-average LLC RQ utilization
- epoch-max LLC RQ utilization
- fraction of epochs whose average or peak crosses the BWC throttle thresholds

The expected failure mode is:

- epoch-average pressure stays below threshold,
- epoch-peak pressure crosses threshold intermittently,
- the controller's one-shot end-of-epoch sample never sees those peaks consistently enough to throttle.

The final measured summary from the new `4×lbm` BWC run is inserted here:

| Policy | Avg Epoch MSHR | Max Epoch MSHR | Avg Epoch LLC RQ | Max Epoch LLC RQ | Frac Epoch Max MSHR >= 0.85 | Frac Epoch Max LLC RQ >= 0.80 | Interpretation |
|--------|----------------|----------------|------------------|------------------|-----------------------------|-------------------------------|----------------|
| `bwc_4core` | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

## 4. Strategy Search: What Should Change Next?

The bottleneck study suggests that "just throttle harder" is the wrong next step. The controller must become more selective about *which* traffic to suppress and *when* to suppress it.

The most promising next-step strategies are:

| Rank | Strategy | Why It Helps | What Changes In ChampSim | Expected WS Effect | Cost / Risk |
|------|----------|--------------|--------------------------|--------------------|-------------|
| 1 | Coordinated global throttling or HPAC-style control | Avoids sacrificing the wrong core and can explicitly optimize WS rather than per-core local signals | Add shared-state policy over per-core prefetchers or LLC/MC-visible congestion signals | Highest upside on `mcf+astar` and `astar+lbm` where local vs global tradeoffs matter | Medium implementation cost, medium policy complexity |
| 2 | Demand-vs-prefetch scheduling or dropping under pressure | Targets the actual interference point without necessarily disabling useful prefetch generation | Add memory-controller or LLC queue prioritization/gating for prefetch requests | Strongest fit for `mcf`-style latency protection without fully removing row-buffer-friendly traffic | Higher implementation cost; may require deeper simulator plumbing |
| 3 | Better SPP-side utility filtering than confidence thresholds | Attacks the FDP failure mode directly: confidence is not utility | Add learned or heuristic utility filter ahead of issue/fill decisions | Likely most helpful for `astar`-like mixed regimes and for reducing harmful high-confidence noise | Lower hardware complexity, but benefit may be smaller than shared-resource-aware control |

Concrete paper evidence that supports this ranking:

- `HPAC` adds global interference feedback on top of local FDP-like control. The original MICRO 2009 paper reports `23%` system-performance improvement over always-aggressive prefetching and `14%` over local-only FDP on an 8-core system, with `17%` lower bus traffic. That maps directly to our current failure mode: per-core throttling can look locally sensible while still making the wrong global WS tradeoff.
- `Prefetch-Aware Shared Resource Management` extends the same idea deeper into the shared memory system. The ISCA 2011 paper shows that once memory schedulers and throttling policies become aware of prefetch traffic, performance improves by about `11%` on 4-core systems across three different resource-management schemes while also improving fairness. This is strong evidence that a WS-oriented controller should couple prefetch control with shared-resource management rather than leaving the prefetcher isolated.
- `Prefetch-Aware DRAM Controller (PADC)` is especially relevant for the `mcf` diagnosis. The MICRO 2008 paper shows that adaptively prioritizing demand versus prefetch requests and dropping likely-useless prefetches improves 4-core WS by `8.2%` and 8-core WS by `9.9%`, while reducing DRAM bandwidth use. Our `mcf` regression already points to the memory controller interface as the real contention point, so this is the cleanest next mechanism if we stay within the current SPP framework.
- `PPF` shows why confidence-only filtering is too weak for mixed workloads. The ISCA 2019 paper replaces SPP's internal confidence throttling with a perceptron filter that keeps deeper speculation but filters inaccurate requests, improving performance by `3.78%` in 1-core and `11.4%` in 4-core experiments over the underlying SPP. That makes it a good fit for `astar`, where accuracy is low but not near-zero and the key question is utility rather than raw confidence.
- `Puppeteer` broadens the same lesson from a single filter to a learned manager. The TACO 2022 paper reports average gains of `46.0%` in 1-core, `25.8%` in 4-core, and `11.9%` in 8-core versus no prefetching, while sharply reducing negative outliers. It is likely too large a jump for our next implementation step, but it supports the idea that online learned control can outperform fixed confidence thresholds when workload class changes over time.

Relevant references:

- [HPAC (MICRO 2009)](https://hps.ece.utexas.edu/pub/ebrahimi_micro09.pdf)
- [Prefetch-Aware Shared Resource Management (ISCA 2011)](https://people.inf.ethz.ch/omutlu/pub/prefetchaware-shared-resources_isca11.pdf)
- [Prefetch-Aware DRAM Controller (MICRO 2008)](https://people.inf.ethz.ch/omutlu/pub/prefetch-dram_micro08.pdf)
- [Feedback Directed Prefetching (HPCA 2007)](https://hps.ece.utexas.edu/pub/TR-HPS-2006-006.pdf)
- [Perceptron-Based Prefetch Filtering (ISCA 2019)](https://people.engr.tamu.edu/djimenez/pdfs/ppf_isca2019.pdf)
- [Puppeteer (TACO 2022)](https://bu-icsg.github.io/publications/2022/puppeteer_taco_2022.pdf)

## 5. Interim Takeaway

The current evidence already supports a sharper project narrative:

- `mcf` does not mainly need lower average queue occupancy; it needs protection from extra effective memory latency.
- `astar` is a better training/evaluation midpoint than `bzip2` because it is still stall-dominated but not near-zero-accuracy like `mcf`.
- `4×lbm` does not falsify bandwidth-aware control; it shows that the current sampling method is too weak to detect symmetric saturation.

The remaining missing pieces are the two new 2-core WS tables and the new BWC pressure statistics for `4×lbm`.
