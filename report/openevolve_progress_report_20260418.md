# Official OpenEvolve Progress Report

Date: 2026-04-18

Note: for the latest consolidated status and next-step plan after the repair
pass, use `report/openevolve_status_and_next_steps_20260418.md` as the
authoritative summary. This file remains the earlier progress narrative plus
repair notes.

## Repair Update (Late Apr 18)

After the first draft of this report, the following fixes were implemented and verified:

### Fixed: missing `astar` single-core baseline

`astar` single-core screening was added with `10M warmup + 30M sim`:

- `no_pref_astar_30M.txt`: IPC `0.457`
- `orig_astar_30M.txt`: IPC `0.448`
- ratio vs `no_pref`: `0.9803`

This means `astar` now classifies as a **polluter candidate** under the same Phase 1.5 rule.

`results/phase15_single_core_screening.csv` and `results/phase15_selection.json` were regenerated from the updated screening set.

Current selection state:

- qualified polluters: `astar`
- qualified victims: `gemsfdtd`, `leslie3d`, `lbm`
- selected polluter: `astar`
- selected victim: `gemsfdtd`

### Fixed: multicore parser now marks `panic` and `DEADLOCK!` as failures

`scripts/extract_multicore_metrics.py` was updated so failure detection now catches:

- `panic:`
- `DEADLOCK!`

This corrects the earlier under-reporting where a clearly failed run could appear only as `incomplete`.

After re-extraction, the current `baseline_nominal` status summary is:

- `complete`: 18
- `failed`: 1
- `incomplete`: 3
- `missing`: 2

In particular:

- `4core_train_mcf_gemsfdtd_leslie3d_lbm_bwc_local` is now correctly labeled `failed`

### Fixed: `astar`-dependent WS / retention metrics are now valid

After regenerating `results/openevolve_official/profile_runs/baseline_nominal/seed/baseline_metrics_partial.csv`
with the repaired single-core baseline source:

`2core_train_mcf_astar`

- `orig`: WS `1.986760`, victim_min `1.051`
- `fdp`: WS `1.959631`, victim_min `1.039`
- `bwc_local`: WS `1.955769`, victim_min `1.034`
- `gsp_tiered_seed`: WS `1.955769`, victim_min `1.034`

`2core_val_astar_lbm`

- `orig`: WS `1.525513`, victim_min `0.966`
- `fdp`: WS `1.500771`, victim_min `0.749`
- `bwc_local`: WS `1.510415`, victim_min `0.965`
- `gsp_tiered_seed`: WS `1.510415`, victim_min `0.965`

So the earlier `victim_min = 0.000` corruption is gone.

### Investigated but not solved: 2-core seed collapse

The seed default was updated from `min_active_cores = 4` to `min_active_cores = 2`, and a fresh official smoke run was executed under:

- `results/openevolve_official/profile_runs/smoke/seed_min2/`

Result:

- `2core_smoke_mcf_gemsfdtd` still matches the old seed exactly
- `gsp_tiered_seed` still matches `bwc_local`
- controller footer still reports `symmetric_mode_epochs = 0`

So `min_active_cores` alone was **not** the true root-cause fix.
The 2-core collapse remains and is now better diagnosed:

- in the smoke case, shared pressure never crosses the current global thresholds
- in the long heterogeneous cases, the low-accuracy gate and local level-1 throttling are still likely dominating before any useful global-tier separation appears

This means the remaining issue is now a **controller-design problem**, not a data-pipeline bug.

## 1. Executive Summary

As of this checkpoint, the project has completed the **official OpenEvolve migration**, but it has **not reached a trustworthy search-ready state yet**.

What is already in place:

- official OpenEvolve example directory is live
- candidate genome, evaluator, config, and README are wired up
- fixed manifests for `smoke`, `train_search`, `baseline_nominal`, `val_nominal`, and `stress_calibration` are generated under `results/openevolve_official/manifests/`
- official `smoke` profile has completed successfully
- official `baseline_nominal` long-run freeze has completed **partially**

What is not ready yet:

- no official OpenEvolve search run has started
- no `baseline_cache/train_orig_fdp`, `candidate_runs/`, or search `db/` artifacts exist yet
- the current training data path is blocked by a **missing `astar` single-core baseline**
- the main 4-core `mcf`-based long runs are still unstable and/or extremely slow

Bottom line:

1. The migration itself is basically done.
2. The current official result chain already shows some useful policy behavior.
3. We should **not start OpenEvolve iterations yet** until the data and stability blockers are fixed.

## 2. Current Artifact Status

### 2.1 Official OpenEvolve example

The official search entrypoint is now:

- `../openevolve/examples/champsim_spp_controller/initial_program.py`
- `../openevolve/examples/champsim_spp_controller/evaluator.py`
- `../openevolve/examples/champsim_spp_controller/config.yaml`
- `../openevolve/examples/champsim_spp_controller/README.md`

The seed genome currently exposes only controller parameters:

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

This matches the intended "controller-only evolution" scope.

### 2.2 Official result tree

Under `results/openevolve_official/`, the following pieces are already present:

- `manifests/`
  - `smoke.csv`
  - `train_search.csv`
  - `baseline_nominal.csv`
  - `val_nominal.csv`
  - `stress_calibration.csv`
- `profile_runs/smoke/seed/`
  - complete smoke logs
  - `smoke_metrics.csv`
- `profile_runs/baseline_nominal/seed/`
  - partial long-run logs
  - `manifest.csv`
  - `baseline_metrics_partial.csv` (generated after stopping the overnight job)
- `generated_configs/`
  - stress ladder JSON configs for `orig` and `fdp`

What is still missing from the official flow:

- `baseline_cache/train_orig_fdp/`
- `candidate_runs/<candidate_id>/`
- official OpenEvolve `db/`
- any `OE-best` candidate artifact

So the current state is: **migration + smoke + partial baseline freeze**, not yet evolution.

## 3. Run Completion Status

The overnight `baseline_nominal` job was manually stopped on request at about `2026-04-18 20:43 EDT`.

### 3.1 Smoke profile

`smoke` completion:

- total runs: 8
- complete: 8
- failed/incomplete: 0

This is the strongest evidence that the official evaluator path is live and can launch all four policies:

- `orig`
- `fdp`
- `bwc_local`
- `gsp_tiered_seed`

### 3.2 Long nominal baseline profile

`baseline_nominal` current coverage:

- total runs: 24
- complete: 18
- incomplete: 4
- missing: 2

Broken down by split:

| split | total | complete | incomplete | missing |
|---|---:|---:|---:|---:|
| train | 16 | 14 | 2 | 0 |
| validation | 8 | 4 | 2 | 2 |

Per-mix status:

| mix | status |
|---|---|
| `2core_train_mcf_gemsfdtd` | all 4 policies complete |
| `2core_train_mcf_astar` | all 4 policies complete |
| `4core_train_4xlbm` | all 4 policies complete |
| `4core_train_mcf_gemsfdtd_leslie3d_lbm` | `orig/fdp` complete, `bwc_local/gsp_tiered_seed` incomplete |
| `2core_val_astar_lbm` | all 4 policies complete |
| `4core_val_mcf_astar_leslie3d_lbm` | `orig/fdp` incomplete, `bwc_local/gsp_tiered_seed` missing |

This tells us the current bottleneck is very concentrated:

- the **main 4-core `mcf` train mix**
- the **4-core `mcf` validation mix**

Everything else already has usable long-run artifacts.

## 4. Result Analysis

## 4.1 Smoke profile: official path works, but seed is not yet winning

### `2core_smoke_mcf_gemsfdtd`

| policy | status | WS | vs Orig | vs FDP | victim_min | polluter PF red |
|---|---:|---:|---:|---:|---:|---:|
| orig | complete | 1.267596 | +0.00% | +3.50% | 0.927 | +0.0% |
| fdp | complete | 1.224734 | -3.38% | +0.00% | 0.874 | -0.4% |
| bwc_local | complete | 1.218013 | -3.91% | -0.55% | 0.876 | +74.8% |
| gsp_tiered_seed | complete | 1.218013 | -3.91% | -0.55% | 0.876 | +74.8% |

Takeaway:

- official evaluator is launching the right binaries and parsing the right fields
- but the seed family is **not yet better than FDP** on this heterogeneous 2-core smoke mix
- `bwc_local` and `gsp_tiered_seed` are numerically identical here

### `4core_smoke_4xlbm`

| policy | status | WS | vs Orig | vs FDP | victim_min | polluter PF red |
|---|---:|---:|---:|---:|---:|---:|
| orig | complete | 1.105282 | +0.00% | +1.17% | 0.262 | +0.0% |
| fdp | complete | 1.092549 | -1.15% | +0.00% | 0.255 | +85.0% |
| bwc_local | complete | 1.108229 | +0.27% | +1.44% | 0.258 | +0.4% |
| gsp_tiered_seed | complete | 1.100448 | -0.44% | +0.72% | 0.248 | +62.5% |

Takeaway:

- the 4-core official path does expose distinct seed-family behavior
- `gsp_tiered_seed` already beats `fdp`, but still trails `orig`
- `bwc_local` is slightly better than both `orig` and `fdp` in this very short symmetric smoke

Interpretation:

- the plumbing is working
- the seed family is not broken
- but the seed is not yet strong enough to justify launching full search

## 4.2 Long nominal baselines: current usable results

### `2core_train_mcf_gemsfdtd`

| policy | status | WS | vs Orig | vs FDP | victim_min | polluter PF red |
|---|---:|---:|---:|---:|---:|---:|
| orig | complete | 1.225434 | +0.00% | +0.16% | 0.451 | +0.0% |
| fdp | complete | 1.223494 | -0.16% | +0.00% | 0.429 | +0.1% |
| bwc_local | complete | 1.180295 | -3.68% | -3.53% | 0.432 | +74.9% |
| gsp_tiered_seed | complete | 1.180295 | -3.68% | -3.53% | 0.432 | +74.9% |

Takeaway:

- both `bwc_local` and `gsp_tiered_seed` are over-throttling this mix
- they reduce polluter PF traffic aggressively, but the performance cost is too high
- `orig` remains best

### `2core_train_mcf_astar`

| policy | status | WS | vs Orig | vs FDP | victim_min | polluter PF red |
|---|---:|---:|---:|---:|---:|---:|
| orig | complete | 0.935644 | +0.00% | +1.61% | 0.000 | +0.0% |
| fdp | complete | 0.920792 | -1.59% | +0.00% | 0.000 | +0.1% |
| bwc_local | complete | 0.921617 | -1.50% | +0.09% | 0.000 | +74.9% |
| gsp_tiered_seed | complete | 0.921617 | -1.50% | +0.09% | 0.000 | +74.9% |

This row is **not trustworthy yet**.

Reason:

- `victim_min` is exactly `0.000`
- the official parser is missing an `astar` single-core baseline, so the normalized metrics for this mix are malformed

This is a tooling/data blocker, not a meaningful architectural result.

### `4core_train_4xlbm`

| policy | status | WS | vs Orig | vs FDP | victim_min | polluter PF red |
|---|---:|---:|---:|---:|---:|---:|
| orig | complete | 1.084650 | +0.00% | +1.00% | 0.259 | +0.0% |
| fdp | complete | 1.073921 | -0.99% | +0.00% | 0.253 | +85.6% |
| bwc_local | complete | 1.084768 | +0.01% | +1.01% | 0.258 | -1.7% |
| gsp_tiered_seed | complete | 1.078519 | -0.57% | +0.43% | 0.234 | +76.0% |

Takeaway:

- `gsp_tiered_seed` is directionally better than `fdp`
- but it is still weaker than `orig`
- `bwc_local` matches `orig` almost exactly here

This is currently the cleanest 4-core nominal result we have.

### `2core_val_astar_lbm`

| policy | status | WS | vs Orig | vs FDP | victim_min | polluter PF red |
|---|---:|---:|---:|---:|---:|---:|
| orig | complete | 0.965692 | +0.00% | +28.97% | 0.966 | +0.0% |
| fdp | complete | 0.748762 | -22.46% | +0.00% | 0.749 | +17.2% |
| bwc_local | complete | 0.965103 | -0.06% | +28.89% | 0.965 | +68.1% |
| gsp_tiered_seed | complete | 0.965103 | -0.06% | +28.89% | 0.965 | +68.1% |

Takeaway:

- this is the strongest positive result in the current official chain
- `fdp` collapses badly on this validation mix
- both `bwc_local` and `gsp_tiered_seed` preserve near-`orig` performance while cutting polluter PF traffic by about `68%`

This is worth keeping for the final narrative, with one caveat:

- `gsp_tiered_seed` is again identical to `bwc_local`, so this is not yet evidence that the new family is outperforming the hand-designed local baseline

## 4.3 The two key behavioral findings

### Finding A: `gsp_tiered_seed` collapses to `bwc_local` behavior on 2-core mixes

The current seed uses:

- `min_active_cores = 4`

In practice, that means the global-pressure path is effectively unavailable on 2-core workloads.
The evidence is strong:

- `2core_train_mcf_gemsfdtd`: `bwc_local` and `gsp_tiered_seed` produce identical WS
- `2core_train_mcf_astar`: identical WS
- `2core_val_astar_lbm`: identical WS

The controller footers also converge to the same end states:

- victim core at level 3
- polluter core at level 1
- same final `pf_threshold` / `issue_period`

This is a serious design issue for the current OpenEvolve setup:

- the official `train_search` manifest includes 2-core mixes
- but the seed family currently does not express a meaningfully different global-aware behavior there

So part of the current training set is providing little or no evolutionary signal beyond the old local controller.

### Finding B: the seed family only shows distinct behavior on 4-core symmetric pressure, and even there it is not yet strong enough

On `4core_smoke_4xlbm` and `4core_train_4xlbm`, the seed does activate its global-tier logic:

- many `symmetric_mode_epochs`
- many tier1/tier2/tier3 epochs
- final `issue_period=4`, `pf_threshold=60`

That proves the official runtime parameter plumbing is real.

But performance is still mixed:

- better than `fdp`
- worse than `orig`
- not stronger than `bwc_local`

So the seed family is not failing because the knobs are disconnected.
It is failing because the current seed point is still a mediocre policy.

## 5. Current Blockers

## 5.1 Blocker 1: missing `astar` single-core baseline corrupts official metrics

The evaluator uses:

- `results/phase15_single_core_screening.csv`

Current coverage in that CSV:

- `gemsfdtd`
- `lbm`
- `leslie3d`
- `mcf`
- `omnetpp`

It does **not** include:

- `astar`

This has two direct consequences:

1. `2core_train_mcf_astar` weighted speedup is malformed because the `astar` term is dropped from normalization.
2. any retention metric involving `astar` becomes `0`, which contaminates:
   - `victim_min_retention`
   - `victim_avg_retention`
   - fitness penalties
   - feature dimensions

This is currently the most important search blocker.

Until this is fixed, the official `train` profile is not a valid optimization target.

## 5.2 Blocker 2: the main 4-core `mcf` mix is not stable

### `4core_train_mcf_gemsfdtd_leslie3d_lbm_bwc_local`

Observed behavior:

- repeated CPU0 critical warnings
- eventual `Simulation CPU 0 panic: IPC 0.0092569 < 0.01`
- followed by `DEADLOCK!`

So this run is not merely slow; it is architecturally unstable in the current setup.

### `4core_train_mcf_gemsfdtd_leslie3d_lbm_gsp_tiered_seed`

Observed behavior:

- log truncates at about `01 hr 07 min`
- last visible state already shows CPU0 critical and CPU1 warning
- no clean footer, no final stats, no explicit panic marker in the log body

This looks like an abnormal termination or forced stop, not a healthy completion.

### `4core_val_mcf_astar_leslie3d_lbm_orig/fdp`

Observed behavior before manual stop:

- both were still running
- both were dominated by very slow CPU0 (`mcf`)
- neither had reached final stats

This means the current validation main mix is not yet practical as an unattended long-run baseline in the present setup.

## 5.3 Blocker 3: the partial parser underreports `panic` as `incomplete`

`scripts/extract_multicore_metrics.py` currently marks a run as `failed` only for regex hits like:

- `Assertion`
- `assert`
- `Aborted`
- `invalid pointer`
- `core dumped`

It does **not** detect:

- `panic`
- `DEADLOCK!`

So the partial CSV generated from logs marks the `bwc_local` 4-core train main mix as `incomplete`, even though the log clearly contains a panic and deadlock.

This does not invalidate the raw logs, but it does make the current summary CSV slightly too optimistic.

## 6. What We Can Already Claim

The current official OpenEvolve work has already established several solid facts:

1. The official repo migration is real and functional.
2. The official evaluator can generate frozen manifests, launch ChampSim, and parse unified metrics.
3. `fdp` is clearly fragile on at least one held-out nominal validation mix (`astar + lbm`).
4. `gsp_tiered_seed` already shows distinct global-pressure behavior on 4-core symmetric workloads.
5. The current seed is **not** yet good enough to justify search, because the training pipeline is not clean and the most important 4-core heterogeneous mix is unstable.

## 7. What We Should Not Claim Yet

At this point, we should not claim:

- that official OpenEvolve search has begun
- that `gsp_tiered_seed` is already better than `bwc_local`
- that the train fitness is valid
- that held-out validation is frozen
- that the 4-core main-mix baseline set is complete

## 8. Recommended Minimal Next Steps

The next actions should be small and ordered:

1. Fix the data plane first:
   - add `astar` single-core baselines
   - regenerate the official normalization source used by the evaluator
   - re-extract metrics for all existing official runs

2. Fix the reporting path:
   - make the parser treat `panic` and `DEADLOCK!` as failed
   - regenerate `baseline_metrics_partial.csv`

3. Stabilize the 4-core main mix before any search:
   - reproduce `gsp_tiered_seed` on the 4-core train main mix in a shorter debug profile
   - determine whether the truncation is due to controller behavior, wrapper exit, or manual interruption
   - decide whether `bwc_local` should remain a report-only failing baseline on this mix

4. Re-freeze long nominal baselines:
   - rerun only the missing or invalid rows
   - do not rerun the already complete 18 good rows

5. Only then start official OpenEvolve pilot:
   - baseline cache for `orig/fdp`
   - 20 to 30 candidates
   - no full search until the train metrics and 4-core stability are clean

## 9. Practical Conclusion

The current official OpenEvolve line is **promising but not search-ready**.

The most encouraging result so far is:

- on `2core_val_astar_lbm`, both `bwc_local` and `gsp_tiered_seed` preserve near-`orig` WS while dramatically outperforming `fdp`

The most important problems are:

- broken normalization for `astar`
- unstable or unfinished 4-core `mcf` long runs
- insufficient policy separation between `bwc_local` and `gsp_tiered_seed` on 2-core mixes

So the right interpretation is:

- the official migration succeeded
- the architecture signal is there
- the experiment harness still needs one more cleanup pass before OpenEvolve should be allowed to optimize against it
