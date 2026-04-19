# Phase 1.5 Summary

## What Is Implemented

- Added three DPC-3 traces under `traces/`:
  - `471.omnetpp-188B.champsimtrace.xz`
  - `459.GemsFDTD-765B.champsimtrace.xz`
  - `437.leslie3d-134B.champsimtrace.xz`
- Added one stress-only 4-core config per policy:
  - `configs/orig_4core_stress.json`
  - `configs/fdp_4core_stress.json`
  - `configs/bwc_4core_stress.json`
- Added automation:
  - `scripts/download_phase15_traces.sh`
  - `scripts/screen_phase15_candidates.py`
  - `scripts/extract_multicore_metrics.py`
  - `run_phase15_batch.sh`
- Added extracted outputs:
  - `results/phase15_single_core_screening.csv`
  - `results/phase15_selection.json`
  - `results/phase15_multicore_metrics.csv`
  - `results/phase15_pilot_multicore_metrics.csv`

## Single-Core Screening Result

Screening rule: `10M warmup + 30M sim`, compare `orig` against `no_pref`.

| Workload | No-Pref IPC | Orig IPC | Ratio | Role |
| --- | ---: | ---: | ---: | --- |
| `omnetpp` | 0.2530 | 0.2762 | 1.092 | neutral |
| `GemsFDTD` | 0.6345 | 0.8801 | 1.387 | victim |
| `leslie3d` | 0.9736 | 1.9480 | 2.001 | victim |
| `mcf` | 0.1163 | 0.1212 | 1.042 | fallback polluter |
| `lbm` | 0.6019 | 0.8482 | 1.409 | victim |

Key takeaway:

- `omnetpp` did **not** satisfy the strict polluter rule (`<= 1.02`).
- Under the current rule set, the selected polluter falls back to `mcf`.
- Victim order is stable and favorable: `GemsFDTD`, `leslie3d`, `lbm`.

## Selected Main Mixes

Current selection from `results/phase15_selection.json`:

- 2-core main mix: `mcf + GemsFDTD`
- 4-core main mix: `mcf + GemsFDTD + leslie3d + lbm`

This means the report should move away from `4x lbm` as the headline figure. The stronger story is now:

1. `mcf + GemsFDTD` as the main heterogeneous mix
2. `mcf + GemsFDTD + leslie3d + lbm` under stress as the main 4-core result
3. `4x lbm` as a limitation/ablation

## Stress Configuration

The new stress config keeps the cache hierarchy intact and only tightens the shared-memory bottlenecks:

- `physical_memory.data_rate: 3200 -> 2400`
- `LLC.rq_size: 64 -> 32`
- `LLC.mshr_size: 128 -> 64`

## Pilot Data Available Now

Completed pilot rows are extracted into `results/phase15_pilot_multicore_metrics.csv`.

### 2-core exploratory pilot: `omnetpp + GemsFDTD` (`10M + 10M`)

| Policy | WS | HS | Polluter PF Issued | Victim IPC |
| --- | ---: | ---: | ---: | ---: |
| `orig` | 1.7713 | 0.8821 | 188,559 | 0.8289 |
| `fdp`  | 1.6755 | 0.8349 | 174,028 | 0.7803 |
| `bwc`* | 1.7723 | 0.8861 | 331,752 | 0.7842 |

\* This `bwc` pilot was collected before the symmetric-saturation path was gated off. It is useful as an exploratory datapoint, but it is **not** the report-ready default binary anymore.

Interpretation:

- The pilot confirms that `GemsFDTD` is a strong victim.
- It also confirms that `omnetpp` is a poor headline polluter: the exploratory `bwc` run increased polluter prefetch traffic instead of cutting it.
- So the screening funnel did the right thing by falling back to `mcf` for the main mix.

### 4-core stress pilot: `4x lbm`

- `orig` completed and was extracted.
- The exploratory symmetric-saturation `bwc` path was unstable in 4-core pilot runs, so those rows are intentionally excluded from the complete-only pilot CSV.

## BWC Code Status

Two code changes were made in `prefetcher/spp_bwc/`:

1. **GHR replacement robustness fix**
   - `GLOBAL_REGISTER::update_entry()` now prefers an invalid slot before evicting a valid entry and no longer assumes confidence is always below 100.
   - This avoids the earlier `Cannot find a replacement victim!` assertion.

2. **Optional symmetric-saturation controller scaffold**
   - The requested `congested_epochs` / `relaxed_epochs` logic was added.
   - It is currently compiled in but gated by `BWC_ENABLE_SYMMETRIC_SATURATION = false`.
   - Reason: when enabled, it produced unstable 4-core behavior during pilot runs.

Current safe default:

- `accuracy < 1%` immediate throttle remains active
- original hard congestion thresholds remain active
- symmetric early-trigger path is present but disabled

Smoke-tested binaries now finish with `symmetric_saturation=off`:

- `bin/champsim_bwc_2core`
- `bin/champsim_bwc_4core`
- `bin/champsim_bwc_4core_stress`

## Recommended Next Run Order

Use the current batch script for overnight/full experiments:

```bash
./run_phase15_batch.sh
```

Expected sequence:

1. Reuse the completed single-core screening logs
2. Run `mcf + GemsFDTD` for `orig / fdp / bwc`
3. Run `mcf + GemsFDTD + leslie3d + lbm` nominal for `orig / bwc`
4. Run the same 4-core mix under stress for `orig / bwc`
5. Only if `bwc` is close to or better than `orig`, add `fdp` stress

## Reporting Guidance

- Put `2-core heterogeneous` first.
- Put `4-core stress` second.
- Keep `4x lbm nominal` or `4x lbm stress` as a limitation/ablation, not the main claim.
- Keep nominal and stress conclusions separated in the prose and in tables.
