# Phase 1 Compact Report

This is the single authoritative Phase 1 note. Older `phase1_*` files are archive only.

All numbers below are from corrected short-run reruns on `office-kijun` with:

- `1M` warmup instructions
- `3M` simulation instructions

One earlier Phase 1B attempt used small overlay configs that changed `executable_name` but did
not reliably override `L2C.prefetcher` under this repo's `config.sh` merge behavior. Those runs are
invalid and are intentionally excluded here. Everything in this report uses rebuilt full configs
and corrected reruns only.

Authoritative artifacts:

- `results/generated/quick_1m3m_coord_20260417/summary/single_core.csv`
- `results/generated/quick_1m3m_coord_20260417/summary/multicore.csv`
- `results/generated/phase1c_cycle1_fetch/cycle1_multi.csv`
- `results/generated/phase1c_cycle23_fetch/cycle23_single.csv`
- `results/generated/phase1c_cycle23_fetch/cycle23_multi.csv`

## 1. Motivation / Observation

Phase 1 goal:

1. improve `4 x 470.lbm` weighted speedup by `+1%` over `orig`
2. then carry the same idea to `429.mcf + 470.lbm` without harming `mcf`

What the earlier search established:

- single-core `lbm` is easy to preserve
- hard tail cutting hurts `4 x lbm`
- controller-level runway/phase coordination changes bulk counters, but did not beat `orig`

That left a more specific Phase 1C question:

> if shared budget is limited, which prefetches should survive because they reduce
> **demand-critical** misses rather than merely increasing bulk useful-prefetch counts?

This cycle stayed aligned with the meta-controller framing. It did not change SPP's core pattern
generation. Instead it tried to change which already-generated candidates survive under pressure.

Methodology for this cycle:

- `5` logical controller families
- `3` short retuning cycles per family
- `4 x lbm` as the primary target
- `1c lbm` as the safety gate before any candidate could continue
- `mcf + lbm` kept closed unless a `4 x lbm` winner appeared
- remote execution on `office-kijun` using isolated workspaces per family

## 2. Ideas Tried

All five families start from the same safe, tail-preserving controller base and differ only in the
selectivity rule applied under shared pressure.

| Policy | Main signal | Intended effect |
| --- | --- | --- |
| `crit_blacklist` | learned bad-signature utility | block chronically unhelpful mid/tail candidates |
| `crit_confgate` | candidate confidence | keep only high-confidence tail traffic |
| `crit_levelgate` | pressure level + confidence | demote or keep different depths depending on observed pressure |
| `crit_rankcap` | candidate rank | cap deeper runway unless usefulness evidence promotes it |
| `crit_rescuebudget` | per-core rescue score | spend extra budget only on the currently best-looking core |

Each family was run through three cycles:

- `cycle1`: first usable implementation
- `cycle2`: first retune after observing failure mode
- `cycle3`: one more retune intended either to rescue the idea or prove it should be retired

## 3. Results

### 3.1 Single-core guardrail

All corrected `cycle2` and `cycle3` candidates preserved `1c lbm` exactly.

| Workload | IPC |
| --- | --- |
| `orig_lbm_1M3M` | `0.8747` |
| every `cycle2/cycle3` criticality candidate | `0.8747` |

So the remaining problem is entirely multicore.

### 3.2 `4 x lbm` cycle progression

Baseline:

- `orig_4xlbm_1M3M` weighted speedup = `1.0540756831`

Progress across the full 5-family x 3-cycle search:

| Policy | Cycle 1 | Cycle 2 | Cycle 3 | Best read |
| --- | --- | --- | --- | --- |
| `crit_blacklist` | `+0.000%` | `+0.000%` | `+0.000%` | completely inert |
| `crit_confgate` | `-0.575%` | `-0.174%` | `+0.000%` | over-prune -> nearly safe -> exact `orig` |
| `crit_levelgate` | `-0.553%` | `-0.553%` | `-0.163%` | best nontrivial candidate |
| `crit_rankcap` | `-0.672%` | `-0.390%` | `-0.423%` | still a suppressor, not a learner |
| `crit_rescuebudget` | `-0.564%` | `-0.390%` | `-0.390%` | rescue path collapsed |

No candidate beat `orig`. The best nontrivial result was:

- `crit_levelgate cycle3`: `WS = 1.0523608094`, which is still `-0.163%` vs `orig`

### 3.3 Why each family failed

`crit_blacklist`

- `filtered_prefetches=0`, `mid_filtered=0`, and `tail_filtered=0` in all three cycles.
- The blacklist never accumulated a strong enough negative signal to change behavior.
- Result: exact `orig` every time.

`crit_confgate`

- `cycle1` over-pruned badly: about `281k-288k` tail candidates filtered per core and `WS=-0.575%`.
- `cycle2` relaxed the thresholds and reduced tail filtering to about `50k` per core while rescuing
  about `240k-255k` tail candidates per core; WS improved to `-0.174%` but still lost.
- `cycle3` relaxed far enough that filtering became zero and the result reverted to exact `orig`.
- Interpretation: this family only interpolates between "harmful over-pruning" and "effectively off".
  No positive region appeared in the short-run search.

`crit_levelgate`

- `cycle3` was the strongest nontrivial candidate:
  `PF useful = 78,796` vs `45,442` for `orig`, `DBUS = 4.410` vs `4.468`, and
  `tail_demoted ≈ 262k-264k` per core with no hard tail drops.
- Yet WS still landed at `-0.163%`.
- This is the clearest evidence from the cycle: a policy can improve bulk proxies such as useful
  prefetch count and average DRAM bus occupancy without improving the misses that dominate WS.

`crit_rankcap`

- `cycle2` and `cycle3` widened the allowed ranks and reached `max_rank_observed=5`.
- But `useful_promotions=0` stayed zero, so the policy never actually learned which deeper ranks
  deserved rescue.
- Result: the family remained a cap-based suppressor, not a criticality learner.

`crit_rescuebudget`

- `cycle1` over-favored one core and shifted IPC unevenly without improving total WS.
- After softening that asymmetry, `cycle2` and `cycle3` reported
  `rescuebudget_grant_windows=0` and `rescuebudget_suppressed_windows≈272-283`.
- In other words, the "rescue" path never fired anymore. The policy collapsed into a plain
  suppression baseline and stayed at `-0.390%`.

### 3.4 Quantitative bottleneck reading

The key failure was not implementation correctness. The policies changed behavior, and the logs show
that clearly. The problem is that the available signals still tracked **bulk usefulness** more than
**throughput-critical usefulness**.

Three facts make that conclusion hard to avoid:

1. `crit_blacklist` did nothing because its learning signal was too weak to separate good and bad
   candidates.
2. `crit_levelgate cycle3` improved the best bulk counters of the whole search, yet still lost in
   weighted speedup.
3. `BWC_TIMELY fill_age*` stayed entirely in `age0`, so the remaining loss did not look like a
   simple "late prefetch arrival" problem in this short-run window.

The refined bottleneck is therefore:

> the controller still cannot predict which preserved prefetches reduce the demand misses that
> actually move weighted speedup.

This is more precise than saying "criticality-aware control is needed" in the abstract. The current
signals `confidence`, `useful`, `rank`, `density`, and similar counters are not yet aligned with
the marginal WS contribution of a prefetch.

### 3.5 `mcf + lbm` gate

The same-scale corrected baseline remains:

| Mix | CPU0 IPC | CPU1 IPC | WS |
| --- | --- | --- | --- |
| `orig_mcf_lbm_1M3M` | `0.03606` (`mcf`) | `0.8489` (`lbm`) | `1.3315735631` |

No `4 x lbm` candidate beat `orig`, so this cycle makes **no** new `mcf + lbm` claim.

## 4. Feedback

What worked:

- the search stayed within the meta-controller scope instead of turning into a new prefetcher
- isolated workspaces plus corrected full-config builds made the evidence trustworthy
- five distinct controller ideas were explored, and each was given three iterations

What failed:

- none of the five families beat `orig` on `4 x lbm`
- the strongest family (`crit_levelgate`) improved bulk counters but still missed the target
- the current signals still cannot separate "generally useful" from "WS-critical"

Most likely improvement direction:

- tie controller credit to demand-side stall reduction, not only prefetch usefulness
- estimate a prefetch's opportunity cost under shared pressure, not only its local success rate
- add conflict/victim awareness so a candidate can be rejected even if it is locally useful
- score per-core marginal WS gain before spending shared budget on that core

In short: the next step should still be a meta-controller, but it needs a better proxy for
**critical demand service**, not another round of confidence-only or rank-only gating.

## 5. Manual

This is the exact workflow that reproduced the corrected Phase 1C campaign.

### 5.1 Workspace layout

Use one isolated workspace per family, both locally and remotely. In this cycle the layout was:

- local main repo: `/Users/shinkijun/Developers/ChampSim` for report updates and `crit_confgate`
- local family workspaces:
  - `/Users/shinkijun/Developers/ChampSim_crit_blacklist`
  - `/Users/shinkijun/Developers/ChampSim_crit_levelgate_local`
  - `/Users/shinkijun/Developers/ChampSim_crit_rankcap`
  - `/Users/shinkijun/Developers/ChampSim_crit_rescuebudget`
- remote mirrors:
  - `~/Developers/ChampSim_crit_confgate`
  - `~/Developers/ChampSim_crit_blacklist`
  - `~/Developers/ChampSim_crit_levelgate`
  - `~/Developers/ChampSim_crit_rankcap`
  - `~/Developers/ChampSim_crit_rescuebudget`

This avoids one candidate build overwriting another candidate's generated `.csconfig`.

### 5.2 Sync and build

Sync each local family workspace into its matching remote workspace with `rsync`. Exclude
generated outputs and shared trace trees:

```sh
rsync -az \
  --exclude '.git/' \
  --exclude 'bin/' \
  --exclude '.csconfig/' \
  --exclude 'results/generated/' \
  --exclude 'traces/' \
  --exclude 'vcpkg/' \
  --exclude 'vcpkg_installed/' \
  --exclude 'test/' \
  /Users/shinkijun/Developers/ChampSim_crit_levelgate_local/ \
  office-kijun:~/Developers/ChampSim_crit_levelgate/
```

Build from full configs only. Do not use overlay configs.

Also, when launching many builds remotely, use `bash -lc` with `while read -r ...`; do not rely on
naive `zsh` word splitting for grouped specs. The latter caused a real mis-build earlier.

Template:

```sh
ssh office-kijun 'bash -lc "
cd ~/Developers/ChampSim_crit_levelgate
while read -r cfg bin expect; do
  make configclean >/dev/null
  ./config.sh \"$cfg\" >/dev/null
  rg -n \"$expect\" .csconfig/core_inst.cc.inc
  make -j16 \"bin/$bin\"
done <<EOF
configs/gsp_headgate_crit_levelgate_c3_config.json champsim_gsp_headgate_crit_levelgate_c3 spp_gsp_headgate_crit_levelgate_c3
configs/gsp_headgate_crit_levelgate_c3_4core.json champsim_gsp_headgate_crit_levelgate_c3_4core spp_gsp_headgate_crit_levelgate_c3
EOF
"'
```

Before trusting a run, verify both:

- the expected prefetcher name appears in `.csconfig/core_inst.cc.inc`
- the raw output log banner matches the intended variant

### 5.3 Run order

Use only short runs:

- `--warmup-instructions 1000000`
- `--simulation-instructions 3000000`

Run order:

1. refresh `orig_lbm_1M3M` and `orig_4xlbm_1M3M`
2. run each candidate on `1c lbm`
3. keep only candidates that preserve `1c lbm`
4. run those candidates on `4 x lbm`
5. open `mcf + lbm` only if a candidate actually beats `orig` on `4 x lbm`

### 5.4 Fetch and summarize

Fetch the final remote campaign directories back locally. Ignore temporary fetch directories such as
`*_partial`; those were intermediate copies only and should not be used for final analysis.

Use the extractor scripts in `scripts/`:

```sh
python3 scripts/extract_single_core_metrics.py \
  --inputs results/generated/phase1c_cycle23_fetch/*/results/generated/phase1c_cycle23/cycle*_lbm_1M3M.txt \
  --out-csv results/generated/phase1c_cycle23_fetch/cycle23_single.csv

python3 scripts/extract_multicore_metrics.py \
  --single-core-baselines results/generated/phase1c_cycle23_fetch/cycle23_single.csv \
  --inputs \
    results/generated/phase1c_cycle1_fetch/cycle1_crit_*_4xlbm_1M3M.txt \
    results/generated/phase1c_cycle23_fetch/*/results/generated/phase1c_cycle23/cycle*_4xlbm_1M3M.txt \
    results/generated/phase1c_cycle1_fetch/orig_4xlbm_1M3M.txt \
    results/generated/phase1c_cycle1_fetch/orig_mcf_lbm_1M3M.txt \
  --out-csv results/generated/phase1c_cycle23_fetch/cycle23_multi.csv
```

For this report, the authoritative final summaries are:

- `results/generated/phase1c_cycle23_fetch/cycle23_single.csv`
- `results/generated/phase1c_cycle23_fetch/cycle23_multi.csv`
