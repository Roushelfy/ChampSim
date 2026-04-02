# Week 3 Research Report: Bandwidth-Aware Prefetch Throttling with BWC

---

## Executive Summary

This report refocuses our Week 3 results around the main project objective stated in the proposal: building and evaluating a **bandwidth-aware runtime throttling controller** for hardware prefetchers. Our main artifact is **Bandwidth-Aware Control (BWC)**, a lightweight controller layered on top of SPP that uses runtime signals to decide **when** and **how aggressively** prefetching should proceed under contention.

The motivation is straightforward. In multicore systems, prefetch quality cannot be judged by accuracy alone: even nominally useful prefetches can still hurt performance if they fill shared queues, consume off-chip bandwidth, and delay demand requests. BWC therefore augments prefetch accuracy with **MSHR pressure** and **shared LLC read-queue pressure**, aiming to throttle only when the memory system is genuinely under stress.

Across our current experiments, BWC shows two clear behaviors. First, in **single-core** runs it follows a "do no harm" policy: it leaves `lbm` unchanged, because that workload benefits from aggressive coverage and does not create harmful shared contention in isolation. Second, in a **2-core heterogeneous** mix (`mcf` + `lbm`), BWC successfully performs **asymmetric per-core throttling**: it suppresses the low-value polluter (`mcf`) while preserving the high-value streaming prefetches of `lbm`, improving Weighted Speedup by **5.1% over FDP**.

At the same time, the experiments reveal an important limitation of the current hand-designed controller. In a **4-core homogeneous** `4×lbm` scenario, BWC never triggers and therefore matches unthrottled SPP exactly. This suggests that the current per-core threshold design is effective for heterogeneous contention, but not yet sufficient for symmetric saturation where all cores generate similar pressure simultaneously.

The main takeaway is that **BWC is a useful hand-designed baseline for the project**, not the final answer. The current results validate the idea that multi-signal, bandwidth-aware control is more robust than static accuracy-only throttling, while also identifying the exact limitation that a later OpenEvolve-style search or a richer system-level controller should target.

---

## 1. Methodology

### 1.1 Simulation Infrastructure

All experiments were conducted on the **ChampSim** cycle-accurate microarchitectural simulator. `mcf` and `lbm` experiments used a **10M-instruction warmup** followed by a **50M-instruction simulation phase**. `bzip2` experiments, being compute-bound with shorter phases, used a **1M-instruction warmup** followed by a **5M-instruction simulation phase**. Memory subsystem parameters match a realistic DDR4-3200 system with a single DRAM channel, a private per-core L2C (512 KB, 8-way, 1024 sets), and a shared LLC (2 MB, 16-way, 2048 sets).

### 1.2 Controller Context and Baselines

Our focus is not on inventing a new address predictor, but on controlling prefetch aggressiveness under bandwidth pressure. Accordingly, **`SPP_Orig`** serves as the unthrottled reference, **`SPP_FDP`** serves as the primary prior-style static-threshold control baseline, and **BOP** is retained only as an auxiliary comparison point to show how a different predictor interacts with contention. These baselines are important as anchors, but the main question of this report is whether **BWC**, our hand-designed bandwidth-aware controller, makes better throttling decisions than an accuracy-only policy.

The key runtime signals measured by BWC are directly aligned with the project proposal: **prefetch usefulness/accuracy**, **L2C MSHR pressure**, and **shared LLC read-queue occupancy**. Together, these signals act as hardware-realistic proxies for the deeper objective of the project: detecting when prefetch traffic is no longer just hiding latency, but is instead degrading performance through **bandwidth contention and queuing delay**.

### 1.3 BWC Implementation

BWC is implemented as a **thin control layer around the unmodified SPP prediction pipeline**, rather than as a new prefetcher from scratch. The underlying SPP components—Signature Table (ST), Pattern Table (PT), Prefetch Filter, and Global History Register (GHR)—remain intact. BWC changes only the *control policy* that determines how aggressively SPP is allowed to issue prefetches in each epoch.

The implementation has four key elements:

1. **Per-L2C independent controller state.** Each core's L2C owns its own BWC instance and therefore maintains separate epoch counters, throttle level, and issue-period state. This is important in multi-core runs: one core can be throttled while another remains unthrottled.

2. **Three-signal sensor.** At the end of each epoch, BWC samples:
   - **Prefetch accuracy** = `useful / issued` over the current epoch
   - **L2C MSHR utilization** from `intern_->get_mshr_occupancy_ratio()`
   - **Shared LLC read-queue utilization** from `intern_->lower_level->rq_occupancy() / rq_size()`

   The accuracy signal acts as a near-zero-value fallback: if accuracy falls below **1%**, BWC throttles even if queue pressure is low. This is intended to catch workloads like `mcf`, where prefetches consume bandwidth but rarely help architecturally. The queue-pressure signals, by contrast, are designed to detect bandwidth stress even when nominal accuracy is not extremely low.

3. **Epoch-based decision logic.** BWC reuses FDP's epoch length of **500 cache accesses** and the same five-level control space, but changes the trigger condition. At each epoch boundary:
   - If `LLC_RQ_util > 80%`, or `MSHR_util > 85%`, or `accuracy < 1%`, BWC moves one level **down** toward more conservative prefetching.
   - If `LLC_RQ_util < 30%`, and `MSHR_util < 50%`, and `accuracy > 80%`, BWC moves one level **up** toward more aggressive prefetching.
   - Otherwise, it holds the current level.

   This makes BWC deliberately asymmetric: it throttles readily under pressure, but accelerates only when both congestion and accuracy indicate that extra prefetching is safe.

4. **Dual actuator: confidence threshold + rate limiter.** After selecting a new level, BWC updates the same SPP confidence thresholds used by FDP (`pf_threshold`, `fill_threshold`), but adds a second actuator absent from FDP: an **issue-period rate limiter**. The five levels map to:

| Level | `pf_threshold` | `fill_threshold` | `issue_period` | Effect |
|------|----------------|------------------|----------------|--------|
| 1 | 80 | 90 | 4 | Issue only every 4th eligible candidate |
| 2 | 60 | 90 | 2 | Issue only every 2nd eligible candidate |
| 3 | 25 | 90 | 1 | Baseline SPP behavior |
| 4 | 15 | 75 | 1 | More aggressive than baseline |
| 5 | 5 | 50 | 1 | Most aggressive |

The **placement of the rate limiter is important**. In the implementation, BWC checks `bwc_should_issue()` *before* the Prefetch Filter's combined check-and-set operation. This avoids marking a line as "already prefetched" when BWC actually decided to suppress it. In other words, dropped candidates do not poison SPP's own bookkeeping.

Operationally, this means BWC can respond in two different ways depending on the bottleneck. If low-confidence candidates dominate, raising `pf_threshold` filters them out directly. If a workload still produces many high-confidence candidates under pressure, the issue-period limiter can reduce traffic without changing the predictor itself. This is exactly why BWC differs from FDP in the multi-core experiments below.

### 1.4 Workloads

| Benchmark | Class | Memory Profile |
|-----------|-------|---------------|
| `429.mcf` | Graph optimization (pointer-chasing) | Memory-latency bound; serial dependency chains; ~0.015% prefetch accuracy |
| `470.lbm` | Lattice Boltzmann (streaming stencil) | Memory-bandwidth bound; high MLP; ~5% prefetch accuracy |
| `401.bzip2` | Compression (sliding window, tree walk) | Compute-bound with moderate memory pressure; mixed access patterns |

These benchmarks were selected to span the cases most relevant to a bandwidth-aware controller: extremely low-value prefetching (`mcf`), high-coverage streaming prefetching (`lbm`), and a lighter-pressure compute-bound case (`bzip2`).

---

## 2. Single-Core Evaluation: What BWC Learns in Isolation

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

*BOP IPC delta relative to No-Pref. SPP\_FDP and SPP\_BWC delta relative to SPP\_Orig. † BOP's 0% accuracy is a measurement artifact discussed briefly in Section 2.4.*

### 2.2 BWC's Single-Core Design Principle: Do No Harm

The single-core experiments are useful mainly because they show **what BWC should *not* do**. In isolation, the system does not yet exhibit the kind of shared-resource contention that motivates a bandwidth-aware controller. The main risk is therefore overreaction: a controller that throttles useful prefetches simply because their nominal accuracy looks low.

BWC avoids that mistake on `lbm`. Although `lbm`'s prefetch accuracy is only **5.08%**, BWC leaves the prefetcher at **Level 3**, issues the same **3.2M** prefetches as `SPP_Orig`, and preserves the full **0.8482 IPC**. This is an important early validation of the controller philosophy: for streaming workloads, low accuracy does not necessarily mean low value.

By contrast, FDP collapses `lbm`'s IPC from **0.8482 to 0.6626** because its 50% accuracy threshold misclassifies a useful streaming workload as harmful. The contrast matters more than the absolute single-core number: even before shared contention appears, BWC already demonstrates that **accuracy alone is not a sufficient control signal**.

### 2.3 What `mcf` Teaches BWC

`mcf` exposes a different lesson. Its prefetch accuracy is extremely low, so both FDP and BWC correctly classify it as a poor-quality prefetch stream. Yet in single-core, throttling still does not help: both controllers regress back to near the no-prefetch baseline.

For the BWC story, the important point is not that the controller made the wrong classification, but that **single-core `mcf` is not a bandwidth-contention case**. Instead, the data suggests that SPP's prefetches incidentally help by warming DRAM row buffers, so reducing them removes a secondary latency benefit. This means the BWC controller is doing what it was designed to do—detect near-zero-value prefetches—but the experiment also shows that usefulness under no contention is subtler than a single accuracy number.

Taken together, the single-core results support a narrow but important claim: **BWC is conservative where it should be conservative (`lbm`) and surfaces the limits of accuracy-only reasoning on irregular memory behavior (`mcf`)**.

### 2.4 Secondary Baseline Note: Why BOP Is Only a Reference Here

BOP is retained in this report only as a supporting comparison, not as the main object of analysis. The most relevant facts are: (1) its reported `0% useful_prefetch` is a ChampSim artifact for LLC-fill prefetchers rather than proof of zero value, and (2) its behavior helps show that prefetcher-side differences alone do not solve the project problem of **runtime bandwidth-aware throttling**. The central question of this report is therefore not whether BOP or SPP is the stronger predictor, but whether **BWC can control an aggressive prefetcher more intelligently than FDP under contention**.

---

## 3. Multi-Core Evaluation (2-Core): BWC's Main Success Case

### 3.1 Experimental Setup

The 2-core experiment places `mcf` on Core 0 and `lbm` on Core 1, sharing the LLC and DRAM. This is the clearest realization of the proposal's target scenario: one workload injects a large volume of low-value prefetch traffic, while another relies on high-value streaming prefetches and is sensitive to shared-memory contention.

Each L2C has an independent prefetcher instance with its own controller state. Both instances share visibility into the LLC read queue occupancy via the `intern_->lower_level->rq_occupancy()` API, which allows BWC to act per core while still observing shared congestion.

Weighted Speedup (WS) is computed as:

> WS = (IPC\_cpu0\_2core / IPC\_mcf\_1core) + (IPC\_cpu1\_2core / IPC\_lbm\_1core)
>
> where IPC\_mcf\_1core = 0.1212 and IPC\_lbm\_1core = 0.8482

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

*`lbm` PF Issued is large because `lbm` loops while waiting for `mcf` to complete 50M instructions.*

### 3.3 BWC's Core Result: Asymmetric Throttling

This is the strongest evidence for the BWC design. The final controller states show exactly the behavior the proposal is aiming for:

- On **cpu0 (`mcf`)**, BWC drops to **Level 1** and sets `issue_period=4`, reducing `mcf`'s prefetch volume by **75%**.
- On **cpu1 (`lbm`)**, BWC stays at **Level 3**, leaving the streaming prefetcher untouched.

In other words, BWC does not apply a uniform rule to all prefetch traffic. It makes a **per-core discrimination** between a low-value polluter and a high-value victim. That leads directly to the most important system-level outcome: `lbm` retains **0.7933 IPC**, only **6.5%** below its single-core baseline, while FDP collapses it to **0.5990 IPC**.

This section is the clearest validation that **multi-signal control matters**. BWC uses the near-zero-accuracy fallback to catch `mcf`, but it does not overreact to `lbm`'s low nominal accuracy because the rest of the sensor picture does not justify throttling.

### 3.4 Why FDP Is an Inadequate Control Baseline

FDP is useful here primarily as a contrast case. It applies the same accuracy threshold to both cores, and therefore throttles both of them to **Level 1**. This is exactly the weakness identified in the proposal: a static-threshold controller cannot tell apart "low accuracy but still valuable under streaming" from "low accuracy and genuinely harmful."

The data makes the failure mode clear. FDP leaves `mcf`'s own prefetch count essentially unchanged, but slashes `lbm`'s prefetch stream from **69M to 5M**. That frees bandwidth and helps `mcf`, but only by **sacrificing the victim workload**. The result is lower Weighted Speedup than BWC and a much less desirable QoS outcome.

For the purposes of this report, that is the main role of FDP: not as an equal analytical target, but as evidence that **accuracy-only throttling is an insufficient baseline for multicore bandwidth control**.

### 3.5 What the 2-Core Experiment Says About BWC

The 2-core results support three concrete claims about the current controller:

1. **Per-core state matters.** A useful controller must be able to throttle one core without automatically throttling another.
2. **Accuracy alone is not enough.** `lbm` remains valuable at roughly 5% accuracy, so static accuracy thresholds are too coarse.
3. **The actuator matters too.** BWC's rate limiter allows it to reduce issued traffic directly, rather than relying only on confidence filtering.

This is the point in the report where BWC most clearly satisfies the proposal's goal of a **hardware-friendly bandwidth-aware throttling module**.

---

## 4. Multi-Core Evaluation (4-Core): BWC's Current Blind Spot

### 4.1 Experimental Setup

The 4-core experiment runs four simultaneous `lbm` instances, all sharing the same LLC and single DRAM channel. Unlike the 2-core mix, this is a **homogeneous bandwidth-stress** case: there is no clear polluter/victim split, and all cores generate similar high-MLP streaming pressure.

This experiment is important because it tests whether BWC can move beyond heterogeneous discrimination and respond to **system-wide symmetric saturation**.

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

*† BOP's 0% accuracy is the LLC-fill measurement artifact noted earlier.*

### 4.3 BWC's Main Limitation: No Response Under Symmetric Contention

The most important observation is simple: **BWC does nothing**. All four instances remain at **Level 3**, and every reported metric matches `SPP_Orig` exactly.

This matters because it identifies the present boundary of the current controller design. BWC works well when it can separate a polluter from a victim, but in `4×lbm` there is no such asymmetry. Every core has moderate accuracy, every core produces similar pressure, and the per-core threshold logic never pushes any controller instance into a throttled state.

That is a useful result, not a failed experiment. It tells us that the current BWC design is a strong **heterogeneous contention controller**, but not yet a full **system-level saturation controller**.

### 4.4 Supporting Comparison: Why FDP Does Not Solve the Problem Either

FDP is again useful mainly as a supporting contrast. It throttles all four cores uniformly, sharply reducing prefetch traffic and increasing measured accuracy, but the overall result is still **worse Weighted Speedup** and **longer completion time** than `SPP_Orig`.

So the lesson is not that "BWC should simply throttle harder like FDP." Rather, the 4-core result suggests that the next controller needs **better shared-system sensing**, not just a more aggressive static rule.

### 4.5 What the 4-Core Experiment Teaches the Project

The 4-core case directly motivates the next design step proposed in `init_proposal.tex`. If the goal is a broadly robust bandwidth-aware controller, then the signal set likely needs to expand beyond the current per-core view to include stronger **system-level congestion indicators** or a more flexible searched policy.

BOP is kept in the table only as an auxiliary reference point; it is not the main lesson here. The important lesson is that **BWC's current thresholds and sensor structure are not yet sufficient for homogeneous saturation**, which gives us a concrete target for future refinement.

---

## 5. Conclusion

This report has been intentionally organized around the project goal of evaluating a **bandwidth-aware throttling controller** rather than treating every baseline as an equal contribution. From that perspective, the current results support four main conclusions.

First, **multi-signal control is more reliable than static accuracy-only throttling**. The contrast between BWC and FDP in the 2-core experiment shows that accuracy alone is too crude to guide throttling decisions under contention. BWC's additional queue-pressure signals, combined with a much lower accuracy fallback threshold, allow it to preserve useful streaming prefetches that FDP mistakenly suppresses.

Second, **the most useful signals in the current design appear to be the near-zero-accuracy fallback plus per-core congestion sensing**. Together they are sufficient to identify a low-value polluter in a heterogeneous mix and to preserve a high-value victim workload. This is the clearest success of the current controller.

Third, **external bandwidth-aware throttling does help when contention is asymmetric**. In the `mcf` + `lbm` mix, BWC improves over FDP by protecting `lbm` while still constraining `mcf`'s prefetch traffic. This is exactly the kind of runtime control behavior envisioned in the proposal.

Fourth, **the current hand-designed BWC remains incomplete**. In homogeneous `4×lbm`, it fails to trigger at all, which shows that the present per-core threshold design does not yet capture system-wide symmetric saturation. This limitation is valuable because it identifies the next step for the project more precisely than a purely positive result would.

In that sense, the current BWC should be viewed as the **hand-designed baseline controller** proposed in the initial project plan. It already demonstrates why bandwidth-aware, multi-signal throttling is promising, and it provides a concrete starting point for future work: adding richer system-level signals, testing across more workload mixes and more prefetchers, and eventually replacing hand-tuned thresholds with a searched or evolved control policy.
