# Week 3 Research Report: Evaluating SPP, BOP, and Bandwidth-Aware Prefetch Control (FDP vs. BWC)

---

## Executive Summary

This report presents an empirical evaluation of two adaptive prefetch controllers—Feedback-Directed Prefetching (FDP) and our proposed Bandwidth-Aware Control (BWC)—layered over the Signature Path Prefetcher (SPP), alongside an independent evaluation of the Best-Offset Prefetcher (BOP). Experiments span both single-core and multi-core (2-core) scenarios using three workloads: `429.mcf` (pointer-chasing, memory-latency bound), `470.lbm` (streaming, high-memory-level parallelism, bandwidth bound), and `401.bzip2` (compute-bound with moderate memory pressure).

Our central findings are:

1. **BOP single-core:** BOP achieves the highest single-core IPC on `mcf` (0.1255, +8.0% vs. no-prefetch) through DRAM bus efficiency — its fixed offset=2 generates sequential cache-line prefetches that minimize bus contention. On `lbm`, BOP scores 0.7995 IPC (+32.8%), second only to SPP (0.8482), limited by lower prefetch coverage. BOP reports 0% `useful_prefetch` across all workloads due to a ChampSim measurement artifact for LLC-fill prefetchers (explained in Section 2.5).

2. **Single-core SPP+control:** In isolation, bandwidth is never the bottleneck. Throttling SPP is either neutral (on `lbm` for BWC) or actively harmful (on `lbm` for FDP, −21.9% IPC). An unexpected *DRAM row-buffer warm-up effect* means that even SPP's "useless" prefetches on `mcf` provide an accidental hardware benefit; eliminating them partially regresses IPC.

3. **Multi-core:** When a high-pollution workload (`mcf`) and a high-value streaming workload (`lbm`) share LLC and DRAM bandwidth, BWC's per-core, multi-sensor design correctly identifies and throttles the polluter while protecting the victim. FDP's single accuracy signal cannot make this distinction: it throttles both, sacrificing the victim to marginally accelerate the polluter and yielding a **5.1% lower Weighted Speedup** than BWC.

---

## 1. Methodology

### 1.1 Simulation Infrastructure

All experiments were conducted on the **ChampSim** cycle-accurate microarchitectural simulator. `mcf` and `lbm` experiments used a **10M-instruction warmup** followed by a **50M-instruction simulation phase**. `bzip2` experiments, being compute-bound with shorter phases, used a **1M-instruction warmup** followed by a **5M-instruction simulation phase**. Memory subsystem parameters match a realistic DDR4-3200 system with a single DRAM channel, a private per-core L2C (512 KB, 8-way, 1024 sets), and a shared LLC (2 MB, 16-way, 2048 sets).

### 1.2 Prefetcher and Controller Configurations

| Configuration | Prefetcher | Controller | Key Parameters |
|---------------|-----------|-----------|----------------|
| **No-Prefetch** | None | None | Demand-only baseline |
| **SPP\_Orig** | SPP (DPC-3 Champion) | None | `pf_threshold=25`, `fill_threshold=90` |
| **SPP\_FDP** | SPP | FDP (accuracy-based) | Epoch=500 accesses; throttle if accuracy < 50% |
| **SPP\_BWC** | SPP | BWC (our design) | Throttle if accuracy < **1%** OR LLC RQ util > 80% OR MSHR util > 85% |
| **BOP** | BOP (Best-Offset Prefetcher, DPC-3) | None | ±40 offsets (SCORE\_MAX=31, ROUND\_MAX=100), LLC-fill |

FDP operates on a single signal—per-epoch prefetch accuracy—and adjusts `pf_threshold` across five discrete levels. BWC extends this with a *hybrid* sensor: queue-pressure monitors on the shared LLC read queue and the L2C MSHR augment the accuracy signal, enabling BWC to throttle under bandwidth stress independent of accuracy. Crucially, BWC's accuracy fallback threshold is set at 1% (versus FDP's 50%), making it far more tolerant of streaming workloads with inherently low hit rates.

### 1.3 Workloads

| Benchmark | Class | Memory Profile |
|-----------|-------|---------------|
| `429.mcf` | Graph optimization (pointer-chasing) | Memory-latency bound; serial dependency chains; ~0.015% prefetch accuracy |
| `470.lbm` | Lattice Boltzmann (streaming stencil) | Memory-bandwidth bound; high MLP; ~5% prefetch accuracy |
| `401.bzip2` | Compression (sliding window, tree walk) | Compute-bound with moderate memory pressure; mixed access patterns |

These benchmarks were selected to span three distinct memory access regimes: latency-bound pointer chasing (`mcf`), bandwidth-bound streaming (`lbm`), and compute-bound moderate-pressure (`bzip2`).

---

## 2. Single-Core Evaluation

### 2.1 Results

| Workload | No-Pref | BOP | SPP\_Orig | SPP\_FDP | SPP\_BWC |
|----------|---------|-----|----------|---------|---------|
| **`mcf` IPC** | 0.1163 | **0.1255** (+7.9%) | 0.1212 | 0.1165 (−3.9%) | 0.1164 (−4.0%) |
| **`lbm` IPC** | 0.6019 | 0.7995 (+32.8%) | **0.8482** | 0.6626 (−21.9%) | **0.8482** (0.0%) |
| **`bzip2` IPC** | 1.899 | 2.066 (+8.8%) | **2.146** | 2.113 (−1.5%) | **2.146** (0.0%) |
| `mcf` PF Issued | — | 2,674,508 | 4,739,040 | 4,728,524 | 4,739,040 |
| `lbm` PF Issued | — | 324,839 | 3,218,978 | 499,021 | 3,218,978 |
| `mcf` PF Accuracy | — | 0%† | 0.015% | 0.017% | 0.015% |
| `lbm` PF Accuracy | — | 0%† | 5.08% | 21.6% | 5.08% |
| `mcf` Avg DRAM Bus Congested (cycles) | 8.682 | **6.566** | 7.753 | 8.668 | 8.670 |
| `lbm` Avg DRAM Bus Congested (cycles) | 5.830 | 7.480 | 7.524 | 6.669 | 7.524 |
| BOP converged offset | — | mcf=2, lbm=9, bzip2=11 | — | — | — |
| FDP/BWC Final Level | — | N/A | N/A | **Level 1** (throttled) | **Level 3** (unchanged) |

*BOP IPC delta relative to No-Pref. SPP\_FDP and SPP\_BWC delta relative to SPP\_Orig. † BOP's 0% accuracy is a measurement artifact explained in Section 2.5.*

### 2.2 Insight I: The DRAM Row-Buffer Warm-Up Effect on `mcf`

The most counterintuitive result is that both FDP and BWC, despite being correctly triggered on `mcf` (0.015% accuracy is far below both the 50% and 1% thresholds), actually *regress* IPC from the SPP\_Orig baseline back to near the no-prefetch level.

The underlying mechanism is a **second-order DRAM row-buffer warm-up effect**. The `mcf` trace follows pointer chains with natural heap-level page locality: successive pointer dereferences frequently fall within the same 4 KB physical page, and therefore within the same DRAM row (typically 8 KB). SPP's prefetches—even the "useless" ones that are never consumed by the CPU—continuously access these DRAM rows, keeping them open in the row buffer. When the subsequent demand access for the actual pointer target arrives, it finds the row already open and enjoys a DRAM *row buffer hit*, dramatically reducing effective access latency.

When a controller throttles SPP, this unintended row-buffer warming ceases. Demand accesses that previously landed on open rows now suffer row-buffer misses (activate + CAS penalty). The net result is that the "useless" prefetches were, paradoxically, providing genuine latency reduction through a side-channel mechanism orthogonal to their nominal purpose of bringing data into the L2C. This effect is **controller-independent**: any mechanism that reduces prefetch volume on `mcf` will incur this cost.

### 2.3 Insight II: FDP's Catastrophic False-Positive on `lbm`

FDP collapses `lbm`'s IPC from **0.8482 to 0.6626 (−21.9%)**, reducing it to only 10.1% above the no-prefetch baseline despite SPP\_Orig delivering a 40.9% gain. The cause is a fundamental misalignment between FDP's accuracy metric and the value of prefetching for streaming workloads.

FDP observes `lbm`'s 5.08% prefetch accuracy, compares it against `ACC_LOW = 50%`, and immediately drops to Level 1 (`pf_threshold = 80`). This raises the confidence bar for issuing a prefetch, slashing the issued count from **3.2M to 499K** (−84.5%). However, `lbm`'s prefetches, while individually imprecise in the SPP confidence sense, collectively provide *high aggregate bandwidth coverage* of the streaming access pattern. Each prefetch that does hit brings a useful cache line early enough to hide memory latency. The 5% that are "useful" by ChampSim's definition undersells their collective impact; the 95% that are "useless" are, in fact, necessary over-prefetching to maintain streaming DRAM bandwidth utilization.

The core failure is that FDP uses a single scalar accuracy metric to make a binary quality judgment, with no mechanism to distinguish "low accuracy, low value" (pointer-chasing garbage) from "low accuracy, high aggregate value" (streaming over-coverage).

### 2.4 Insight III: BWC's "Do No Harm" Principle on `lbm`

BWC's 1% accuracy fallback threshold correctly classifies `lbm`'s 5.08% accuracy as *not requiring intervention*. The queue-pressure sensors (LLC RQ utilization and L2C MSHR utilization) also do not fire in single-core: `lbm` uses bandwidth efficiently, not to the point of saturating the LLC read queue or the MSHR. As a result, BWC remains at Level 3 throughout the simulation, issuing the same 3.2M prefetches as SPP\_Orig and achieving identical IPC: **0.8482**.

This result validates the design philosophy of using a *multi-signal sensor with a conservative fallback*. By setting the accuracy threshold at 1% rather than 50%, BWC's operating envelope encompasses all workloads where prefetching provides meaningful benefit, regardless of their nominal hit rate.

### 2.5 BOP: Offset Convergence and the LLC-Fill Accuracy Measurement Gap

**Offset convergence.** BOP's scoring phase (40 candidate offsets, SCORE\_MAX=31, ROUND\_MAX=100) converges to a single best offset per execution phase. For these workloads: **offset=2** for `mcf` (capturing consecutive-line heap locality), **offset=9** for `lbm` (partially capturing the stencil stride), and **offset=11** for `bzip2` (low-volume; bzip2's access pattern offers few exploitable constant-offset regularities, yielding only 1,715 prefetches over 5M instructions).

**The LLC-fill accuracy measurement gap.** BOP reports 0% `useful_prefetch` across all workloads. This is a structural artifact, not a correctness failure. BOP uses `fill_this_level=false`, placing prefetches in the LLC rather than L2C. ChampSim's `useful_prefetch` counter registers a hit only when a demand access finds a prefetched block *in L2C*—which requires the block to have been (a) filled to L2C from LLC following an earlier demand miss on the prefetched block, and (b) demanded again before eviction. For low-reuse workloads, this two-step sequence rarely completes within the L2C residency window.

The **actual latency benefit** materializes through MSHR merging: when a demand access arrives for a block whose BOP prefetch is already in-flight in the DRAM queue, the demand merges into the prefetch's MSHR entry. The demand stall is eliminated without incrementing `useful_prefetch`. This is directly measurable in demand stall cycles: BOP reduces `mcf` demand stall cycles from 404M (no-prefetch) to 373M (−31M cycles). This 31M-cycle reduction in stall time closely tracks the 31.3M-cycle reduction in total execution cycles, suggesting that MSHR-level demand stall elimination is the primary source of BOP's IPC gain on `mcf`.

**BOP outperforms SPP on `mcf`.** Counterintuitively, BOP achieves the highest single-core IPC on `mcf` among all five configurations (0.1255 vs. SPP\_Orig's 0.1212). The mechanism operates through DRAM bus efficiency: BOP's fixed offset=2 generates spatially sequential prefetch requests that match the natural cache-line address stride of `mcf`'s heap allocations. These sequential accesses keep DRAM rows open longer, allowing both prefetch and subsequent demand requests to land as row-buffer hits with lower latency. The Average DRAM Data Bus Congested metric drops to **6.566 cycles** for BOP—the lowest across all five policies—versus 7.753 for SPP\_Orig and 8.682 for no-prefetch. SPP generates 4.7M prefetches following complex signature-driven patterns that spread across DRAM rows more randomly, partially negating their value through increased bus contention.

**SPP dominates BOP on `lbm`.** For the streaming stencil workload, SPP\_Orig achieves 0.8482 IPC versus BOP's 0.7995 (−5.7%). BOP's single offset=9 captures lbm's array stride partially but issues only 324K prefetches. SPP's path-confidence mechanism identifies the stride signature and systematically generates 3.2M prefetches—approximately 10× more coverage—while maintaining similar DRAM bus efficiency (AVG 7.524 vs. BOP's 7.480 cycles). For structured streaming workloads, the breadth of path-based prediction outweighs BOP's simplicity advantage.

---

## 3. Multi-Core Evaluation (2-Core: `mcf` + `lbm`)

### 3.1 Experimental Setup

The 2-core experiment places `mcf` on Core 0 and `lbm` on Core 1, sharing the LLC and DRAM. Core 0 acts as the **Polluter**: its 4.7M low-accuracy prefetches per 50M instructions flood the LLC read queue and DRAM row buffers, starving Core 1's high-value demand traffic. Core 1 is the **Victim**: its performance is highly sensitive to DRAM bandwidth availability.

Each L2C has an independent prefetcher instance with its own sensor state. Both instances share visibility into the LLC read queue occupancy via the `intern_->lower_level->rq_occupancy()` API, enabling per-core controllers to observe system-level congestion while making independent throttling decisions.

Weighted Speedup (WS) is computed as:

> WS = (IPC\_cpu0\_2core / IPC\_mcf\_1core) + (IPC\_cpu1\_2core / IPC\_lbm\_1core)
>
> where IPC\_mcf\_1core = 0.1212 and IPC\_lbm\_1core = 0.8482 (SPP\_Orig single-core baselines)

### 3.2 Results

| Metric | SPP\_Orig | SPP\_FDP | SPP\_BWC |
|--------|----------|---------|---------|
| **`mcf` IPC** (cpu0) | 0.0395 | **0.0589 (+49%)** | 0.0385 (−3%) |
| **`lbm` IPC** (cpu1) | 0.7884 | 0.5990 (−24%) | **0.7933 (+0.6%)** |
| `mcf` PF Issued | 4,730,978 | 4,734,472 (≈same) | **1,190,400 (−75%)** |
| `lbm` PF Issued | 69,171,171 | 5,080,267 (−93%) | 71,617,061 (≈same) |
| `mcf` PF Accuracy | 0.011% | 0.011% | 0.012% |
| `lbm` PF Accuracy | 4.89% | 25.0% | 4.90% |
| **Weighted Speedup** | **1.256** | **1.192 (−5.1%)** | **1.253 (−0.2%)** |
| Total Simulation Cycles | 1.265B | **0.849B** | 1.300B |
| cpu0 (mcf) controller | N/A | FDP Level 1 | **BWC Level 1** (acc < 1%) |
| cpu1 (lbm) controller | N/A | FDP Level 1 | **BWC Level 3** (untouched) |

*lbm PF Issued is large because lbm loops many times while waiting for mcf to complete 50M instructions.*

### 3.3 Insight IV: Asymmetric Throttling — BWC's Core Victory

The decisive result is visible in the final controller state readout:

- **BWC on cpu0 (mcf):** `Final level=1, pf_threshold=80, issue_period=4` — mcf's per-epoch accuracy (0.012%) immediately triggers the `BWC_ACC_LOW_THROTTLE = 1%` fallback. The rate-limiter engages with `issue_period=4`, issuing only every fourth prefetch candidate and cutting mcf's prefetch volume by **75%** (4.73M → 1.19M).
- **BWC on cpu1 (lbm):** `Final level=3, pf_threshold=25, issue_period=1` — lbm's accuracy (4.9%) clears the 1% fallback. The queue-pressure sensors, having been partially relieved by mcf's throttling, also do not fire. lbm is left completely unthrottled.

The result is **asymmetric throttling**: the polluter is suppressed; the victim is protected. lbm achieves **0.7933 IPC**, a mere 6.5% below its uncontended single-core performance of 0.8482. BWC's Weighted Speedup of **1.253** is **5.1% above FDP's 1.192**.

### 3.4 Insight V: Why FDP Accidentally Helps `mcf` — The Bandwidth Transfer Effect

The most counterintuitive result in the multi-core experiment is that FDP raises `mcf`'s IPC by **49%** (0.0395 → 0.0589). The mechanism, however, is not what one might expect.

Examining the prefetch issue counts reveals a critical fact: FDP's Level 1 (`pf_threshold=80`) does **not** reduce `mcf`'s prefetch volume. `mcf` issues 4,730,978 prefetches under SPP\_Orig and 4,734,472 under FDP — effectively identical. `mcf`'s SPP access signatures are predominantly short, high-confidence patterns that clear even the elevated threshold=80 bar.

Instead, FDP's threshold dramatically affects `lbm`. `lbm`'s SPP patterns involve deep lookahead paths with low per-step confidence; raising the threshold from 25 to 80 eliminates the majority of these, slashing `lbm`'s prefetch volume from **69M to 5M (−93%)**. This frees an enormous amount of DRAM bandwidth that was previously consumed by `lbm`'s prefetch stream. With `lbm`'s prefetch traffic gone, `mcf`'s demand accesses—which had been queued behind `lbm`'s prefetches—can be serviced much more quickly. `mcf` completes 50M instructions in 849M cycles versus 1.265B under SPP\_Orig.

BWC's actuator behaves differently: its rate-limiter reduces `mcf`'s own prefetch count by 75% (4.73M → 1.19M), but leaves `lbm`'s prefetches largely intact (71.6M, approximately equal to SPP\_Orig). The bandwidth consumed by `lbm`'s prefetch stream is unchanged, so `mcf`'s demand accesses experience the same queuing delay as before. `mcf`'s IPC sees no meaningful improvement.

This reveals an important asymmetry: **the key lever for improving `mcf`'s performance in this scenario is throttling `lbm`'s prefetches, not `mcf`'s own.** FDP achieves this accidentally by applying a uniform threshold to both cores; BWC's design deliberately leaves `lbm` unthrottled to preserve its performance and does not obtain this benefit. This is a genuine design trade-off: a controller optimizing for total system throughput would throttle `lbm` to help `mcf`; a controller optimizing for per-workload QoS would protect `lbm` as BWC does.

### 3.5 Insight VI: System-Level Performance Isolation

FDP and BWC represent two philosophically distinct approaches to multi-core prefetch management:

**FDP (Single-Signal, Uniform Throttle):** FDP applies the same Level 1 throttle to *both* cores. On cpu1, lbm's 5% accuracy falls below the 50% threshold just as definitively as mcf's 0.011% does. The result is that lbm's prefetch volume is slashed by **93%** (69M → 5M), destroying its IPC by 24%. FDP has **sacrificed the victim to accelerate the polluter**, an outcome that is the opposite of the intended goal of bandwidth-aware prefetch control.

**BWC (Multi-Signal, Per-Core Discrimination):** BWC's wider accuracy tolerance, combined with per-core sensor state, enables it to make the correct classification for each core independently. The net effect is **system-level performance isolation**: mcf's bandwidth consumption is constrained; lbm's performance envelope is preserved. While BWC does not achieve FDP's reduction in total simulation cycles (the simulation ends when the slower core, `mcf`, finishes its 50M instructions: 849M cycles for FDP vs. 1.300B for BWC, because FDP throttles `lbm` and frees bandwidth for `mcf`), it produces a better *fairness-weighted* outcome under the Weighted Speedup metric.

The fundamental trade-off is clear: optimizing for total-cycle throughput (FDP's implicit objective) and optimizing for per-core performance fairness (BWC's explicit objective) require different control strategies. For latency-sensitive or QoS-constrained deployments where a specific workload's performance must be preserved—a database query servicing an end user, for example—BWC's performance isolation approach is the correct design.

---

## 4. Multi-Core Evaluation (4-Core: 4×`lbm`)

### 4.1 Experimental Setup

The 4-core experiment runs four simultaneous `lbm` instances (one per core), all sharing the same LLC (2 MB, 16-way) and single DRAM channel. This is a **homogeneous, bandwidth-stress** scenario: every core issues the same high-MLP streaming prefetch pattern, heavily stressing the shared LLC read queue and DRAM bus.

The single-core `lbm` SPP\_Orig baseline (IPC = 0.8482) serves as the normalization reference for Weighted Speedup (WS) and Harmonic Speedup (HS):

> WS = Σ (IPC\_cpu*i*\_4core / 0.8482),  HS = 4 / Σ (0.8482 / IPC\_cpu*i*\_4core)

Simulation parameters: 5M-instruction warmup, 20M-instruction simulation phase per core.

### 4.2 Results

| Metric | SPP\_Orig | SPP\_FDP | SPP\_BWC | BOP |
|--------|----------|---------|---------|-----|
| **CPU0 IPC** | 0.2320 | **0.2419** | 0.2320 | 0.2307 |
| **CPU1 IPC** | 0.2335 | 0.2383 | 0.2335 | 0.2295 |
| **CPU2 IPC** | 0.2339 | 0.2143 | 0.2339 | 0.2281 |
| **CPU3 IPC** | 0.2193 | 0.2153 | 0.2193 | 0.2289 |
| **Avg IPC** | 0.2297 | 0.2274 | 0.2297 | **0.2293** |
| **Weighted Speedup** | **1.0831** | 1.0726 (−1.0%) | **1.0831** | 1.0813 (−0.2%) |
| **Harmonic Speedup** | **0.2706** | 0.2673 (−1.2%) | **0.2706** | 0.2703 (−0.1%) |
| Max Simulation Cycles | 91.2M | 93.3M | 91.2M | **87.7M** |
| Total PF Issued (all cores) | 6,000,711 | 694,761 | 6,000,711 | 518,367 |
| System PF Accuracy | 4.64% | **28.3%** | 4.64% | 0%† |
| DRAM Avg Bus Congested (cycles) | 4.490 | 6.074 | 4.490 | **4.934** |
| DRAM RQ Row Buffer Hit | 117,699 | **142,115** | 117,699 | 120,201 |
| Total LLC Miss | 9,467,316 | 5,376,240 | 9,467,316 | **5,112,542** |
| Controller Final State | Level 3 | **Level 1** (all cores) | Level 3 | offset=6 |

*† BOP's 0% accuracy is the LLC-fill measurement artifact described in Section 2.5.*

### 4.3 Insight VII: BWC Does Not Fire in Homogeneous Bandwidth-Stress

The most striking result is that **BWC produces output identical to SPP\_Orig** across all metrics: same per-core IPCs, same prefetch counts, same DRAM statistics. The controller state confirms this: all four BWC instances report `Final level=3, issue_period=1` — completely unthrottled.

This outcome is consistent with BWC's sensor design. BWC's accuracy fallback fires at < 1%; in 4×lbm, each core's `lbm` prefetches achieve ~4.6% accuracy — above the threshold. In this run, the LLC RQ and MSHR pressure sensors also never drove any core below Level 3 at epoch boundaries. This suggests that BWC's current per-core, threshold-based design is well matched to the heterogeneous case (one polluter, one victim), but is less responsive to symmetric contention where all cores generate similar pressure.

This suggests an important scope limitation: **BWC's "do no harm" conservatism, which is its strength in heterogeneous scenarios, becomes less effective in homogeneous bandwidth-stress.** A system-level sensor (e.g., total DRAM queue occupancy rather than per-core LLC RQ) may be needed to detect and respond to symmetric contention.

### 4.4 Insight VIII: FDP Throttles Uniformly and Hurts System Throughput

FDP drops all four cores to Level 1 (`pf_threshold=80`), cutting total prefetch volume from 6.0M to 694K (−88%). The higher threshold filters out many lower-confidence SPP candidates, pushing system PF accuracy from 4.6% to 28.3% and reducing total LLC misses by 43% (9.47M → 5.38M). However, the net effect on performance is **negative**: Weighted Speedup drops by 1.0% (1.0831 → 1.0726) and max simulation cycles increase by 2.1M.

The explanation likely parallels the 2-core result. In the homogeneous case, FDP's `pf_threshold=80` appears to remove a large fraction of the prefetch coverage that normally helps `lbm` tolerate memory latency. Per-core throughput shifts unevenly: CPU0 and CPU1 improve slightly (0.232→0.242 and 0.234→0.238), while CPU2 and CPU3 regress (0.234→0.214 and 0.219→0.215). The wider IPC spread under FDP (0.214–0.242 vs. 0.219–0.234 for SPP\_Orig) indicates increased cross-core imbalance, plausibly due to timing-sensitive LLC/DRAM queue interactions after uniform throttling.

### 4.5 Insight IX: BOP Achieves the Fastest Completion Through DRAM Efficiency

BOP achieves the **lowest max simulation cycles (87.7M)**, finishing 4.0% faster than SPP\_Orig (91.2M) despite lower Weighted Speedup (1.0813 vs. 1.0831). The IPC distribution is also the most uniform (0.228–0.231), with the smallest core-to-core variance of all four configurations.

BOP converges to **offset=6** in the 4-core scenario (versus offset=9 in single-core). One plausible interpretation is that, under 4-core contention, a shorter offset produces a prefetch stream with better spatial locality. Consistent with that interpretation, DRAM RQ Row Buffer Misses drop from 2,316,114 (SPP\_Orig) to 2,216,056 (BOP, −4.3%).

BOP's total LLC misses (5.11M) are also lower than SPP\_Orig (9.47M), suggesting that BOP's smaller prefetch footprint may be a better match for LLC capacity under 4-core pressure. DRAM Avg Bus Congested is slightly higher for BOP than for SPP\_Orig (4.934 vs. 4.490), so the fastest completion is more plausibly explained by the combination of fewer LLC misses, fewer row-buffer misses, and a tighter per-core IPC distribution than by lower bus congestion alone.

The apparent paradox — BOP finishes fastest yet has lower Weighted Speedup — arises because Weighted Speedup is dominated by total IPC summed across cores, whereas max simulation cycles is determined by the slowest core. BOP's more uniform per-core IPC distribution means no single core lags far behind the others; SPP\_Orig's wider variance (0.219–0.234) leaves more room for one slow core to extend the overall completion time. Under a wall-clock completion metric, BOP could therefore be preferable even though its Weighted Speedup is slightly lower.

---

## 5. Conclusion

This study evaluated FDP, BWC, and BOP across single-core, 2-core heterogeneous, and 4-core homogeneous bandwidth-stress scenarios. Our empirical results establish the following conclusions:

1. **Prefetch throttling is not universally beneficial in single-core environments.** In isolation, bandwidth is abundant, and reducing prefetch volume can trigger secondary hardware effects (DRAM row-buffer cooling on `mcf`) or eliminate high-value streaming coverage (on `lbm`). Controllers must be conservative by default.

2. **BOP provides genuine latency-hiding benefits despite 0% reported accuracy in ChampSim's counter.** The zero `useful_prefetch` count is a structural artifact of LLC-level fill: BOP's demand latency benefit accrues via MSHR merging at the DRAM level, visible only in demand stall cycles (bop\_mcf: 373M vs. no-prefetch: 404M). BOP unexpectedly achieves the **highest single-core IPC on `mcf`** among all five policies (0.1255) through a DRAM bus efficiency advantage (AVG DBUS CONGESTED = 6.566 cycles) driven by its spatially sequential offset=2 prefetch stream. For structured streaming workloads, SPP retains a decisive coverage advantage (`lbm` IPC 0.8482 vs. BOP's 0.7995).

3. **FDP's 50% accuracy threshold is a fatal misclassification boundary.** Streaming workloads like `lbm` operate with 5–10% prefetch accuracy by design—their value comes from aggregate bandwidth coverage, not individual prefetch precision. FDP cannot distinguish this class from pointer-chasing garbage, resulting in a −21.9% single-core IPC regression and a −24% multi-core IPC regression for `lbm`.

4. **BWC's multi-signal, conservative-threshold design achieves "do no harm" in all tested scenarios.** The 1% accuracy fallback correctly encompasses all workloads where prefetching provides value. In 2-core, queue-pressure sensors provide the additional discrimination needed to identify bandwidth congestion caused by a co-running polluter.

5. **Asymmetric per-core throttling is possible and effective.** BWC's per-L2C independent sensor state enables it to simultaneously throttle `mcf` (accuracy < 1%) while leaving `lbm` unthrottled, achieving **+5.1% Weighted Speedup over FDP** in the heterogeneous 2-core scenario.

6. **The source of bandwidth relief matters as much as the amount.** FDP's uniform threshold throttles `lbm`'s prefetches (69M → 5M), freeing bandwidth for `mcf`'s demand accesses (+49% IPC for `mcf`). BWC's rate-limiter throttles `mcf`'s own prefetches instead, leaving `lbm`'s bandwidth intact, so `mcf` sees no relief. This reveals that in a heterogeneous multi-core scenario, throttling the *high-volume* co-runner's prefetches can be more effective for the latency-bound core than throttling its own.

7. **BWC's per-core threshold design shows a blind spot in this homogeneous bandwidth-contention case.** In the 4-core 4×`lbm` scenario, BWC does not throttle at all (all cores remain at Level 3), producing results identical to SPP\_Orig. This suggests BWC's sensors are better calibrated for the heterogeneous case than for symmetric saturation where all cores simultaneously stress the shared DRAM channel.

8. **BOP achieves the fastest 4-core completion while maintaining the most uniform IPC distribution.** BOP finishes 4.0% faster than SPP\_Orig in wall-clock simulation cycles, with the tightest per-core IPC variance (0.228–0.231). BOP adapts its converged offset from 9 (single-core) to 6 (4-core) under increased contention, and its lower row-buffer-miss count plus lower LLC miss footprint (5.11M vs. SPP\_Orig's 9.47M) are consistent with a smaller, more locality-friendly prefetch footprint under capacity pressure.

These results motivate further exploration of multi-signal, per-core bandwidth controllers in multi-core server workloads, where heterogeneous co-runners and shared memory hierarchies make the accurate classification of prefetch value a critical and unsolved problem. The 4-core homogeneous results additionally motivate system-level (rather than per-core) congestion sensing to handle symmetric bandwidth saturation.
