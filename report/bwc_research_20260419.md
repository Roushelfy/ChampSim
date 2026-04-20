# BWC Research Unified Report

This file supersedes the previous split notes for:

- single-core `astar`
- single-core `lbm`
- 2-core `astar + lbm`
- local / `office-kijun` execution procedure

It is now the only active file under `report/` for this repo snapshot.

The active short-cycle scope in this report is:

- host: `office-kijun`
- run length: `1M warmup + 5M simulation`
- workloads: single-core `astar`, single-core `lbm`, 2-core `astar + lbm`
- final target: beat stock `orig` (`SDP`) weighted speedup by `+0.5%`
- intermediate targets:
  - single-core `astar` IPC `+0.5%`
  - single-core `lbm` IPC `+0.5%`

## 1. Retained Active State

After cleanup, the active families kept in the main tree are:

- `prefetcher/spp_dev/`
  - retained because the best tuned single-core `astar` point came from the FDP family
- `prefetcher/spp_orig/`
  - retained because the best tuned single-core `lbm` point came from the `orig` family
- `prefetcher/spp_gsp_tiered/`
  - retained because the best tuned 2-core `astar + lbm` point came from this family

Pruned from the active current-cycle tree:

- `prefetcher/spp_bwc/`
- mixed-policy `hetero_*` configs
- intermediate `agent_runs`
- intermediate `quick_astar_lbm`
- split current-cycle notes and handoff files

Retained result summaries:

- [orig_astar_1M_5M.summary.json](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M/single_core/orig_astar_1M_5M.summary.json)
- [astar_best_fdp_loop2_pf20_1M_5M.summary.json](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M/single_core/astar_best_fdp_loop2_pf20_1M_5M.summary.json)
- [orig_lbm_1M_5M.summary.json](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M/single_core/orig_lbm_1M_5M.summary.json)
- [lbm_best_orig_iter2_lookahead_half_1M_5M.summary.json](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M/single_core/lbm_best_orig_iter2_lookahead_half_1M_5M.summary.json)
- [orig_astar_lbm_1M_5M.summary.json](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M/pair/orig_astar_lbm_1M_5M.summary.json)
- [astar_lbm_best_gsp_iter8_1M_5M.summary.json](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M/pair/astar_lbm_best_gsp_iter8_1M_5M.summary.json)

The three best-point summaries were rerun and re-verified on the cleaned `office-kijun` tree after this pruning pass.

## 2. Frozen Baseline References

These `orig` numbers are the frozen pre-pruning references used for all current-cycle comparisons.

Single-core `orig`:

| workload | IPC |
|---|---:|
| `astar` | `0.11610134952385481` |
| `lbm` | `0.8684027658975455` |

2-core `orig` pair:

- controlled reference WS used in the cycle analysis: `1.3609773502230509`
- retained raw summary WS: `1.3609840021286248`
- difference between them: `0.0000066519055739`

The small WS discrepancy came from the already-frozen control bookkeeping versus the retained parsed summary. It does not change any conclusion in this report. All target-gap calculations below use the controlled reference `1.3609773502230509` so they remain consistent with the cycle decisions.

Targets:

- single-core `astar` target IPC: `0.11668185627147408`
- single-core `lbm` target IPC: `0.8727447797270331`
- 2-core `astar + lbm` target WS: `1.367782236974166`

## 3. Best Tuned Points

### 3.1 Single-core `astar`

Best tuned point:

- family: `spp_dev`
- retained code: [spp_dev.h](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_dev/spp_dev.h)
- representative point name: `loop2_pf20`
- IPC: `0.116503266306`
- absolute delta vs `orig`: `+0.0004019167821451841`
- relative delta vs `orig`: `+0.34617752833494553%`
- miss to `+0.5%` target: `0.00017858996547408246`

Retained `spp_dev` code now freezes the winning astar-side FDP shape:

- `FDP_EPOCH_SIZE=1000`
- `FDP_PF_THRESH={0,20,20,20,15,5}`
- `FDP_FILL_THRESH={0,85,85,85,75,50}`

### 3.2 Single-core `lbm`

Best tuned point:

- family: `spp_orig`
- retained code:
  - [spp_orig.h](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.h)
  - [spp_orig.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.cc)
- representative point name: `iter2_lookahead_half`
- IPC: `0.8706`
- absolute delta vs `orig`: `+0.002197234102454537`
- relative delta vs `orig`: `+0.25302016399999516%`
- miss to `+0.5%` target: `0.002144779727033086`

Retained `spp_orig` code keeps the winning deep-lookahead semantics:

- rounded propagated confidence in the depth path
- relaxed lookahead continuation threshold: `max(pf_threshold / 2, 12)`
- safety bound on the prefetch queue walk

The retained controller line for the best point stayed at:

- `pf_threshold=25`
- `fill_threshold=90`

### 3.3 2-core `astar + lbm`

Best tuned point:

- family: `spp_gsp_tiered`
- retained code:
  - [spp_gsp_tiered.h](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_gsp_tiered/spp_gsp_tiered.h)
  - [spp_gsp_tiered.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_gsp_tiered/spp_gsp_tiered.cc)
- representative point name: `iter8_runway_relief_higher_thresholds_1M_5M`
- WS: `1.3656079886747214`
- absolute delta vs `orig`: `+0.0046306384516705545`
- relative delta vs `orig`: `+0.3402436088235872%`
- miss to `+0.5%` target: `0.002174248299444459`

Canonical retained env for the pair best point:

```bash
SPP_GSP_TIERED_PRESSURE_MODE=avg
SPP_GSP_TIERED_GLOBAL_MSHR_T1=0.30
SPP_GSP_TIERED_GLOBAL_MSHR_T2=0.38
SPP_GSP_TIERED_GLOBAL_MSHR_T3=0.46
SPP_GSP_TIERED_GLOBAL_LLC_T1=0.30
SPP_GSP_TIERED_GLOBAL_LLC_T2=0.38
SPP_GSP_TIERED_GLOBAL_LLC_T3=0.46
SPP_GSP_TIERED_LOCAL_RUNWAY_MSHR=0.65
SPP_GSP_TIERED_LOCAL_RUNWAY_LLC_RQ=0.18
SPP_GSP_TIERED_BURST_ACTIVATION_GAP=0.10
SPP_GSP_TIERED_CONGESTED_EPOCHS=3
SPP_GSP_TIERED_RELAXED_EPOCHS=2
SPP_GSP_TIERED_CANDIDATE_ID=iter8_runway_relief_higher_thresholds_1M_5M
```

Best-point pair IPCs:

- `astar=0.04813`
- `lbm=0.8259`

## 4. Current-State Landscape

### 4.1 Stock single-core landscape

| workload | policy | IPC | note |
|---|---|---:|---|
| `astar` | `orig` | `0.1161013495` | best stock baseline |
| `astar` | `fdp` | `0.1144643990` | below `orig` |
| `astar` | `bwc_local` | `0.1144373103` | below `fdp` |
| `astar` | `no_pref` | `0.1137791025` | worst stock point |
| `lbm` | `orig` | `0.8684027659` | best stock baseline |
| `lbm` | `bwc_local` | `0.8628744851` | slightly below `orig` |
| `lbm` | `fdp` | `0.6776885940` | large regression |
| `lbm` | `no_pref` | `0.6108538466` | worst stock point |

### 4.2 Stock 2-core pair landscape

| policy | `astar` IPC | `lbm` IPC | WS | delta vs `orig` |
|---|---:|---:|---:|---:|
| `orig` | `0.0464302364` | `0.8345924552` | `1.3609773502` | baseline |
| `gsp_default` | `0.05675` | `0.7210` | `1.3190569820` | `-0.0419203683` |
| `fdp` | `0.06579` | `0.6399` | `1.3035301420` | `-0.0574472082` |
| `bwc_local` | `0.04612` | `0.8259` | `1.3482955284` | `-0.0126818218` |

### 4.3 True per-core mixed-policy negatives

These were all explicit negative results and should not be rerun as current lead directions.

| config | `astar` IPC | `lbm` IPC | WS | delta vs `orig` |
|---|---:|---:|---:|---:|
| `hetero_fdp_orig` | `0.04550` | `0.8350` | `1.3534343751` | `-0.0075429751` |
| `hetero_bwc_orig` | `0.04546` | `0.8391` | `1.3578111600` | `-0.0031661902` |
| `hetero_nopref_orig` | `0.04534` | `0.8362` | `1.3534381161` | `-0.0075392342` |

Interpretation:

- naive per-core stock mixing was not the missing ingredient
- the remaining pair gap is not solved by simply swapping one core to `fdp`, `bwc_local`, or `no_pref`

## 5. Do-Not-Rerun Ledger

### 5.1 Single-core `astar` tuned sweep

| point | retained interpretation | IPC |
|---|---|---:|
| `baseline_astar_1M_5M` | stock `fdp` | `0.1145` |
| `loop1_epoch1000_origlike` | epoch `1000`, `pf=25`, `fill=85` | `0.1162` |
| `loop2_pf20` | epoch `1000`, `pf=20`, `fill=85` | `0.1165` |
| `loop3_pf15` | epoch `1000`, `pf=15`, `fill=85` | `0.1164` |
| `loop4_epoch1500_pf20` | epoch `1500`, `pf=20`, `fill=85` | `0.1164` |
| `loop5_fill70_45` | looser upper fill tiers | `0.1164` |

Takeaway:

- the winning lever was the shorter FDP epoch plus the `pf=20`, `fill=85` low-level point
- stretching the epoch back to `1500` removed the tiny but real edge

### 5.2 Single-core `lbm` tuned sweep

| point | retained interpretation | IPC |
|---|---|---:|
| `iter1_roundup` | rounded propagated confidence | `0.8706` |
| `iter2_lookahead_half` | iter1 + half-threshold lookahead continuation | `0.8706` |
| `iter3_fill85` | `fill=85` | `0.8685` |
| `iter4_pf20` | `pf=20`, `fill=85` | `0.8689` |
| `iter5_safe` | safer tuned variant | `0.8700` |

Takeaway:

- `lbm` wanted the deeper lookahead semantics
- lowering the stock `orig` thresholds hurt more than it helped

### 5.3 Hopper pressure-family sweep

| loop | key idea | WS | delta vs `orig` |
|---|---|---:|---:|
| `1` | default | `1.3190569820` | `-0.0419203683` |
| `2` | `max` + short-run winner ladder | `1.3163850009` | `-0.0445923493` |
| `3` | softer `max` ladder | `1.3190569820` | `-0.0419203683` |
| `4` | `avg` activation + shorter persistence | `1.3567364294` | `-0.0042409208` |
| `5` | softer `max` ladder, higher thresholds | `1.3432746710` | `-0.0177026792` |
| `6` | `avg`, thresholds `0.32/0.40/0.48`, persistence tightened | `1.3631101710` | `+0.0021328208` |
| `7` | `avg`, softer thresholds `0.34/0.42/0.50` | `1.3627656445` | `+0.0017882942` |

Takeaway:

- Hopper proved that moving from `max` to `avg` was necessary at `1M + 5M`
- even the best Hopper point still missed the target by `0.0046720659491905625` WS

### 5.4 Noether runway / relief family

| loop | key idea | WS | delta vs `orig` |
|---|---|---:|---:|
| `1` | short-run winner baseline | `1.3163850009` | `-0.0445923493` |
| `2` | `avg` pressure only | `1.3541524801` | `-0.0068248701` |
| `3` | low-accuracy override | `1.3189689502` | `-0.0420084000` |
| `4` | aggressive actuators | `1.3372923073` | `-0.0236850430` |
| `5` | runway + relief | `1.3638853558` | `+0.0029080056` |
| `6` | loop5 + `acc_low_throttle=0.005` | `1.3638853558` | `+0.0029080056` |
| `7` | loop5 + softer actuators | `1.3557028497` | `-0.0052745005` |
| `8` | loop5 + higher thresholds `0.30/0.38/0.46` | `1.3656079887` | `+0.0046306385` |

Takeaway:

- this was the best family overall
- the best point kept the `avg` activation semantics from loop5 and improved them with slightly higher thresholds

## 6. Why The Goals Were Missed

### 6.1 Intermediate single-core goals

Single-core `astar`:

- best IPC: `0.116503266306`
- target IPC: `0.11668185627147408`
- miss: `0.00017858996547408246`

Single-core `lbm`:

- best IPC: `0.8706`
- target IPC: `0.8727447797270331`
- miss: `0.002144779727033086`

Interpretation:

- `astar` was very close and mainly needed a tiny additional FDP-side gain
- `lbm` was not close enough; the remaining gap was materially larger

### 6.2 Final pair goal

Best pair:

- WS: `1.3656079886747214`
- target WS: `1.367782236974166`
- miss: `0.002174248299444459`

Equivalent remaining gap from `iter8`:

- if `lbm` is fixed at `0.8259`, needed `astar` gain is about `+0.0002524332` IPC
- if `astar` is fixed at `0.04813`, needed `lbm` gain is about `+0.0018881232` IPC

Quantitative interpretation:

- the pair bottleneck is still mostly on the `lbm` side
- `iter8` already found the right activation semantics for preserving `astar`
- the missing improvement is now a small additional `lbm` recovery under shared pressure without giving back the `astar` gain

So the current best explanation is:

- `max`-driven global triggering was too harsh at `1M + 5M`
- pure stock `avg+max` stayed too conservative about `lbm` preservation
- `avg` plus runway relief was the right direction
- but the current controller still lacks enough asymmetry to free `lbm` a little more once `astar` is already safe

## 7. Manual: Local <-> `office-kijun`

### 7.1 Sync

Use local as the source of truth and `office-kijun` as the execution copy.

```bash
rsync -av \
  --exclude '.git' \
  --exclude 'bin' \
  --exclude 'traces' \
  --exclude 'vcpkg' \
  --exclude 'vcpkg_installed' \
  --exclude '.csconfig' \
  --exclude '_configuration.mk' \
  --exclude 'absolute.options' \
  --exclude 'results/logs' \
  ./ office-kijun:~/Developers/ChampSim/
```

If machine-specific build files were already synced by mistake:

```bash
ssh office-kijun 'cd ~/Developers/ChampSim && rm -f absolute.options _configuration.mk'
```

### 7.2 Remote rebuild

For any trusted run, always rebuild from a clean generated state:

```bash
rm -f absolute.options _configuration.mk
make configclean
./config.sh <config-json>
make -j4 bin/<binary-name>
```

### 7.3 Active traces

Current active traces used in this cycle:

- `traces/473.astar-359B.champsimtrace.xz`
- `traces/470.lbm-1274B.champsimtrace.xz`

Do not silently swap in the older deprecated `473.astar-42B` name.

### 7.4 Retained run recipes

Single-core `astar` best:

```bash
rm -f absolute.options _configuration.mk
make configclean
./config.sh configs/spp_fdp_config.json
make -j4 bin/champsim_fdp

python3 scripts/quick_pair_eval.py \
  --binary ./bin/champsim_fdp \
  --trace ./traces/473.astar-359B.champsimtrace.xz \
  --warmup 1000000 \
  --simulation 5000000 \
  --summary-path results/retained_astar_lbm_1M_5M/single_core/astar_best_fdp_loop2_pf20_1M_5M.summary.json
```

Single-core `lbm` best:

```bash
rm -f absolute.options _configuration.mk
make configclean
./config.sh configs/spp_orig_config.json
make -j4 bin/champsim_orig

python3 scripts/quick_pair_eval.py \
  --binary ./bin/champsim_orig \
  --trace ./traces/470.lbm-1274B.champsimtrace.xz \
  --warmup 1000000 \
  --simulation 5000000 \
  --summary-path results/retained_astar_lbm_1M_5M/single_core/lbm_best_orig_iter2_lookahead_half_1M_5M.summary.json
```

2-core `astar + lbm` best:

```bash
rm -f absolute.options _configuration.mk
make configclean
./config.sh configs/gsp_tiered_2core.json
make -j4 bin/champsim_gsp_tiered_2core

SPP_GSP_TIERED_PRESSURE_MODE=avg \
SPP_GSP_TIERED_GLOBAL_MSHR_T1=0.30 \
SPP_GSP_TIERED_GLOBAL_MSHR_T2=0.38 \
SPP_GSP_TIERED_GLOBAL_MSHR_T3=0.46 \
SPP_GSP_TIERED_GLOBAL_LLC_T1=0.30 \
SPP_GSP_TIERED_GLOBAL_LLC_T2=0.38 \
SPP_GSP_TIERED_GLOBAL_LLC_T3=0.46 \
SPP_GSP_TIERED_LOCAL_RUNWAY_MSHR=0.65 \
SPP_GSP_TIERED_LOCAL_RUNWAY_LLC_RQ=0.18 \
SPP_GSP_TIERED_BURST_ACTIVATION_GAP=0.10 \
SPP_GSP_TIERED_CONGESTED_EPOCHS=3 \
SPP_GSP_TIERED_RELAXED_EPOCHS=2 \
SPP_GSP_TIERED_CANDIDATE_ID=iter8_runway_relief_higher_thresholds_1M_5M \
python3 scripts/quick_pair_eval.py \
  --binary ./bin/champsim_gsp_tiered_2core \
  --trace ./traces/473.astar-359B.champsimtrace.xz \
  --trace ./traces/470.lbm-1274B.champsimtrace.xz \
  --baseline-ipc astar=0.11610134952385481 \
  --baseline-ipc lbm=0.8684027658975455 \
  --warmup 1000000 \
  --simulation 5000000 \
  --summary-path results/retained_astar_lbm_1M_5M/pair/astar_lbm_best_gsp_iter8_1M_5M.summary.json
```

### 7.5 Parallel remote work

If multiple trees are needed on `office-kijun`, isolate them:

```bash
ssh office-kijun 'mkdir -p ~/Developers/ChampSim_agents/<worker>'
ssh office-kijun 'rsync -a --exclude .git --exclude traces ~/Developers/ChampSim/ ~/Developers/ChampSim_agents/<worker>/'
ssh office-kijun 'ln -sfn ~/Developers/ChampSim/traces ~/Developers/ChampSim_agents/<worker>/traces'
```

Do not share:

- `.csconfig`
- `_configuration.mk`
- `absolute.options`
- `bin/`

across concurrent code-changing workers.

## 8. Bottom Line

The current retained state is:

- single-core `astar` best tuned point: `+0.3462%`
- single-core `lbm` best tuned point: `+0.2530%`
- 2-core `astar + lbm` best tuned point: `+0.3402%`

So the cycle improved all three objectives, but none reached the requested `+0.5%` line.

The best next technical direction is not another stock policy swap.
It is:

- keep `spp_gsp_tiered`
- keep `avg`-pressure activation
- keep the runway-relief idea
- add a more asymmetric relief path so `lbm` regains roughly `0.0019` IPC without giving back the `astar` gain
