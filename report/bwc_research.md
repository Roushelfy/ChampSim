# BWC Research Unified Report

This is the single active report for the current repo state.

- date: `2026-04-21`
- host: `office-kijun`
- run length: `1M warmup + 5M simulation`
- active goal: signal-based adaptive selector that keeps at least `85%` of the current retained gain on all single-core workloads and on `2-core mcf + lbm`
- status: achieved in this cycle; the retained artifact is a pair-safe adaptive selector with one-shot `orig/fdp` coordination in `2-core mcf + lbm`

Latest retained result:

- single-core retained exact IPCs remain the same as the previous `rule2` selector:
  - `astar = 0.11769683762267738`
  - `mcf = 0.0955303095422819`
  - `lbm = 0.8874912094048452`
  - `bzip2 = 2.150898860454012`
- new retained `2-core mcf + lbm` adaptive result:
  - `weighted_speedup = 1.297473125485411`
  - `orig_orig` baseline `= 1.282646900577691`
  - retained static `orig_fdp = 1.294978831807267`
- this new adaptive point clears the pair `85%` threshold and also exceeds the previous retained static winner
- repo cleanup state:
  - active retained prefetcher: `adaptive_selector`
  - retained internal expert/build dependencies only: `bop`, `spp_orig`, `spp_dev`, `no`

## 1. Current Targets

Current retained single-core `85%` thresholds:

| workload | retained exact IPC | `85%` minimum acceptable IPC |
|---|---:|---:|
| `astar` | `0.11769683762267738` | `0.11747892256899518` |
| `mcf` | `0.0955303095422819` | `0.09515787039793643` |
| `lbm` | `0.8874912094048452` | `0.8849506075610004` |
| `bzip2` | `2.150898860454012` | `2.148157634794696` |

Current retained pair target:

| item | exact value |
|---|---:|
| `orig_orig` pair baseline WS | `1.282646900577691` |
| retained best overall pair WS (`rule3`) | `1.297473125485411` |
| current forward `85%` minimum acceptable pair WS | `1.295249191749253` |

## 2. 2026-04-21 Pair-Safe Adaptive Result

What changed in code:

- the multicore path in [adaptive_selector.h](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.h) and [adaptive_selector.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.cc) was simplified into a pair-safe one-shot coordinator
- shared coordination is now refreshed at evaluation time by default instead of every cycle
- `2-core` pair mode now defaults to:
  - hold until the peer window is ready
  - demote `bop -> orig` for the `high_page` side
  - promote `orig -> fdp` for the `low_page` side when the peer is `high_page`
  - lock after the first committed switch
- a combined regression runner was added at [run_adaptive_retention_suite.py](/Users/shinkijun/Developers/ChampSim/scripts/run_adaptive_retention_suite.py)

Idea families explored in this cycle:

1. current adaptive baseline on `2-core mcf + lbm`
2. low-page `orig -> fdp` promotion under shared pressure
3. peer-aware `bop -> orig` protection for `lbm`-like peers
4. `promote_fdp + peerprotect` with unlock
5. `promote_fdp + peerprotect` with lock-after-first-switch
6. high-page peer signal for earlier `orig -> fdp` promotion
7. execution-policy tuning: `3-way parallel` vs `single-run sequential`
8. pair-safe one-shot `orig/fdp` split with evaluation-time shared snapshots only

Subagent support used in parallel:

- idea-family analysis and knob design
- pair-aware selector patch proposal
- retained-suite script proposal
- runtime-cost diagnosis showing that the earlier `10-13m` aborts were premature relative to the actual `15m+` pair runtime

Exact retained source-of-truth files for this cycle:

- pair summary: [pair_rule3_default_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_pair_adaptive_20260421/main/summaries/pair_rule3_default_r1.json)
- pair raw log: [pair_rule3_default_r1.txt](/Users/shinkijun/Developers/ChampSim/results/cycle_pair_adaptive_20260421/main/logs/pair_rule3_default_r1.txt)
- single-core aggregate: [rule3_singlecore_aggregate.json](/Users/shinkijun/Developers/ChampSim/results/cycle_pair_adaptive_20260421/main/single/summaries/rule3_singlecore_aggregate.json)
- full local aggregate: [rule3_retention_suite.json](/Users/shinkijun/Developers/ChampSim/results/cycle_pair_adaptive_20260421/main/summaries/rule3_retention_suite.json)

## 3. Quantitative Outcome

Single-core validation:

| workload | exact IPC | exact delta vs `orig` | retention vs current retained exact | `85%` gate |
|---|---:|---:|---:|---|
| `astar` | `0.11769683762267738` | `+0.0014527670245479407` (`+1.2498%`) | `100%` | pass |
| `mcf` | `0.0955303095422819` | `+0.0024829276289698027` (`+2.6685%`) | `100%` | pass |
| `lbm` | `0.8874912094048452` | `+0.01693734562563154` (`+1.9456%`) | `100%` | pass |
| `bzip2` | `2.150898860454012` | `+0.018274837728774695` (`+0.8569%`) | `100%` | pass |

`2-core mcf + lbm` validation:

| item | exact value |
|---|---:|
| adaptive `rule3` WS | `1.297473125485411` |
| `orig_orig` baseline WS | `1.282646900577691` |
| retained static `orig_fdp` WS | `1.294978831807267` |
| previous-cycle pair `85%` minimum WS | `1.2931290421228305` |
| new forward pair `85%` minimum WS | `1.295249191749253` |
| adaptive delta vs `orig_orig` | `+0.01482622490772001` (`+1.1559%`) |
| adaptive delta vs static `orig_fdp` | `+0.0024942936781440217` (`+0.1926%`) |
| gain retention vs previous retained pair target | `120.2263%` |

Interpretation:

- the retained pair winner is no longer static `orig_fdp`; it is now the adaptive selector itself
- the pair log shows that the winner used runtime signal only:
  - `cpu0`: `page_growth=0.96875`, `small_delta_ratio=0.0`, switched to `orig`
  - `cpu1`: `page_growth=0.03125`, `small_delta_ratio=0.984127`, switched to `fdp`
- this reproduces the beneficial `orig/fdp` split without workload IDs
- the earlier runtime-barrier hypothesis was false; the pair run simply needed to be allowed to complete

## 4. Main Conclusion

The current retained best overall results are now:

- best overall single-core adaptive point: retained `rule2`
- best overall `2-core mcf + lbm` point: adaptive `rule3`

Why this artifact is the right retained one:

- it keeps the previous single-core retained behavior exactly
- it adds a narrow pair-safe override only when there are two cores and the peer window is ready
- it uses only generic runtime features:
  - `page_growth`
  - `small_delta_ratio`
  - peer readiness
  - peer `high_page` detection
- it clears every current gate:
  - all four single-core workloads pass the `85%` minimum
  - `2-core mcf + lbm` passes the pair `85%` minimum
  - the adaptive pair point beats the previous retained static winner

Most likely improvement directions from here:

1. keep the multicore path narrow and pair-safe instead of reintroducing a rich continuous controller
2. if more multicore pairs are added later, generalize the current `high_page/low_page_dense` split before adding heavier shared logic
3. only reintroduce pressure-gated continuous remap if a new pair cannot be covered by the current one-shot rule

## 5. Historical 2026-04-21 Early Barrier Note

Earlier in the same cycle, `10-13m` pair runs were treated as an execution-cost barrier and aborted.

That interpretation turned out to be wrong:

- the retained static winner log already showed that this pair can legitimately run for `18m23s` wall-clock before completion
- the final adaptive winner completed in `912.94s` (`15m12.94s`) on `office-kijun`
- the old runtime-barrier notes were folded into this report during cleanup instead of being kept as a separate temp artifact

## 6. Retained Static 2-Core Result

Current status:

| item | status |
|---|---|
| clean current-source `orig_orig` baseline freeze | achieved |
| true heterogeneous-core `orig_fdp` validation | achieved |
| target `+0.5%` WS over `orig` | achieved |
| adaptive/shared lane superiority over static winner | not achieved |
| source-of-truth retained summaries | achieved |

Winner details:

| workload | policy | IPC |
|---|---|---:|
| `mcf` | `orig` | `0.033295509641944825` |
| `lbm` | `fdp` | `0.8158351217788197` |

Why this is the retained source-of-truth:

- the copied raw winner summary had `weighted_speedup = null` and its `ipcs` followed raw CPU index order
- [loop02_orig_fdp_mcf_lbm_1M_5M.corrected.json](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/loop02_orig_fdp_mcf_lbm_1M_5M.corrected.json) remaps the final CPU lines using trace ownership from [loop02_orig_fdp_mcf_lbm_1M_5M.log](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/loop02_orig_fdp_mcf_lbm_1M_5M.log)
- after correction, the winner is internally consistent and exceeds the target by `0.005918696726687599` WS

## 7. Historical 2026-04-20 Static Cycle Summary

Idea families explored this cycle:

1. static hetero upper bound: `orig_fdp`
2. static specialist split: `bop_orig`
3. static inverse split: `orig_bop`
4. local per-core adaptive selector without shared coordination
5. shared-pressure demote of `bop` under congestion
6. gated `bop` activation after a minimum-demand burn-in
7. shared fallback to `orig` under pressure
8. recheck/unlock variants for previously locked adaptive decisions

Exact representative points from the retained ledger and subagent reports:

| family | representative point | exact WS | delta vs clean `orig_orig` |
|---|---|---:|---:|
| static hetero upper bound | `orig_fdp` | `1.294978831807267` | `+0.012331931229575988` |
| static specialist split | `bop_orig` | `1.2776023755104065` | `-0.005044525067284476` |
| static inverse split | `orig_bop` | `1.2733666633962868` | `-0.009280237181404177` |
| local adaptive selector | `adapt_noshared_1M_5M` | `1.2787758031123295` | `-0.003871097465361517` |
| shared-pressure demote | `loop3_shared_demote` | `1.2793912895358897` | `-0.0032556110418013873` |
| gated `bop` activation | `loop1_seed_gated_bop512` | `1.2745471803238275` | `-0.008099720253863519` |
| shared `orig` fallback | `loop2_shared_orig_fallback` | `1.275503345595213` | `-0.007143554982477974` |

Stop condition:

- the cycle stopped early once the clean current-source `orig_fdp` point cleared the final goal by `+0.9614%`
- after that, remaining adaptive/shared lanes were only used to bound how far they still were from the winner
- none of the adaptive/shared variants beat even the clean `orig_orig` baseline, so there was no reason to keep spending remote time on them in this cycle

### Static Archive 1. Why Adaptive And Shared Coordination Lost

Best adaptive/shared points still lost to the baseline:

| point | mcf delta IPC vs `orig_orig` | lbm delta IPC vs `orig_orig` | WS delta vs `orig_orig` | WS delta vs `orig_fdp` |
|---|---:|---:|---:|---:|
| `adapt_noshared_1M_5M` | `-0.0010679100191997026` (`-3.3092%`) | `+0.006621396649271638` (`+0.8128%`) | `-0.003871097465361517` | `-0.016203028694937505` |
| `loop3_shared_demote` | `-0.0012389303013252521` (`-3.8391%`) | `+0.008757281195653777` (`+1.0749%`) | `-0.0032556110418013873` | `-0.015587542271377375` |

Quantitative interpretation:

- every adaptive/shared lane in this cycle overprotected `lbm` at the expense of `mcf`
- the `lbm` gains were real but too small in weighted-speedup terms to compensate for the `mcf` slowdown
- the clean winner avoids that tradeoff entirely by leaving `mcf` on `orig` while giving `lbm` the lighter-weight `fdp` behavior
- in other words, this cycle's bottleneck was not lack of coordination logic; it was failure to preserve `mcf` enough while helping `lbm`

Important data-hygiene note:

- some copied worker trees produced inconsistent `orig_orig` values around `1.2993`, but those were discarded as non-source-of-truth because they came from stale/cross-workspace artifacts
- the retained current-source truth for this cycle is only the local copied files under [results/cycle_mcf_lbm_2core_20260420/retained](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained)

### Static Archive 2. Retained Files For This Cycle

- winner summary: [loop02_orig_fdp_mcf_lbm_1M_5M.corrected.json](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/loop02_orig_fdp_mcf_lbm_1M_5M.corrected.json)
- raw winner log: [loop02_orig_fdp_mcf_lbm_1M_5M.log](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/loop02_orig_fdp_mcf_lbm_1M_5M.log)
- clean baseline summary: [orig_baseline.summary.json](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/orig_baseline.summary.json)
- best adaptive no-shared point: [adapt_noshared_1M_5M.json](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/adapt_noshared_1M_5M.json)
- best shared-coordination point: [loop3_shared_demote.json](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_lbm_2core_20260420/retained/loop3_shared_demote.json)
- hetero config generator: [generate_hetero_2core_config.py](/Users/shinkijun/Developers/ChampSim/scripts/generate_hetero_2core_config.py)
- pair runner: [run_mcf_lbm_2core_eval.py](/Users/shinkijun/Developers/ChampSim/scripts/run_mcf_lbm_2core_eval.py)
- note: temporary collector helpers from that cycle were removed during cleanup; the retained summaries above are the source of truth

## 8. Compact Manual

Remote workspace:

```bash
mkdir -p /Users/kijunshi/Developers/ChampSim_pair_adapt_20260421
cd /Users/kijunshi/Developers/ChampSim_pair_adapt_20260421
ln -sfn /Users/kijunshi/Developers/ChampSim/vcpkg_installed vcpkg_installed
ln -sfn /Users/kijunshi/Developers/ChampSim/traces traces
```

Build, always sequential:

```bash
rm -f _configuration.mk
make configclean
./config.sh configs/adaptive_selector_config.json
make -j4 bin/champsim_adaptive_selector

python3 scripts/generate_hetero_2core_config.py \
  --base-config configs/orig_2core.json \
  --core0-prefetcher adaptive_selector \
  --core1-prefetcher adaptive_selector \
  --executable-name champsim_adaptive_selector_2core \
  --output /tmp/adaptive_selector_2core.json

rm -f _configuration.mk
make configclean
./config.sh /tmp/adaptive_selector_2core.json
make -j4 bin/champsim_adaptive_selector_2core
```

Validate:

```bash
python3 scripts/run_adaptive_retention_suite.py \
  --single-binary ./bin/champsim_adaptive_selector \
  --pair-binary ./bin/champsim_adaptive_selector_2core \
  --label candidate_name \
  --results-dir results/cycle_pair_adaptive_20260421
```

Guardrails:

- never build `1-core` and `2-core` configs concurrently in the same workspace
- avoid `3-way` parallel pair runs
- do not abort `mcf+lbm` just because there is no summary at `10-13m`; the retained adaptive winner took `15m12.94s`

## Historical Archive: Single-Core Sliding-Window Selector

The remaining sections are preserved from the previous single-core cycle.

### Archive 1. Executive Summary

Current status:

| item | status |
|---|---|
| `lbm` reproducibility root cause isolation | achieved |
| current-source single-core landscape refreeze | achieved |
| retained sliding-window selector implementation | achieved |
| `4/4` current-source selector validation | achieved |
| repeated `lbm` selector reproducibility | achieved, bit-identical |

Final retained artifact:

- selector code: [adaptive_selector.h](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.h), [adaptive_selector.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.cc)
- selector config: [adaptive_selector_config.json](/Users/shinkijun/Developers/ChampSim/configs/adaptive_selector_config.json)
- historical single-core helper scripts from that cycle were removed during cleanup; the retained summaries below are the source of truth

Final retained selector rule (`rule2`):

- recent window: `64` demand-memory accesses
- evaluate every `16` demand-memory accesses
- require `2` consecutive identical candidates before switching
- lock after first switch
- decision rule:
  - if `page_growth >= 0.40` -> `bop`
  - else if `small_delta_ratio >= 0.70` -> `orig`
  - else if `page_growth < 0.17` -> `fdp`
  - else -> `orig`

Main conclusion:

- the earlier `lbm` drift was real measurement corruption, not a valid retained point
- the root causes were SPP-family lookahead queue overflow plus signed-shift UB, and a temporary stream-mismatch episode from misaligned selector/base config experiments
- after fixing those issues and refreezing the current-source landscape, the retained selector meets the `70%` retention goal on all four workloads

### Archive 2. Root Cause And Fix

Root-cause ranking:

1. `PATTERN_TABLE::read_pattern` queue-bound corruption in SPP-family code
   - [spp_orig.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.cc:367)
   - [spp_dev.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_dev/spp_dev.cc:400)
2. signed-shift undefined behavior in lookahead base-address reconstruction
   - [spp_orig.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.cc:121)
   - [spp_dev.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_dev/spp_dev.cc:129)
3. selector/base expert stream mismatch during intermediate experiments
   - selector path: [adaptive_selector.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.cc:80)
   - config alignment point: [adaptive_selector_config.json](/Users/shinkijun/Developers/ChampSim/configs/adaptive_selector_config.json:81)

What was fixed:

- SPP queue growth is now bounded by actual queue capacity
- stale sentinel-style `pf_q_tail++` growth is removed
- negative deltas now use block-number arithmetic instead of signed left shift
- retained selector config is aligned with the current-source expert binaries

What was ruled out:

- `vmem` randomization / seed drift
- trace-reader nondeterminism
- remote host randomness independent of code

Key reproducibility evidence:

- repeated current-source direct `lbm + orig` runs:
  - [direct_orig_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_orig_matrix_r1.json)
  - `lbm = 0.8881199942236687`
- repeated current-source selector `lbm` runs:
  - [repro_rule2_full_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_full_r1.json)
  - [repro_rule2_lbm_r2.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_lbm_r2.json)
  - both runs produce `lbm = 0.8874912094048452`

### Archive 3. Current-Source Single-Core Landscape

Frozen baselines remain:

| workload | frozen `orig` baseline |
|---|---:|
| `astar` | `0.11624407059812944` |
| `mcf` | `0.09304738191331209` |
| `lbm` | `0.8705538637792136` |
| `bzip2` | `2.132624022725237` |

Current-source direct matrix source-of-truth:

- [direct_orig_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_orig_matrix_r1.json)
- [direct_fdp_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_fdp_matrix_r1.json)
- [direct_bop_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_bop_matrix_r1.json)

Best-tuned points on the current source:

| workload | best expert | exact IPC | gain vs frozen `orig` | minimum acceptable IPC |
|---|---|---:|---:|---:|
| `astar` | `bop` | `0.11689840080543869` | `+0.0006543302073092477` | `0.11670210174324591` |
| `mcf` | `bop` | `0.09483537300389441` | `+0.001787991090582322` | `0.09429897567671972` |
| `lbm` | `orig` | `0.8881199942236687` | `+0.017566130444455097` | `0.8828501550903322` |
| `bzip2` | `orig` | `2.152851106263848` | `+0.020227083538610913` | `2.1467829812022647` |

Important current-source change from the earlier ledger:

- `lbm` best point moved from `0.8728014354438657` to `0.8881199942236687`
- `bzip2` best point moved from `fdp` to `orig`
- `astar` and `mcf` still prefer `bop`

### Archive 4. Retained Selector Result

Current source-of-truth selector validation:

- full pass: [repro_rule2_full_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_full_r1.json)
- repeated `lbm` reproducibility: [repro_rule2_lbm_r2.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_lbm_r2.json)

`rule2` exact result:

| workload | switch decision | exact IPC | minimum acceptable IPC | gain retention |
|---|---|---:|---:|---:|
| `astar` | `bop` | `0.11769683762267738` | `0.11670210174324591` | `222.02353006474267%` |
| `mcf` | `bop` | `0.0955303095422819` | `0.09429897567671972` | `138.8668904474882%` |
| `lbm` | `orig` | `0.8874912094048452` | `0.8828501550903322` | `96.42047051391424%` |
| `bzip2` | `fdp` | `2.150898860454012` | `2.1467829812022647` | `90.3483574084735%` |

Aggregate selector sum IPC:

- `3.2516172170238162`

Exact switch-window features:

| workload | `page_growth` | `small_delta_ratio` | selected expert |
|---|---:|---:|---|
| `astar` | `0.65625` | `0.15873` | `bop` |
| `mcf` | `0.96875` | `0.0` | `bop` |
| `lbm` | `0.09375` | `0.873016` | `orig` |
| `bzip2` | `0.125` | `0.539683` | `fdp` |

Interpretation:

- `page_growth` alone was sufficient to isolate `bop` workloads
- `small_delta_ratio` was the missing signal that separates `lbm` from `bzip2`
- after the SPP fixes, `lbm` no longer belongs in the old `fdp` bucket

### Archive 5. Do-Not-Rerun Ledger

These are the points that should now be treated as frozen source-of-truth for this cycle.

| artifact | note |
|---|---|
| [direct_orig_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_orig_matrix_r1.json) | current-source `orig` direct matrix |
| [direct_fdp_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_fdp_matrix_r1.json) | current-source `fdp` direct matrix |
| [direct_bop_matrix_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/direct_bop_matrix_r1.json) | current-source `bop` direct matrix |
| [repro_rule2_full_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_full_r1.json) | retained selector `4/4` pass |
| [repro_rule2_lbm_r2.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_lbm_r2.json) | repeated selector `lbm` reproducibility check |

Obsolete / intentionally removed from active consideration:

- pre-fix `lbm` drift summaries
- `orig_rfo` temporary config experiment
- general-selector scripts and result trees
- feature-dump-only and threshold dead-end summaries
- raw per-run output dumps under `repro/runs`

### Archive 6. Compact Manual

Minimal rerun flow on `office-kijun`:

```bash
rsync -av prefetcher/adaptive_selector/adaptive_selector.cc \
          prefetcher/adaptive_selector/adaptive_selector.h \
          prefetcher/spp_orig/spp_orig.cc \
          prefetcher/spp_dev/spp_dev.cc \
          configs/adaptive_selector_config.json \
          office-kijun:/Users/kijunshi/Developers/ChampSim/
```

Clean rebuild:

```bash
cd /Users/kijunshi/Developers/ChampSim
rm -f _configuration.mk
make configclean
./config.sh configs/adaptive_selector_config.json
make -j4 bin/champsim_adaptive_selector

./config.sh configs/spp_orig_config.json
make -j4 bin/champsim_orig

./config.sh configs/spp_fdp_config.json
make -j4 bin/champsim_fdp

./config.sh configs/bop_config.json
make -j4 bin/champsim_bop
```

Retained selector validation:

- the original helper command from that cycle was removed during cleanup
- the retained summaries below remain the historical source of truth

Optional `lbm` stability check:

- use the retained summary links in this archive rather than regenerating the old helper flow

## 9. Older Archive

Older cycle records are intentionally kept for continuity, but they are no longer the active source-of-truth.

### 7.1 Older selector phase

Older retained selector family before the final `rule2` update:

- `page_only` rule
  - `page_growth < 0.17 -> fdp`
  - `0.17 <= page_growth < 0.40 -> orig_like`
  - `page_growth >= 0.40 -> bop`
- representative validation point: `reprofix2_full`
  - `astar = 0.11769683762267738`
  - `mcf = 0.0955303095422819`
  - `lbm = 0.8804757668415644`
  - `bzip2 = 2.150898860454012`
- this phase is preserved only as a historical step before the `lbm` current-source landscape was refrozen

### 7.2 Older single-core landscape

Earlier frozen best-tuned points before the current-source refreeze:

| workload | older best | exact IPC |
|---|---|---:|
| `astar` | `bop` | `0.11689840080543869` |
| `mcf` | `bop` | `0.09483537300389441` |
| `lbm` | tuned `orig` | `0.8728014354438657` |
| `bzip2` | `fdp` | `2.1518726883512267` |

These were valid at the time, but the final retained ledger for this cycle is the current-source matrix in Section 3.

### 7.3 Older pair-focused research

Older hetero pair cycle summary:

| pair | earlier best point | exact WS | note |
|---|---|---:|---|
| `mcf + lbm` | `orig_fdp` | `1.294978831807267` | target miss remained |
| `bzip2 + lbm` | `orig_fdp` | `2.008543659928995` | target miss remained |

Interpretation carried forward:

- pair bottlenecks were primarily `lbm` protection / coordination
- that observation motivated the later single-core adaptive-selector work

## 10. Next Step

The next cycle should start from the current retained artifacts, not from the older `page_only` or pure-static archives.

Most promising follow-up:

- split the multicore path into a minimal `orig/fdp` selector instead of carrying full `bop/orig/fdp` richness into the pair
- make the pair controller closer to one-shot classification so `2-core` execution cost stays bounded
- rerun `mcf + lbm` pair candidates with at most `1-2` concurrent runs per workspace on `office-kijun`
