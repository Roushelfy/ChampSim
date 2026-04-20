# BWC Research Unified Report

This is the single active report for the current repo state.

- date: `2026-04-20`
- host: `office-kijun`
- run length: `1M warmup + 5M simulation`
- active goal: sliding-window in-simulator expert selector for `astar`, `mcf`, `lbm`, `bzip2`
- acceptance rule: retain at least `70%` of the improvement from frozen `orig` baseline to the current best-tuned point

Latest retained result:

- selector summary: [repro_rule2_full_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/repro/summaries/repro_rule2_full_r1.json)
- best current rule: `page_growth + small_delta_ratio` (`rule2`)
- outcome: `4/4` pass on the current source
- strongest exact selector IPCs:
  - `astar = 0.11769683762267738`
  - `mcf = 0.0955303095422819`
  - `lbm = 0.8874912094048452`
  - `bzip2 = 2.150898860454012`

## 1. Executive Summary

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
- run helper: [run_singlecore_adaptive_selector.py](/Users/shinkijun/Developers/ChampSim/scripts/run_singlecore_adaptive_selector.py)

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

## 2. Root Cause And Fix

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

## 3. Current-Source Single-Core Landscape

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

## 4. Retained Selector Result

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

## 5. Do-Not-Rerun Ledger

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

## 6. Compact Manual

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

```bash
python3 scripts/run_singlecore_adaptive_selector.py \
  --workloads astar mcf lbm bzip2 \
  --label repro_rule2_full_r1 \
  --results-dir results/cycle_sliding_window_selector_20260420/repro \
  --env ADAPT_WINDOW_REFS=64 \
        ADAPT_EVAL_STRIDE=16 \
        ADAPT_DECISION_STREAK=2 \
        ADAPT_LOCK_AFTER_SWITCH=1 \
        ADAPT_FDP_PAGE_GROWTH_MAX=0.17 \
        ADAPT_ORIG_PAGE_GROWTH_MAX=0.40 \
        ADAPT_ORIG_SMALL_DELTA_MIN=0.70
```

Optional `lbm` stability check:

```bash
python3 scripts/run_singlecore_adaptive_selector.py \
  --workloads lbm \
  --label repro_rule2_lbm_r2 \
  --results-dir results/cycle_sliding_window_selector_20260420/repro \
  --env ADAPT_WINDOW_REFS=64 \
        ADAPT_EVAL_STRIDE=16 \
        ADAPT_DECISION_STREAK=2 \
        ADAPT_LOCK_AFTER_SWITCH=1 \
        ADAPT_FDP_PAGE_GROWTH_MAX=0.17 \
        ADAPT_ORIG_PAGE_GROWTH_MAX=0.40 \
        ADAPT_ORIG_SMALL_DELTA_MIN=0.70
```

## 7. Historical Archive

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

## 8. Next Step

The next cycle should start from `rule2`, not from the older `page_only` rule.

Most promising follow-up:

- replace the hard switch-once lock with a light re-evaluation policy that preserves warm expert state
- keep the same `page_growth + small_delta_ratio` family unless a new workload set is introduced
