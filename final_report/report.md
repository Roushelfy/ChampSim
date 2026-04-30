# BWC Adaptive Selector: Pair-Aware Runtime Prefetcher Selection in ChampSim

Zhaofeng Luo and Kijun Shin  
15-740 Computer Architecture

This Markdown file is the detailed draft/reference version of the final report.
The submission-ready version is `report.tex` / `report.pdf`.

## 0. Abstract

Hardware prefetchers hide memory latency, but aggressive prefetch traffic can
consume shared cache capacity, memory queues, and off-chip bandwidth. This is
especially important in multicore systems, where a locally plausible prefetch
stream can delay demand requests from a peer. We address this prefetch-control
problem in ChampSim by building the BWC Adaptive Selector, a pair-aware L2
runtime selector over original SPP, an FDP-style SPP variant, and BOP.

BWC uses local stream features such as page growth, line growth,
small-delta ratio, and RFO share, then uses peer/shared signals to decide
whether the local expert choice is safe under contention. Across Phase 1 scale
sweeps at 1M+5M, 2M+10M, and 4M+20M, average gains remain positive for both
single-core IPC and two-core weighted speedup. Individual rows are mixed,
especially for some `lbm`-related pairs and near-neutral `bzip2` and `lbm`
single-core cases. OpenEvolve was then used in a focused Phase 2 2M+10M
diagnostic to refine only pair coordination, improving `bzip2 + lbm` and
reducing the `mcf + lbm` worst-case loss.

## 1. Introduction

Modern data prefetchers exploit regularity in memory accesses to fetch cache
lines before the core demands them. However, prefetches occupy queues, MSHRs,
cache space, and DRAM bandwidth. In multicore systems these costs cross core
boundaries, so a prefetcher that helps one core locally can hurt a peer through
shared LLC and memory-system pressure.

The final artifact is the BWC Adaptive Selector. Rather than inventing a new
predictor, it chooses among existing experts with different behaviors:
`spp_orig`, `spp_dev` with FDP-style controls, and `bop`. The selector asks two
questions on every stable window: what does the local stream look like, and
does the peer make the locally preferred choice risky?

The scale sweep shows positive average behavior over a static `SPP_orig`
baseline at all tested simulation lengths, but it also exposes fragility in
individual cases. Longer runs reveal small regressions for several
`lbm`-related pairs, so the final conclusion is not that every case is solved.
Instead, peer-aware selection improves the average tradeoff and points clearly
to the remaining hard interactions.

## 2. Related Work

Feedback Directed Prefetching (FDP) by Srinath et al. dynamically adjusts
prefetch aggressiveness using feedback such as accuracy, timeliness, and
pollution. Our `spp_dev` expert contains an FDP-style controller, but the final
design builds above it: it selects among experts and uses pair signals to avoid
choices that are unsafe under shared contention.

The Signature Path Prefetcher (SPP), based on path-confidence lookahead
prefetching, is our main SPP baseline. Best-Offset Prefetching (BOP) searches
for spatial offsets that work for the current stream. In our design, SPP
represents the strong default lookahead prefetcher, the FDP-style variant
represents a feedback-controlled SPP mode, and BOP represents an offset-oriented
alternative. BWC differs by adding a selector and pair coordinator above the
experts rather than replacing their address-generation logic.

ChampSim is the trace-based simulator used to evaluate these designs.
OpenEvolve is an evolutionary coding framework with LLM-guided mutation and
quality-diversity search. We use it in a narrow, coordinator-only way so the
evolved design remains interpretable.

## 3. Method

The main implementation is in `prefetcher/adaptive_selector/`. The Phase 2
retained best point is stored separately in
`prefetcher/adaptive_selector_phase2_best/`.

| Stage | Main function / artifact | Role |
|---|---|---|
| Feature extraction | `compute_window_features()` | Computes `page_growth`, `line_growth`, `small_delta_ratio`, and `rfo_share`. |
| Local selection | `classify_window()` | Produces the local `orig`, `FDP`, or `BOP` candidate. |
| Shared context | `refresh_shared_pressure()` | Records local pressure plus peer page/delta signatures and `peer_lbm_like`. |
| Pair coordination | `coordinate_candidate()` | Remaps or blocks risky pair choices before activation. |
| Configurations | `adaptive_selector` configs | Instantiate the selector in single-core and two-core ChampSim runs. |

For OpenEvolve, mutations were constrained to the pair-coordination decision
path, primarily `coordinate_candidate()`, while the expert prefetchers and
ChampSim interfaces stayed fixed. The prompt emphasized preserving the Phase 1
selector behavior while improving fragile `lbm`-related pair cases. Each
candidate was evaluated on a focused 2M-warmup plus 10M-simulation mini-suite
containing single-core `lbm`, `bzip2 + lbm`, and `mcf + lbm`. The score combined
mean gain with a worst-case guardrail. The reported `combined_score` is this
internal OpenEvolve ranking objective; higher is better, and it is used only to
rank Phase 2 candidates rather than as a separate architectural metric.

## 4. Experimental Results

### Phase 1 Final Scale Sweep

Baseline is `spp_orig`; current is the phase 1 final `adaptive_selector`.
Single-core rows report IPC, and two-core rows report weighted speedup (WS).
Runs were completed on `office-kijun` with at most 3 sweep jobs in parallel.

| scale | single-core average gain | two-core average gain |
|---|---:|---:|
| `1M + 5M` | `+1.5464%` | `+1.3677%` |
| `2M + 10M` | `+1.8540%` | `+1.6013%` |
| `4M + 20M` | `+2.6782%` | `+1.3017%` |

### 1M + 5M Single-Core IPC

| workload | baseline IPC | adaptive IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.114681681637` | `0.117696837623` | `+2.6292%` |
| `mcf` | `0.0921060128519` | `0.0955303095423` | `+3.7178%` |
| `lbm` | `0.888119994224` | `0.887491209405` | `-0.0708%` |
| `bzip2` | `2.15285110626` | `2.15089886045` | `-0.0907%` |

### 1M + 5M Two-Core WS

| pair | baseline WS | adaptive WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.79889602604` | `1.84664106396` | `+2.6541%` |
| `astar + bzip2` | `1.8733459097` | `1.90206011146` | `+1.5328%` |
| `bzip2 + mcf` | `1.86822168103` | `1.89396475041` | `+1.3779%` |
| `astar + lbm` | `1.34207381864` | `1.36131871857` | `+1.4340%` |
| `bzip2 + lbm` | `1.90987686659` | `1.92099258692` | `+0.5820%` |
| `mcf + lbm` | `1.28367655124` | `1.28212254127` | `-0.1211%` |
| `astar + astar` | `1.62053632111` | `1.68882841105` | `+4.2142%` |
| `mcf + mcf` | `1.72062980105` | `1.75127275557` | `+1.7809%` |
| `lbm + lbm` | `1.03379930233` | `1.03556150363` | `+0.1705%` |
| `bzip2 + bzip2` | `1.99733913116` | `1.99836823288` | `+0.0515%` |

### 2M + 10M Single-Core IPC

| workload | baseline IPC | adaptive IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.123092697685` | `0.127849553135` | `+3.8644%` |
| `mcf` | `0.133869243938` | `0.138126056966` | `+3.1798%` |
| `lbm` | `0.866440779824` | `0.869665591905` | `+0.3722%` |
| `bzip2` | `2.15021252696` | `2.15019865681` | `-0.0006%` |

### 2M + 10M Two-Core WS

| pair | baseline WS | adaptive WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.67757932858` | `1.75129060943` | `+4.3939%` |
| `astar + bzip2` | `1.85535929286` | `1.89702527774` | `+2.2457%` |
| `bzip2 + mcf` | `1.85014350971` | `1.89079633825` | `+2.1973%` |
| `astar + lbm` | `1.32822308169` | `1.34662973574` | `+1.3858%` |
| `bzip2 + lbm` | `1.7922349941` | `1.7917228544` | `-0.0286%` |
| `mcf + lbm` | `1.2563002705` | `1.24353980771` | `-1.0157%` |
| `astar + astar` | `1.61198327646` | `1.69157360275` | `+4.9374%` |
| `mcf + mcf` | `1.68285731121` | `1.7132872924` | `+1.8082%` |
| `lbm + lbm` | `1.05260903871` | `1.05335202216` | `+0.0706%` |
| `bzip2 + bzip2` | `1.99892024318` | `1.99929371345` | `+0.0187%` |

### 4M + 20M Single-Core IPC

| workload | baseline IPC | adaptive IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.127089889173` | `0.134589236644` | `+5.9008%` |
| `mcf` | `0.12300024918` | `0.128947331077` | `+4.8350%` |
| `lbm` | `0.868004562405` | `0.867861433966` | `-0.0165%` |
| `bzip2` | `2.17156526525` | `2.17142200753` | `-0.0066%` |

### 4M + 20M Two-Core WS

| pair | baseline WS | adaptive WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.67968369463` | `1.75831774679` | `+4.6815%` |
| `astar + bzip2` | `1.90015563102` | `1.95292292993` | `+2.7770%` |
| `bzip2 + mcf` | `1.91154295712` | `1.93582008508` | `+1.2700%` |
| `astar + lbm` | `1.30142044017` | `1.29975024337` | `-0.1283%` |
| `bzip2 + lbm` | `1.7056566415` | `1.70318728384` | `-0.1448%` |
| `mcf + lbm` | `1.25495645906` | `1.24672830361` | `-0.6557%` |
| `astar + astar` | `1.64374110449` | `1.71254953306` | `+4.1861%` |
| `mcf + mcf` | `1.71524235015` | `1.73465860811` | `+1.1320%` |
| `lbm + lbm` | `1.05203463194` | `1.05080569528` | `-0.1168%` |
| `bzip2 + bzip2` | `1.96177282621` | `1.96207781216` | `+0.0155%` |

### Phase 2 OpenEvolve Best

Phase 2 is a focused 2M+10M diagnostic. It targets the fragile `lbm` cases
exposed by the sweep and should not be read as a replacement for the full scale
sweep.

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

Here `combined_score` is the internal Phase 2 ranking objective; higher is
better. It combines average gain with a worst-case guardrail so candidates do
not trade one large pair regression for a small mean improvement.

## 5. Goals and Next Steps

The original proposal goal was an OpenEvolve-optimized bandwidth-aware SPP
controller. We partially met that goal, but the final artifact shifted to a
stronger adaptive-selector formulation. Instead of tuning a single SPP
controller, we select among SPP, FDP-style SPP, and BOP. This preserves the
proposal's larger goal: using automated search to improve prefetch control
under shared-resource pressure.

Future work should evaluate more held-out SPEC mixes, run longer and repeated
simulations, validate four-core behavior, and use OpenEvolve over a broader but
still constrained coordinator rule space. The scale sweep makes the most useful
next analysis clear: ablate each pair rule and report prefetch traffic, cache
pollution, and queue pressure alongside IPC and weighted speedup.

## 6. Collaboration

Zhaofeng Luo focused on experiment infrastructure, running ChampSim evaluations,
collecting IPC and weighted-speedup summaries, preparing the final result
tables and poster figures, and leading the final report and poster writing.

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

The scale sweep shows positive average gains at all tested lengths, but also
exposes remaining fragility in individual `lbm`-related cases. The Phase 2
OpenEvolve best point further shows that narrow, coordinator-only evolution can
reduce those difficult interactions without changing expert predictor internals.
The central lesson is that the right prefetcher for one core is not always the
right prefetcher for the system.

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

4. Nathan Gober, Gino Chacon, Lei Wang, Paul V. Gratz, Daniel A. Jimenez,
   Elvira Teran, Seth H. Pugsley, and Jinchun Kim. "The Championship Simulator:
   Architectural Simulation for Education and Competition." arXiv:2210.14324,
   2022. https://arxiv.org/abs/2210.14324

5. OpenEvolve project repository.
   https://github.com/algorithmicsuperintelligence/openevolve
