# OpenEvolve SPP Status And Next Steps

Date: 2026-04-18

## 1. Scope Decision

This checkpoint deliberately stops short of further controller redesign.

The goal of this report is to freeze the current state of the official OpenEvolve
mainline, record which parts are already trustworthy, identify the remaining
blockers, and define the smallest next-step plan before final report work
continues.

The project mainline remains:

- `orig`
- `fdp`
- `bwc_local`
- `gsp_tiered_seed`
- future `OE-best`

OpenEvolve remains the only intended search entrypoint, but as of this checkpoint
the project is **not yet ready for a meaningful search run**.

## 2. Current Implementation State

### 2.1 Official OpenEvolve integration

The official example path is in place under the OpenEvolve repo:

- `examples/champsim_spp_controller/initial_program.py`
- `examples/champsim_spp_controller/evaluator.py`
- `examples/champsim_spp_controller/config.yaml`
- `examples/champsim_spp_controller/README.md`

ChampSim remains the execution backend and source of truth for:

- controller code
- binaries and configs
- trace selection
- log parsing
- metrics CSVs
- report figures

### 2.2 Controller/code state

The following ChampSim-side components are now present:

- `prefetcher/spp_bwc/` as `bwc_local`
- `prefetcher/spp_gsp_tiered/` as the OpenEvolve seed family
- `scripts/extract_multicore_metrics.py`
- `scripts/screen_phase15_candidates.py`

Two targeted repairs were completed during this checkpoint:

1. `scripts/extract_multicore_metrics.py`
   - now treats `panic:` and `DEADLOCK!` as `failed`
   - now records `missing_baselines`
   - now marks runs as `baseline_missing` when normalization input is incomplete

2. `scripts/screen_phase15_candidates.py`
   - now recognizes `astar`
   - now generates a consistent updated screening/selection result

### 2.3 Single-core screening state

The single-core screening source has been repaired:

- `no_pref_astar_30M.txt`: IPC `0.457`
- `orig_astar_30M.txt`: IPC `0.448`
- `orig / no_pref = 0.9803`

So `astar` now classifies as a polluter candidate under the same Phase 1.5 rule.

Updated screening output:

- `results/phase15_single_core_screening.csv`
- `results/phase15_selection.json`

Current selection result:

- qualified polluters: `astar`
- qualified victims: `gemsfdtd`, `leslie3d`, `lbm`
- selected polluter: `astar`
- selected victim: `gemsfdtd`

## 3. Current Result Inventory

### 3.1 Official smoke status

Completed smoke sets:

- `results/openevolve_official/profile_runs/smoke/seed/`
- `results/openevolve_official/profile_runs/smoke/seed_min2/`

`seed_min2` means the seed was rerun after changing `min_active_cores` from `4`
to `2` in the default candidate.

Smoke completion status:

- `seed`: complete
- `seed_min2`: complete

### 3.2 Official long nominal baseline status

Current processed baseline snapshot:

- `results/openevolve_official/profile_runs/baseline_nominal/seed/baseline_metrics_partial.csv`

Status counts:

- `complete`: 18
- `failed`: 1
- `incomplete`: 3
- `missing`: 2

That means the current nominal long baseline freeze is still partial.

## 4. What Is Now Trustworthy

## 4.1 The official evaluation path works

The project has enough evidence to claim that the official evaluator path is real:

- manifests are generated correctly
- the official evaluator launches ChampSim binaries correctly
- unified metrics extraction works
- smoke runs complete end to end

This part should now be treated as stable infrastructure.

## 4.2 The `astar` normalization bug is fixed

The earlier corrupted rows involving `astar` are no longer using a missing
baseline.

After regeneration:

### `2core_train_mcf_astar`

- `orig`: WS `1.986760`, victim_min `1.051`
- `fdp`: WS `1.959631`, victim_min `1.039`
- `bwc_local`: WS `1.955769`, victim_min `1.034`
- `gsp_tiered_seed`: WS `1.955769`, victim_min `1.034`

### `2core_val_astar_lbm`

- `orig`: WS `1.525513`, victim_min `0.966`
- `fdp`: WS `1.500771`, victim_min `0.749`
- `bwc_local`: WS `1.510415`, victim_min `0.965`
- `gsp_tiered_seed`: WS `1.510415`, victim_min `0.965`

So the old `victim_min = 0.000` rows should now be considered obsolete.

## 4.3 The strongest current positive result

The clearest currently usable nominal result is still:

### `2core_val_astar_lbm`

- `orig`: WS `1.525513`
- `fdp`: WS `1.500771`
- `bwc_local`: WS `1.510415`
- `gsp_tiered_seed`: WS `1.510415`

Interpretation:

- `fdp` clearly degrades this validation mix
- `bwc_local` and `gsp_tiered_seed` both preserve near-`orig` victim behavior
- this is useful evidence against `fdp`
- this is **not yet** useful evidence that `gsp_tiered_seed` beats `bwc_local`

## 4.4 The cleanest current 4-core nominal result

### `4core_train_4xlbm`

- `orig`: WS `1.084650`
- `fdp`: WS `1.073921`
- `bwc_local`: WS `1.084768`
- `gsp_tiered_seed`: WS `1.078519`

Interpretation:

- `gsp_tiered_seed` already beats `fdp`
- `gsp_tiered_seed` still trails `orig`
- `bwc_local` essentially matches `orig`

This remains the best currently complete 4-core nominal point.

## 5. What Is Still Broken Or Unresolved

## 5.1 The 2-core seed collapse is still unresolved

This was explicitly re-tested.

We changed the seed default from:

- `min_active_cores = 4`

to:

- `min_active_cores = 2`

and reran official smoke under:

- `results/openevolve_official/profile_runs/smoke/seed_min2/`

But the result did **not** materially change.

For `2core_smoke_mcf_gemsfdtd`:

- `gsp_tiered_seed` still matches `bwc_local`
- controller footer still shows `symmetric_mode_epochs = 0`

This is an important diagnosis:

- the remaining 2-core collapse is no longer a manifest problem
- it is no longer a missing-baseline problem
- it is no longer just a bad `min_active_cores` default

The remaining cause is most likely controller logic:

- the shared-pressure thresholds are too high for the 2-core smoke case
- the local low-accuracy path likely drives the controller to the same final state
  before global behavior can differentiate

So the next fix here is a **controller-design change**, not a data repair.

## 5.2 The main 4-core `mcf` mix is still not stable

### `4core_train_mcf_gemsfdtd_leslie3d_lbm_bwc_local`

This run is now correctly classified as `failed`.

Observed behavior:

- repeated CPU0 critical warnings
- eventual `Simulation CPU 0 panic: IPC 0.0092569 < 0.01`
- `DEADLOCK!`

### `4core_train_mcf_gemsfdtd_leslie3d_lbm_gsp_tiered_seed`

This run is still only `incomplete`.

Observed behavior from the existing log:

- truncates early
- no final footer
- no clean completion

So the main 4-core train mix is still not in a report-ready state for the new
controller family.

## 5.3 The 4-core validation main mix is still unfinished

For `4core_val_mcf_astar_leslie3d_lbm`:

- `orig`: incomplete
- `fdp`: incomplete
- `bwc_local`: missing
- `gsp_tiered_seed`: missing

So held-out 4-core validation is not frozen yet.

## 5.4 OpenEvolve search itself has not started

The following official search-stage artifacts do not yet exist as finalized,
usable state:

- `baseline_cache/train_orig_fdp/`
- `candidate_runs/<candidate_id>/`
- official OpenEvolve search DB
- any `OE-best`

So we must still say:

- migration is done
- smoke is done
- baseline freeze is partial
- search has not begun

## 6. Current Interpretation

The current project state is best described as:

1. The official OpenEvolve migration is successful.
2. The data-plane bug around `astar` has been repaired.
3. The parser-side failure accounting has been repaired.
4. The seed family still lacks meaningful separation from `bwc_local` on 2-core mixes.
5. The most important 4-core heterogeneous nominal run is still unstable.

Therefore:

- the infrastructure is in much better shape than before
- the methodology is clearer than before
- but the controller family is still not ready for a productive OpenEvolve run

## 7. Recommended Next-Step Plan

The next plan should stay minimal.

### Step 1: Freeze the repaired data plane

Do not change the parser or screening path again unless a new concrete bug appears.

Treat the following as the current repaired baseline inputs:

- `results/phase15_single_core_screening.csv`
- `results/phase15_selection.json`
- `results/openevolve_official/profile_runs/baseline_nominal/seed/baseline_metrics_partial.csv`

Goal:

- stop spending time on bookkeeping bugs

### Step 2: Fix the 2-core seed collapse with one small controller change

Do not do a wide redesign.

Make one small controller-side intervention aimed specifically at enabling
nontrivial 2-core differentiation, for example:

- lower the global-pressure tier-1 thresholds
- or weaken the `!shared.any_low_accuracy` gate
- or allow global tiering to coexist with the local low-accuracy fallback instead
  of being suppressed by it

But:

- change only one mechanism at a time
- verify first on official smoke
- then rerun only the affected 2-core nominal rows

Goal:

- make `gsp_tiered_seed` stop collapsing to `bwc_local` on 2-core mixes

### Step 3: Repair only the invalid nominal rows

Do not rerun the 18 already-complete nominal rows.

Only target:

- `4core_train_mcf_gemsfdtd_leslie3d_lbm_bwc_local`
- `4core_train_mcf_gemsfdtd_leslie3d_lbm_gsp_tiered_seed`
- `4core_val_mcf_astar_leslie3d_lbm_orig`
- `4core_val_mcf_astar_leslie3d_lbm_fdp`
- `4core_val_mcf_astar_leslie3d_lbm_bwc_local`
- `4core_val_mcf_astar_leslie3d_lbm_gsp_tiered_seed`

Goal:

- finish the nominal baseline freeze without wasting wall-clock time

### Step 4: Only start official pilot search after the above two are done

Minimum gate before OpenEvolve pilot:

- no missing single-core baseline inputs
- parser failure accounting remains correct
- 2-core seed behavior is meaningfully different from `bwc_local`
- the main 4-core train mix has at least a complete seed-family run

Then:

- build baseline cache for `orig/fdp`
- run a small official pilot
- keep the first pilot to 20 to 30 candidates

Goal:

- test search dynamics only after the controller family is worth searching

## 8. Practical Conclusion

The right decision at this checkpoint is:

- stop digging deeper today
- preserve the repaired state
- keep the current conclusions narrow and honest

What we can confidently say now:

- the official OpenEvolve path is alive
- the data path is cleaner than before
- `fdp` remains vulnerable on some nominal mixes
- `gsp_tiered_seed` still needs controller-side work before evolution will be
  informative

What should happen next:

- one focused controller fix
- one targeted nominal rerun batch
- then official pilot search
