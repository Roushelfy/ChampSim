# BWC Adaptive Selector: Pair-Aware Runtime Prefetcher Selection in ChampSim

Zhaofeng Luo and Kijun Shin  
15-740 Computer Architecture

## 0. Abstract

Hardware prefetchers can hide memory latency, but aggressive prefetching can also
consume shared cache capacity and off-chip bandwidth. This effect is especially
important in multicore systems, where a prefetch stream that is locally plausible
for one core may delay demand requests from another core. Our project addresses
this prefetch-control problem in ChampSim. We focus on L2 data prefetching and
build a pair-aware runtime selector, called the BWC Adaptive Selector, that
chooses among three expert policies: original SPP, an FDP-style SPP variant, and
BOP.

The selector observes local stream features such as page growth, line growth,
small-delta ratio, and RFO share. In multicore runs, it also observes shared and
peer signals such as local queue pressure, peer page-growth behavior, peer
small-delta behavior, and whether the peer looks like an `lbm`-like dense
stream. A local classifier first proposes an expert, and a pair coordinator then
remaps risky choices before the expert is activated. We used OpenEvolve in the
final phase to refine only this pair-coordination logic, leaving the expert
prefetchers unchanged.

Our reported final selector improves all four listed single-core workloads over
the SPP baseline, with gains from +0.8569% to +2.6685% IPC. It also improves all
ten reported two-core pairs, with weighted-speedup gains from +0.0515% to
+4.2142%. The Phase 2 OpenEvolve best point specifically improves harmful
`lbm`-related pair cases: `bzip2 + lbm` moves from -0.0286% to +0.0820%, while
`mcf + lbm` remains negative but improves its worst-case loss from -1.0157% to
-0.5410%. The main insight is that prefetcher control should be peer-aware:
locally reasonable prefetch decisions can become globally harmful when shared
resources are contended.

## 1. Introduction

Modern data prefetchers exploit regularity in memory accesses to fetch cache
lines before the core demands them. This can significantly improve performance
for streaming or structured workloads. However, prefetching is not free. A
prefetch request occupies queues, MSHRs, cache space, and DRAM bandwidth. In a
single-core setting, these costs are already hard to balance. In a multicore
setting, the problem becomes more subtle because a core's prefetch traffic can
hurt a peer that shares the LLC and memory system.

The project began from a bandwidth-aware control question: can a runtime policy
control an SPP-like prefetcher better than static or accuracy-only throttling?
Accuracy is an important signal, but it is incomplete. A stream can have low
cache-level accuracy while still being valuable, and a confident predictor can
still create harmful shared-resource pressure. This motivated us to treat
prefetching as a control problem rather than only an address-prediction problem.

The final artifact in this repository is the BWC Adaptive Selector. Instead of
designing a new predictor, it chooses among existing experts with different
behaviors. The experts are `spp_orig`, `spp_dev` with FDP-style controls, and
`bop`. This decomposition lets the selector use simple runtime signatures to ask
two questions. First, what does the local stream look like? Second, does the
peer make the locally preferred choice risky?

Our main research question is:

Can a lightweight runtime selector use local stream features and peer/shared
signals to choose safer prefetch behavior than a single static policy?

The answer from our reported experiments is yes for the evaluated suite. The
final selector improves every listed single-core and two-core result over the
SPP baseline. At the same time, the Phase 2 mini-evaluation shows a useful
limitation: OpenEvolve reduced harm in `mcf + lbm`, but did not fully eliminate
the regression in that smaller 2M warmup plus 10M simulation setting. This
mixed result is still valuable because it identifies the difficult case: pairs
where protecting a dense stream while controlling sparse or irregular traffic
requires more than a local prefetcher choice.

## 2. Related Work

Feedback Directed Prefetching (FDP) by Srinath et al. is the closest classic
baseline for our control goal. FDP adjusts prefetch aggressiveness using
feedback such as prefetch accuracy, timeliness, and pollution, with the goal of
improving performance while reducing bandwidth waste
([Srinath et al., HPCA 2007](https://hps.ece.utexas.edu/pub/srinath_hpca07.pdf)).
Our `spp_dev` expert includes an FDP-style controller, but the final project
builds above it: rather than applying one feedback policy everywhere, we select
among experts and use pair signals to avoid choices that are unsafe under shared
contention.

The original SPP prefetcher, Path Confidence Based Lookahead Prefetching, is a
strong modern prefetching baseline. It uses path signatures and confidence to
look ahead in memory streams
([Kim et al., MICRO 2016](https://dblp.org/rec/conf/micro/KimPGRWC16.html)).
Our project keeps SPP as an important expert because many workloads benefit
from its coverage. The central difference is that we do not assume SPP should
always be used unmodified; the selector can choose SPP, a more conservative
FDP-style SPP variant, or BOP depending on the observed stream.

Best-Offset Prefetching (BOP) by Michaud searches for spatial offsets that are
useful for the current workload
([Michaud, HPCA 2016](https://core.ac.uk/outputs/48160133/)). BOP gives our
selector a different prediction style from SPP. In the final design, BOP is
most useful as a candidate expert for sparse or high-page-growth streams, but
it can be blocked or remapped in pair mode when the peer signature suggests
shared-resource risk.

ChampSim is the trace-based simulator used for this work. Its local citation
entry describes ChampSim as an architectural simulator for education and
competition, and the repository includes SPP-related publication metadata in
`PUBLICATIONS_USING_CHAMPSIM.bib`. ChampSim is a good fit for this project
because prefetchers are modular, configurations can instantiate different core
counts, and the simulator reports IPC and multicore statistics needed for
weighted-speedup evaluation.

OpenEvolve is an evolutionary coding and optimization framework that combines
LLM-guided mutations with a MAP-Elites style quality-diversity database
([OpenEvolve GitHub](https://github.com/algorithmicsuperintelligence/openevolve)).
Our original proposal planned to use OpenEvolve to optimize an SPP controller
parameter space. The final implementation uses the same spirit in a narrower
way: Phase 2 searches changes to the pair-coordination rules in
`coordinate_candidate()` while keeping the expert prefetcher implementations
fixed. This made the search target interpretable and reduced the risk that a
candidate would overfit by changing unrelated predictor internals.

## 3. Method

### Implementation Overview

The main implementation is in `prefetcher/adaptive_selector/`. The selector is
implemented as a ChampSim L2 prefetcher and owns three internal experts:
`spp_orig`, `spp_dev`, and `bop`. The single-core and two-core configurations
are in `configs/adaptive_selector_config.json` and
`configs/adaptive_selector_2core.json`. The Phase 2 OpenEvolve best point is
retained separately in `prefetcher/adaptive_selector_phase2_best/`.

Each demand access is recorded into a sliding window. From that window, the
selector computes four local features:

| feature | meaning |
|---|---|
| `page_growth` | fraction of recent accesses that touch distinct pages |
| `line_growth` | fraction of recent accesses that touch distinct cache lines |
| `small_delta_ratio` | fraction of consecutive accesses within a small line-distance |
| `rfo_share` | fraction of recent demand accesses that are RFOs |

The local classifier uses these features to pick an initial expert. High
page-growth streams can select BOP, dense small-delta streams tend toward
`spp_orig`, and low-page-growth cases can select the FDP-style expert. The
selector uses a decision streak before switching, so a transient window does
not immediately lock in a new expert. The active expert then handles normal
ChampSim prefetch callbacks, while metadata tags route cache-fill feedback back
to the expert that generated the prefetch.

### Pair-Aware Coordination

The selector becomes more interesting in multicore runs. Each core keeps its own
local selector state, but the implementation also stores shared snapshots of
recent pressure and stream features. The shared path records local pressure from
MSHR, prefetch-queue, and request-queue occupancy. It also tracks peer
`page_growth`, peer `small_delta_ratio`, peer pressure, whether the peer looks
`lbm`-like, and whether the peer has high page growth.

The final decision is made by `coordinate_candidate()`. This function receives
the local candidate and can remap it based on peer state. Examples include:

- Delaying early decisions until the peer has produced enough signal.
- Splitting low-page-growth pairs using the density difference between peers.
- Blocking or demoting BOP in pair mode when BOP is likely to create shared
  interference.
- Protecting dense `lbm`-like peers by preferring a lower-interference expert.
- Promoting FDP when an `lbm`-like stream is paired with a high-page-growth peer.

This design is deliberately rule-based and hardware-friendly. It uses counters,
ratios over short windows, threshold comparisons, and a small amount of shared
state. The intent is not to create a complex offline oracle, but to show that
even simple peer-aware rules can outperform a local-only prefetch choice.

### OpenEvolve Setup

The original OpenEvolve path in the project was a parameter search over an SPP
controller dictionary. That path defined candidate fields such as accuracy
fallback threshold, global MSHR thresholds, global LLC thresholds, issue periods,
hysteresis, and pressure aggregation mode. During the project, the retained
artifact shifted toward adaptive expert selection because this gave clearer
control over heterogeneous pair behavior.

For Phase 2, OpenEvolve was used in a narrower coordinator-only role. The search
kept the expert implementations fixed and changed only
`coordinate_candidate()`. The retained Phase 2 best point changed several pair
fallbacks from `orig` to `FDP`, especially dense-peer split handling,
pair-scope BOP demotion, and `lbm`-like peer protection. It also added two
runtime-signal guards: one for sparse/high-page-growth cores paired with an
`lbm`-like peer, and one for `lbm`-like cores paired with a high-page-growth
peer. This design choice made the evolved solution explainable: it targeted
harmful pair interactions without changing the base predictors.

### Evaluation Setup

The report uses the retained result ledger in the root-level `report.md`.
Single-core results are reported as IPC relative to `SPP_orig`. Two-core results
are reported as weighted speedup relative to `SPP_orig` pairs. The workload set
in the retained summaries includes `astar`, `mcf`, `lbm`, and `bzip2`, with both
heterogeneous and homogeneous two-core mixes.

The Phase 2 OpenEvolve mini-evaluation uses a smaller 2M warmup plus 10M
simulation setting and focuses on one single-core `lbm` case plus two
`lbm`-related pairs. The broader retained selector summary reports four
single-core workloads, six heterogeneous two-core pairs, and four homogeneous
two-core pairs.

## 4. Experimental Results

### Phase 2 OpenEvolve Best

The Phase 2 experiment compares the Phase 1 source against the retained
OpenEvolve best point. The best point leaves single-core `lbm` unchanged and
improves the two targeted pair cases.

| case | SPP_orig baseline | Phase 1 source | Phase 1 gain | Phase 2 best | Phase 2 gain | delta |
|---|---:|---:|---:|---:|---:|---:|
| `lbm` IPC | 0.866441 | 0.8696655919 | +0.3722% | 0.8696655919 | +0.3722% | +0.0000 pp |
| `bzip2 + lbm` WS | 1.792235 | 1.7917226370 | -0.0286% | 1.7937053011 | +0.0820% | +0.1106 pp |
| `mcf + lbm` WS | 1.256300 | 1.2435395759 | -1.0157% | 1.2495028378 | -0.5410% | +0.4747 pp |

| metric | Phase 1 source | Phase 2 best | delta |
|---|---:|---:|---:|
| `combined_score` | -6.2665 | -3.0105 | +3.2559 |
| `mean_gain_pct` | -0.2240% | -0.0289% | +0.1951 pp |
| `min_gain_pct` | -1.0157% | -0.5410% | +0.4747 pp |

The result is mixed but informative. `bzip2 + lbm` crosses from a small
regression to a small gain. `mcf + lbm` is still below the SPP baseline in this
mini-evaluation, so it is not a full success. However, the worst-case loss is
nearly halved, which shows that the evolved pair rules reduced the harmful
interaction that motivated the search.

### Single-Core IPC

The retained Phase 1 selector improves all four listed single-core workloads.
This is important because the selector's pair-aware logic should not be bought
by sacrificing isolated behavior.

| workload | baseline IPC | current IPC | gain |
|---|---:|---:|---:|
| `astar` | 0.1162440706 | 0.1176968376 | +1.2498% |
| `mcf` | 0.0930473819 | 0.0955303095 | +2.6685% |
| `lbm` | 0.8705538638 | 0.8874912094 | +1.9456% |
| `bzip2` | 2.1326240227 | 2.1508988605 | +0.8569% |

The largest single-core gain is on `mcf`, while `bzip2` has the smallest but
still positive gain. The `lbm` result matters because `lbm` is a dense streaming
workload that can be easy to over-throttle if a controller treats low nominal
accuracy as automatically harmful.

### Two-Core Heterogeneous Weighted Speedup

The heterogeneous pair results test whether the selector can handle asymmetric
workload behavior.

| pair | baseline WS | current WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | 1.7777866560 | 1.8249906616 | +2.6552% |
| `astar + bzip2` | 1.8710239670 | 1.8993594472 | +1.5144% |
| `bzip2 + mcf` | 1.8688840697 | 1.8943434680 | +1.3623% |
| `astar + lbm` | 1.3562017695 | 1.3751206959 | +1.3950% |
| `bzip2 + lbm` | 1.9383916295 | 1.9496149303 | +0.5790% |
| `mcf + lbm` | 1.2826469006 | 1.2974731255 | +1.1559% |

The largest heterogeneous gain is `mcf + astar` at +2.6552%. The `lbm` pairs
are especially useful for interpretation. They show the benefit of pair-aware
rules that can preserve dense streaming behavior while moderating the peer's
prefetch choices.

### Two-Core Homogeneous Weighted Speedup

The homogeneous pair results test whether the selector remains safe when both
cores have the same workload.

| pair | baseline WS | current WS | gain |
|---|---:|---:|---:|
| `astar + astar` | 1.5987553559 | 1.6661295598 | +4.2142% |
| `mcf + mcf` | 1.7032220285 | 1.7335549654 | +1.7809% |
| `lbm + lbm` | 1.0546594170 | 1.0564571762 | +0.1705% |
| `bzip2 + bzip2` | 2.0162830918 | 2.0173219541 | +0.0515% |

The largest reported gain overall is `astar + astar` at +4.2142%. The
`lbm + lbm` and `bzip2 + bzip2` gains are small, but their sign is still
important: the selector does not introduce a visible regression in these
retained summaries.

### Experimental Process

The commit history shows the project moving through several design stages.
Commit `40deaeb4` retained the first sliding-window selector baseline. Commit
`2535efab` retained a rule-2 adaptive selector state. Commit `051aaca8` added a
pair-safe selector state, and `bc78d4bf` generalized the pair behavior across
more retained workloads. Later commits `27258b89` and `7a743462` finalized the
Phase 1 adaptive selector, while `e65a5b4e` recorded the Phase 2 OpenEvolve best
point. This progression matters because many intermediate ideas were not kept:
the final design is the result of repeatedly testing whether a rule improved
one pair without breaking previously retained single-core and pair behavior.

## 5. Goals and Next Steps

The original proposal goal was an OpenEvolve-optimized bandwidth-aware
controller for SPP-style prefetching. We partially met that goal, but the final
artifact changed shape. Instead of only tuning one SPP controller, we built a
runtime expert selector that chooses among original SPP, FDP-style SPP, and BOP.
This shift made the final design more expressive because it can change both
aggressiveness and prefetcher style.

We did meet the broader architectural goal of demonstrating that runtime
prefetch control benefits from direct workload and peer signals. The selector
improves all retained single-core and two-core summaries, and the Phase 2
OpenEvolve result shows that coordinator-only search can reduce harmful pair
interactions. We did not fully meet the most ambitious original goal of a
broadly validated OpenEvolve policy over a large train/validation suite. The
current results are promising, but they should be treated as retained project
evidence rather than a complete generalization proof.

If we continued the project, the next steps would be:

- Evaluate more held-out workload mixes, especially SPEC mixes beyond the four
  workloads in the retained report.
- Run longer simulations and repeat selected experiments to measure stability.
- Extend validation to four-core mixes where symmetric bandwidth pressure may
  expose different failure modes.
- Use OpenEvolve over a broader but still constrained coordinator rule space.
- Add ablations that disable one pair rule at a time, so each rule's value is
  measured directly.
- Report prefetch traffic, cache pollution, and queue-pressure metrics
  alongside IPC and weighted speedup.

## 6. Collaboration

Zhaofeng Luo focused on experiment infrastructure, running ChampSim evaluations,
collecting IPC and weighted-speedup summaries, preparing the final result tables
and poster figures, and leading the final report and poster writing.

Kijun Shin focused on the adaptive-selector implementation, pair-coordination
rules, and OpenEvolve-guided Phase 2 refinement. He also helped debug the
coordinator behavior and interpret the evolved changes.

Both members jointly designed the BWC selector idea, studied related prefetching
work, discussed evaluation methodology, reviewed intermediate results, and
refined the final project narrative.

## 7. Conclusion

This project studied prefetch control in ChampSim under the observation that
locally reasonable prefetching can be globally harmful. The final BWC Adaptive
Selector uses short-window stream features to choose an expert prefetcher and
then uses peer/shared signals to avoid risky choices in two-core settings.

The retained results show positive gains for every listed single-core workload
and every listed two-core pair. The Phase 2 OpenEvolve best point further shows
that narrow, coordinator-only evolution can improve difficult `lbm`-related
pairs without changing expert prefetcher internals. The most important lesson is
that prefetch control should not be purely local. A selector needs to ask not
only whether a prefetcher matches the current stream, but also whether that
choice is safe for the peer sharing the memory system.

## References

1. Santhosh Srinath, Onur Mutlu, Hyesoon Kim, and Yale N. Patt. "Feedback
   Directed Prefetching: Improving the Performance and Bandwidth-Efficiency of
   Hardware Prefetchers." HPCA 2007.
   https://hps.ece.utexas.edu/pub/srinath_hpca07.pdf

2. Jinchun Kim, Seth H. Pugsley, Paul V. Gratz, A. L. Narasimha Reddy, Chris
   Wilkerson, and Zeshan Chishti. "Path Confidence Based Lookahead
   Prefetching." MICRO 2016.
   https://dblp.org/rec/conf/micro/KimPGRWC16.html

3. Pierre Michaud. "Best-Offset Hardware Prefetching." HPCA 2016.
   https://core.ac.uk/outputs/48160133/

4. Nathan Gober et al. "The Championship Simulator: Architectural Simulation
   for Education and Competition." ChampSim citation entry in
   `PUBLICATIONS_USING_CHAMPSIM.bib`.

5. OpenEvolve project repository.
   https://github.com/algorithmicsuperintelligence/openevolve
