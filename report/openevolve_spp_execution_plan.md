# Official OpenEvolve SPP Execution Plan

## Mainline

The project is now explicitly **SPP-only**.
The final-report comparison set is:

- `orig`: original SPP
- `fdp`: FDP-style SPP throttling
- `bwc_local`: local hand-designed BWC baseline
- `gsp_tiered_seed`: global-pressure, tiered-throttling seed family
- `OE-best`: best candidate discovered by official OpenEvolve

The official search entrypoint is now:

- [`initial_program.py`](/home/zhaofeng/course/15-740_project/openevolve/examples/champsim_spp_controller/initial_program.py)
- [`evaluator.py`](/home/zhaofeng/course/15-740_project/openevolve/examples/champsim_spp_controller/evaluator.py)
- [`config.yaml`](/home/zhaofeng/course/15-740_project/openevolve/examples/champsim_spp_controller/config.yaml)

The temporary local harness in `ChampSim/scripts/` is no longer the main workflow.

## Source Of Truth

- **ChampSim repo** remains the execution backend and result source of truth:
  - controller code
  - configs and binaries
  - trace inventory
  - log parsing
  - final metrics and report figures
- **OpenEvolve repo** owns:
  - candidate mutation
  - official evaluator entrypoint
  - config and search orchestration

Bootstrap artifacts from the pre-migration harness remain under
`results/openevolve/` only as historical smoke/diagnostic material.
The official path now writes to `results/openevolve_official/`.

## Candidate Family

`gsp_tiered_seed` preserves the SPP predictor path and exposes only controller knobs:

- `acc_low_throttle`
- `global_mshr_t1/t2/t3`
- `global_llc_t1/t2/t3`
- `tier1_issue_period`
- `tier2_issue_period`
- `tier3_pf_threshold`
- `min_active_cores`
- `congested_epochs`
- `relaxed_epochs`
- `pressure_mode`

Hard constraints are enforced inside the official evaluator:

- `0 <= acc_low_throttle <= 0.05`
- `t1 <= t2 <= t3` for both MSHR and LLC thresholds
- `tier1_issue_period`, `tier2_issue_period` in `{1,2,3,4,6,8}`
- `tier1_issue_period <= tier2_issue_period`
- `tier3_pf_threshold in [25, 100]`
- `min_active_cores in {2,3,4}`
- `pressure_mode in {"avg","max","avg+max"}`

Invalid candidates are rejected before any ChampSim run starts.

## Evaluation Profiles

### `train`
- purpose: official OpenEvolve search profile
- mixes:
  - `mcf + GemsFDTD`
  - `mcf + astar`
  - `mcf + GemsFDTD + leslie3d + lbm`
  - `4xlbm`
- instructions: `10M warmup + 30M sim`
- execution model:
  - cache `orig/fdp` once
  - run only `gsp_tiered_seed` for each candidate
  - compute `combined_score` from merged candidate + baseline metrics

### `smoke`
- purpose: official parity check against the old bootstrap harness
- mixes:
  - `2core mcf + GemsFDTD`
  - `4core 4xlbm`
- instructions: `1M + 1M`
- policies: `orig / fdp / bwc_local / gsp_tiered_seed`

### `baseline_nominal`
- purpose: freeze long nominal baselines for report use
- mixes: train + validation mixes
- instructions: `10M + 50M`
- policies: `orig / fdp / bwc_local / gsp_tiered_seed`

### `val_nominal`
- purpose: held-out long validation
- mixes:
  - `astar + lbm`
  - `mcf + astar + leslie3d + lbm`
- instructions: `10M + 50M`
- policies: `orig / fdp / bwc_local / gsp_tiered_seed`

### `stress_calibration`
- purpose: post-search calibration only
- configs:
  - `2800 / 48 / 96`
  - `2666 / 40 / 80`
  - `2400 / 32 / 64`
- policies: `orig / fdp`

## Fitness

The evaluator returns:

- `combined_score`
- `avg_gain_vs_fdp`
- `avg_gain_vs_orig`
- `victim_floor_vs_orig`
- `pf_ratio_vs_orig`
- `traffic_reduction`
- `complete_rate`

Scoring logic:

- primary objective: average weighted-speedup gain vs `fdp`
- secondary objective: average weighted-speedup gain vs `orig`
- penalties:
  - incomplete / failed runs
  - victim IPC floor below `95%` of `orig`
  - prefetch inflation beyond tolerance

MAP-Elites feature dimensions are:

- `victim_floor_vs_orig`
- `traffic_reduction`

## Commands

From `/home/zhaofeng/course/15-740_project/openevolve`:

Smoke parity:

```bash
python examples/champsim_spp_controller/evaluator.py \
  --program examples/champsim_spp_controller/initial_program.py \
  --profile smoke \
  --candidate-id seed \
  --build-jobs 8 \
  --parallelism 2
```

Freeze long baselines:

```bash
python examples/champsim_spp_controller/evaluator.py \
  --program examples/champsim_spp_controller/initial_program.py \
  --profile baseline_nominal \
  --candidate-id seed \
  --build-jobs 8 \
  --parallelism 2
```

Run OpenEvolve:

```bash
python openevolve-run.py \
  examples/champsim_spp_controller/initial_program.py \
  examples/champsim_spp_controller/evaluator.py \
  --config examples/champsim_spp_controller/config.yaml \
  --iterations 30
```

## Cleanup Boundary

Deleted from the mainline after official parity:

- `scripts/openevolve_spp_common.py`
- `scripts/generate_openevolve_manifests.py`
- `scripts/run_spp_experiments.py`
- `scripts/compute_openevolve_fitness.py`
- `scripts/openevolve_eval_spp.py`

Kept in ChampSim:

- `prefetcher/spp_bwc/*`
- `prefetcher/spp_gsp_tiered/*`
- `scripts/extract_multicore_metrics.py`
- `scripts/screen_phase15_candidates.py`
- Phase 1.5 scripts and results

So the repo split is now clean:
ChampSim owns execution truth; OpenEvolve owns evolution.
