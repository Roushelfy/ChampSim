# BWC Research Unified Report

This is the single active report for the current repo state.

Current active cycle:

- date: `2026-04-20`
- host: `office-kijun`
- run length: `1M warmup + 5M simulation`
- final goal:
  - build a sliding-window runtime-adaptive in-simulator expert selector for `astar`, `lbm`, `mcf`, and `bzip2`
- acceptance rule:
  - for each workload, retain at least `70%` of the improvement from `orig` to the current best-tuned point
- workload assumption:
  - only `astar`, `lbm`, `mcf`, and `bzip2` exist
  - the final artifact may still be specialized to these four workloads, but it should rely on recent-window runtime properties instead of exact workload-name or exact first-demand-PC fingerprinting

Important interpretation rule for this cycle:

- the active artifact is now an in-simulator wrapper prefetcher, not the older external selector driver
- exact retention is still evaluated against the frozen office-kijun best-point ledger, and clean-rebuild validation is required on the current source
- the earlier `lbm` drift turned out to be a source-level reproducibility bug, not a valid retained performance point
- current source-of-truth therefore begins only after the reproducibility fixes in `spp_orig` and `spp_dev`

## 1. Executive Summary

Status at the end of this cycle:

| goal | result |
|---|---|
| in-simulator sliding-window selector implementation | achieved |
| reproducibility root cause isolation | achieved |
| best observed clean-rebuild `1M + 5M` selector run | achieved, `4/4` pass |
| repeated clean-rebuild target on `astar` | achieved |
| repeated clean-rebuild target on `mcf` | achieved |
| repeated clean-rebuild target on `bzip2` | achieved |
| repeated clean-rebuild target on `lbm` | achieved and reproduced identically across repeated reruns on the same binary |

Final retained selector artifact:

- in-sim selector prefetcher: [adaptive_selector.h](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.h), [adaptive_selector.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.cc)
- selector config: [adaptive_selector_config.json](/Users/shinkijun/Developers/ChampSim/configs/adaptive_selector_config.json)
- selector build helper: [build_singlecore_adaptive_selector_binaries.sh](/Users/shinkijun/Developers/ChampSim/scripts/build_singlecore_adaptive_selector_binaries.sh)
- selector run helper: [run_singlecore_adaptive_selector.py](/Users/shinkijun/Developers/ChampSim/scripts/run_singlecore_adaptive_selector.py)

Final retained rule family:

- selector window: recent `64` demand-memory accesses
- evaluation stride: every `16` demand-memory accesses
- initial mode: `none`
- hysteresis: switch only after the same candidate is seen in `2` consecutive evaluation windows
- lock policy: lock after the first switch
- selected rule style: `page_only`
  - if `page_growth < 0.17`, choose `fdp`
  - else if `page_growth < 0.40`, choose `orig_like`
  - else choose `bop`

Main conclusion:

- the retained selector is now a real in-simulator sliding-window expert selector
- the strongest family this cycle was locality-footprint-based online classification with hysteresis, not reward-only selection and not unsupervised clustering
- the retained `page_growth` rule achieves `4/4` target retention on the clean-rebuild validation run `reprofix2_full`
- the earlier `lbm` drift was caused by two reproducibility bugs in SPP-family lookahead code: queue-bound corruption and signed-shift UB
- after those fixes, the retained selector deterministically chooses `fdp` for `lbm` and reproduces the same `lbm` IPC across repeated reruns on the same binary

## 2. Frozen Baselines And Retention Targets

### 2.1 Frozen `orig` baselines and current best-tuned points

| workload | frozen `orig` IPC | current best-tuned point | exact best IPC | gain vs `orig` | minimum acceptable IPC |
|---|---:|---|---:|---:|---:|
| `astar` | `0.11624407059812944` | `bop` | `0.11689840080543869` | `+0.00065433020730925` | `0.11670210174324592` |
| `mcf` | `0.09304738191331209` | `bop` | `0.09483537300389441` | `+0.001787991090582322` | `0.09429897567671972` |
| `lbm` | `0.8705538637792136` | tuned `orig` | `0.8728014354438657` | `+0.0022475716646520594` | `0.8721271639444701` |
| `bzip2` | `2.132624022725237` | `fdp` | `2.1518726883512267` | `+0.01924866562598977` | `2.14609808866343` |

### 2.2 Retained in-sim selector outcome

The retained run is `reprofix2_full` from the in-simulator wrapper selector. All four workloads hit or exceed the minimum acceptable IPC on the reproducibility-fixed source.

| workload | selector decision | switch point | exact selected IPC | retention vs best gain | status |
|---|---|---:|---:|---:|---|
| `astar` | `bop` | `80` demand-memory accesses | `0.11769683762267738` | `222.0235%` | pass |
| `mcf` | `bop` | `80` demand-memory accesses | `0.0955303095422819` | `138.8669%` | pass |
| `lbm` | `fdp` | `80` demand-memory accesses | `0.8804757668415644` | `441.4499%` | pass |
| `bzip2` | `fdp` | `80` demand-memory accesses | `2.150898860454012` | `94.9408%` | pass |

Aggregate selector IPC sum:

- selector sum IPC: `3.244601774460535`
- frozen all-`orig` sum IPC: `3.2124693390158922`
- delta vs frozen all-`orig`: `+0.03213243544464284`

Exact switch features in the retained run:

- `astar`: `page_growth=0.65625 -> bop`
- `mcf`: `page_growth=0.96875 -> bop`
- `lbm`: `page_growth=0.09375 -> fdp`
- `bzip2`: `page_growth=0.125 -> fdp`

### 2.3 Validation sequence and drift

Three retained validation points define the current source-of-truth.

| label | rule | outcome | exact notes |
|---|---|---|---|
| `iter1_default` | `rfo + page + small-delta` | obsolete | old output retained only as a pre-fix diagnostic; not source-of-truth after the SPP reproducibility fixes |
| `reprofix2_full` | `page_growth` only | pass | clean-rebuild `4/4` pass on the reproducibility-fixed source |
| `reprofix3_lbm_r1/r2` | `page_growth` only | pass | repeated `lbm` reruns reproduced bit-identically on the same current binary |

Exact summary table:

| label | sum IPC | `astar` | `mcf` | `lbm` | `bzip2` |
|---|---:|---:|---:|---:|---:|
| `iter1_default` | `3.155439446413633` | `0.11753401884289778` | `0.0955303095422819` | `0.8233256725623921` | `2.1190494454660613` |
| `reprofix2_full` | `3.244601774460535` | `0.11769683762267738` | `0.0955303095422819` | `0.8804757668415644` | `2.150898860454012` |
| `reprofix3_lbm_r1` | `0.8804757668415644` | `-` | `-` | `0.8804757668415644` | `-` |
| `reprofix3_lbm_r2` | `0.8804757668415644` | `-` | `-` | `0.8804757668415644` | `-` |

## 3. Selector Design And Experiment Cycle

### 3.1 Current best implementation by workload

| workload | current best implementation | frozen best IPC | relative delta vs frozen `orig` |
|---|---|---:|---:|
| `astar` | `bop` | `0.11689840080543869` | `+0.5629077086242894%` |
| `mcf` | `bop` | `0.09483537300389441` | `+1.9215920467790415%` |
| `lbm` | tuned `spp_orig` | `0.8728014354438657` | `+0.258177208575594%` |
| `bzip2` | `fdp` | `2.1518726883512267` | `+0.9025642336338117%` |

### 3.2 Seven idea families and outcomes

| family | loops / status | best result | status |
|---|---:|---|---|
| fixed-window threshold + hysteresis (`W1`) | `8` | `128` demand-memory window + `2`-window hysteresis hits all four workloads offline; longer-horizon min retention `90.625%` | promising |
| sticky / hysteresis rediscovery (`W2`) | `5+` | rediscovery is dead, but seeded start + near-lock hysteresis is viable | diagnostic |
| runtime reward + activity (`W3`) | `9` | `pf_accuracy + pf_issued_per_kinst` separates `orig/fdp/bop` at `12/12` on longer probe windows | promising but slower |
| page / locality footprint (`W4`) | `7` | `page_growth` alone gives `4/4` window-majority separation; selected as the simplest retained family | selected |
| two-stage SPP split + BOP gate (`W5`) | `5` | `BOP` behaves as a gated specialist, not a safe default | diagnostic |
| centroid / score classifier (`W6`) | `7` | centroid / score works, unsupervised `k-means` fails badly on `lbm` | diagnostic |
| local in-sim wrapper implementation (`W7`) | `2` retained variants | `page_only` in-sim selector reaches `4/4` on the reproducibility-fixed validation `reprofix2_full` | retained |

### 3.3 Why the retained `page_only` rule works

The in-sim selector sees only recent demand-memory accesses, but `page_growth` still separates the four workloads early enough to choose the right expert on the reproducibility-fixed source.

Observed switch-window features in the retained pass:

| workload | `page_growth` | selected expert | interpretation |
|---|---:|---|---|
| `astar` | `0.65625` | `bop` | large early page churn, stream-like |
| `mcf` | `0.96875` | `bop` | extreme page churn |
| `lbm` | `0.09375` | `fdp` | compact early footprint under the fixed source, strongly below the `fdp` threshold |
| `bzip2` | `0.125` | `fdp` | compact footprint with high locality |

This maps directly onto the retained thresholds:

- `page_growth < 0.17 -> fdp`
- `0.17 <= page_growth < 0.40 -> orig_like`
- `page_growth >= 0.40 -> bop`

### 3.4 Why `iter1_default` failed

The first retained implementation (`iter1_default`) used a more complex `rfo + page + small-delta` rule. It correctly sent `astar` and `mcf` to `bop`, but:

- `bzip2` was sent to `orig_like` because its early wrapper-window behavior no longer matched the earlier offline trace-ref thresholds
- `lbm` was still sent to `orig_like`, but the run landed on the low `lbm` outlier

That made `iter1_default` useful as calibration, but not viable as the retained selector.

### 3.5 `lbm` reproducibility fix on the retained selector

The earlier `lbm` drift was not a meaningful selector result. It came from two bugs in the shared SPP-family lookahead path.

Ranked root-cause list:

1. queue-bound corruption in `PATTERN_TABLE::read_pattern`
   - [spp_orig.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.cc:364)
   - [spp_dev.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_dev/spp_dev.cc:400)
   - `pf_q_tail` could advance past queue capacity and later expose out-of-bounds writes / reads on `confidence_q` and `delta_q`
2. signed-shift undefined behavior in lookahead base-address updates
   - [spp_orig.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.cc:121)
   - [spp_dev.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_dev/spp_dev.cc:129)
   - negative `delta` values were shifted left before address reconstruction

Experimental evidence:

- pre-fix `lbm` page-only selector runs spanned `0.8240328861636473 -> 0.8728444464694424`
- after the queue-bound fix, the catastrophic low outlier disappeared
- after the signed-shift fix, repeated current-binary `lbm` reruns reproduced bit-identically

Repeated current-binary `lbm` runs:

| run | binary sha | decision | IPC | status |
|---|---|---|---:|---|
| `reprofix3_lbm_r1` | `b4b89a393ce38d258ea15fe1b329550599ae4081bf85c3c548f88977bc996915` | `fdp` | `0.8804757668415644` | pass |
| `reprofix3_lbm_r2` | `b4b89a393ce38d258ea15fe1b329550599ae4081bf85c3c548f88977bc996915` | `fdp` | `0.8804757668415644` | pass |

Quantitative summary:

- mean IPC: `0.8804757668415644`
- min IPC: `0.8804757668415644`
- max IPC: `0.8804757668415644`
- population stdev: `0.0`
- drift span: `0.0`

Important implication:

- the retained selector is now reproducible on the current binary
- the earlier `iter2_page_only*` `lbm` outputs are invalidated as UB-affected measurements
- the next cycle should start from the fixed source, not from the older `lbm orig_like` narrative

### 3.6 Stability bugs fixed during the selector work

Fixes retained in the repo:

- `spp_dev::GLOBAL_REGISTER::update_entry` and `spp_orig::GLOBAL_REGISTER::update_entry`
  - victim condition changed from `<` to `<=`
  - this removes the all-confidence-`100` no-victim assertion corner case
- `spp_orig::PATTERN_TABLE::read_pattern` and `spp_dev::PATTERN_TABLE::read_pattern`
  - queue growth is now bounded by the actual queue capacity
  - the stale sentinel-style `pf_q_tail++` growth is removed
- `spp_orig` and `spp_dev` lookahead base-address updates
  - negative deltas now use safe block-number arithmetic instead of signed left-shift UB

Effect:

- catastrophic `lbm` drift is gone
- current-binary repeated `lbm` runs are now reproducible
- the retained selector can be used as a clean baseline for the next experiment cycle

### 3.7 Updated do-not-rerun ledger

| workload | point | exact IPC | note |
|---|---|---:|---|
| `astar` | fresh current `orig` baseline | `0.11624407059812944` | baseline |
| `astar` | fresh retained `bop` point | `0.11689840080543869` | current best |
| `astar` | `reprofix2_full` | `0.11769683762267738` | retained selector pass on the reproducibility-fixed source |
| `mcf` | `bop` | `0.09483537300389441` | current best |
| `mcf` | `reprofix2_full` | `0.0955303095422819` | retained selector pass on the reproducibility-fixed source |
| `lbm` | frozen tuned `orig` deep_fill_bonus=`12` | `0.8728014354438657` | frozen oracle best |
| `lbm` | `iter2_page_only` | `0.8240328861636473` | obsolete; invalidated by the later SPP reproducibility fix |
| `lbm` | `iter2_page_only_repeatfull` | `0.8728444464694424` | obsolete; invalidated by the later SPP reproducibility fix |
| `lbm` | `reprofix2_full` | `0.8804757668415644` | retained selector pass on the reproducibility-fixed source |
| `lbm` | `reprofix3_lbm_r1` | `0.8804757668415644` | repeated current-binary reproducibility check |
| `lbm` | `reprofix3_lbm_r2` | `0.8804757668415644` | repeated current-binary reproducibility check |
| `bzip2` | earlier retained `fdp` point | `2.146046596671225` | superseded |
| `bzip2` | fresh current `fdp` point | `2.1518726883512267` | current best |
| `bzip2` | `iter1_default` | `2.1190494454660613` | pre-fix diagnostic fail |
| `bzip2` | `reprofix2_full` | `2.150898860454012` | retained selector pass on the reproducibility-fixed source |

## 4. Historical Pair-Focused Cycle
The sections below retain the previous hetero pair cycle for continuity. They are no longer the active goal for the repo state above.

### 4.1 True hetero `orig_orig` baselines

Corrected from CPU-to-trace mapping in the logs:

| pair | corrected per-workload IPCs | exact WS | target WS |
|---|---|---:|---:|
| hetero `mcf + lbm` | `mcf=0.033382548304568486`, `lbm=0.814687741656703` | `1.294596266161309` | `1.3010692474921155` |
| hetero `bzip2 + lbm` | `bzip2=2.2111608720282567`, `lbm=0.845029670681797` | `2.0075069050281593` | `2.0175444395533` |

Auxiliary homogeneous baselines, kept only for diagnosis:

| pair | corrected per-workload IPCs | exact WS |
|---|---|---:|
| homogeneous `mcf + lbm` | `mcf=0.032270971653009906`, `lbm=0.8146851169960682` | `1.282646900577691` |
| homogeneous `bzip2 + lbm` | `bzip2=2.001715469127761`, `lbm=0.84746279807809` | `1.9120916374190942` |

## 5. Historical Single-core Landscape

### 5.1 Best implementation by workload

| workload | best implementation | exact IPC | delta vs `orig` | relative delta |
|---|---|---:|---:|---:|
| `mcf` | `bop` | `0.09483537300389441` | `+0.001787991090582322` | `+1.9215920467790415%` |
| `lbm` | `spp_orig structural` | `0.8728014354438657` | `+0.0022475716646520594` | `+0.258177208575594%` |
| `bzip2` | `fdp` | `2.146046596671225` | `+0.013422573945987892` | `+0.6293924199932466%` |

Interpretation:

- `mcf` and `bzip2` already clear the intermediate target with stock families
- `lbm` does not
- this cycle's best stock families are:
  - `mcf -> bop`
  - `lbm -> orig`
  - `bzip2 -> fdp`

### 5.2 Single-core do-not-rerun ledger

| workload | point | exact IPC | delta vs `orig` |
|---|---|---:|---:|
| `mcf` | `orig` | `0.09304738191331209` | baseline |
| `mcf` | `fdp` | `0.09389816070441635` | `+0.0008507787911042606` |
| `mcf` | `bop` | `0.09483537300389441` | `+0.001787991090582322` |
| `mcf` | `no_pref` | `0.09036830429782895` | `-0.00267907761548314` |
| `lbm` | `orig` | `0.8705538637792136` | baseline |
| `lbm` | `fdp` | `0.8684541178595745` | `-0.0020997459196391` |
| `lbm` | `fdp orig-like` | `0.8684027658975455` | `-0.002151097881668118` |
| `lbm` | `bop` | `0.8207186146532415` | `-0.04983524912597215` |
| `lbm` | `bop conservative` | `0.8207145731988762` | `-0.049839290580337425` |
| `lbm` | `bop disable-fast` | `0.6108538466199747` | `-0.25970001715923896` |
| `lbm` | `spp_orig pf23` | `0.8697107237767432` | `-0.0008431400024704499` |
| `lbm` | `spp_orig pf23 fill88` | `0.8701702505498606` | `-0.000383613229352993` |
| `lbm` | `spp_orig depth-floor15` | `0.8705538637792136` | `+0.0` |
| `lbm` | `spp_orig depth-floor30` | `0.8705538637792136` | `+0.0` |
| `lbm` | `spp_orig deep_localmax` | `0.8682243769621871` | `-0.0023294868170264764` |
| `lbm` | `spp_orig blend_depth` | `0.8628407617020190` | `-0.007713102077194591` |
| `lbm` | `spp_orig deep_fill_bonus=8` | `0.8713316943006554` | `+0.0007778305214418248` |
| `lbm` | `spp_orig deep_fill_bonus=12` | `0.8728014354438657` | `+0.0022475716646520594` |
| `lbm` | `spp_orig deep_fill_bonus=16` | `0.8242694792482864` | `-0.04628438453092721` |
| `bzip2` | `orig` | `2.132624022725237` | baseline |
| `bzip2` | `fdp` | `2.146046596671225` | `+0.013422573945987892` |
| `bzip2` | `bop` | `2.0656116626063636` | `-0.06701236011887343` |
| `bzip2` | `no_pref` | `1.8992464929463884` | `-0.23337752977884846` |

`lbm` conclusion:

- simple `spp_orig` threshold sweeps plateaued at baseline
- `fdp` never beat `orig`
- `bop` and `no_pref` are clearly dead
- the single-core `lbm` target miss is `0.004352769318896027` IPC

## 6. Historical Pair Results: True Heterogeneous Core

### 6.1 Hetero `mcf + lbm`

Corrected WS ledger:

| point | mapping | corrected `mcf` IPC | corrected `lbm` IPC | exact WS | delta vs `orig_orig` | relative delta |
|---|---|---:|---:|---:|---:|---:|
| `loop00_orig_orig_mcf_lbm_1M_5M` | `orig / orig` | `0.033382548304568486` | `0.814687741656703` | `1.294596266161309` | baseline | baseline |
| `loop01_fdp_orig_mcf_lbm_1M_5M` | `fdp / orig` | `0.03343512427911825` | `0.8083887092740075` | `1.2879256506362977` | `-0.006670615525011359` | `-0.5152660871478365%` |
| `loop02_orig_fdp_mcf_lbm_1M_5M` | `orig / fdp` | `0.033295509641944825` | `0.8158351217788197` | `1.294978831807267` | `+0.00038256564595795517` | `+0.029550961636282125%` |
| `loop03_gsp_orig_mcf_lbm_1M_5M` | `gsp / orig` | `0.03336453827754033` | `0.8151071604867428` | `1.2948844924645488` | `+0.00028822630323976917` | `+0.022263798434574156%` |
| `loop04_orig_gsp_mcf_lbm_1M_5M` | `orig / gsp` | `0.03370987556370968` | `0.8005819269910913` | `1.2819108571501234` | `-0.012685409011185644` | `-0.9798737523629586%` |
| `loop06_orig_no_mcf_lbm_1M_5M` | `orig / no_pref` | `0.05601566565161073` | `0.5300254401407237` | `1.210849313457397` | `-0.08374695270391208` | `-6.468962941800815%` |

Best current point:

- `loop02_orig_fdp_mcf_lbm_1M_5M`
- exact WS: `1.294978831807267`
- delta vs hetero `orig_orig`: `+0.00038256564595795517`
- relative delta vs hetero `orig_orig`: `+0.029550961636282125%`
- miss to target: `0.006090415684848471`

### 6.2 Hetero `bzip2 + lbm`

Corrected WS ledger:

| point | mapping | corrected `bzip2` IPC | corrected `lbm` IPC | exact WS | delta vs `orig_orig` | relative delta |
|---|---|---:|---:|---:|---:|---:|
| `loop00_orig_orig_bzip2_lbm_1M_5M` | `orig / orig` | `2.2111608720282567` | `0.845029670681797` | `2.0075069050281593` | baseline | baseline |
| `loop02_orig_fdp_bzip2_lbm_1M_5M` | `orig / fdp` | `2.2105244775649586` | `0.8461920028811128` | `2.008543659928995` | `+0.001036754900835657` | `+0.051643902107589845%` |
| `loop03_gsp_orig_bzip2_lbm_1M_5M` | `gsp / orig` | `2.2010636419943572` | `0.8441783519241104` | `2.0017943494797716` | `-0.005712555548387677` | `-0.28455969611260734%` |
| `loop04_orig_gsp_bzip2_lbm_1M_5M` | `orig / gsp` | `2.2227688748528527` | `0.8328985050480282` | `1.9990149689355006` | `-0.008491936092658747` | `-0.42300906021240925%` |
| `loop1_bzip2cpu0_fdp_lbmorig` | `fdp / orig` | `1.9928472782710072` | `0.8493812597275389` | `1.9101370157348385` | `-0.09736988929332082` | `-4.850837665034818%` |
| `loop2_bzip2cpu0_bop_lbmorig` | `bop / orig` | `1.774761277003864` | `0.8476518686994037` | `1.8058886605793578` | `-0.20161824444880158` | `-10.04224439785542%` |
| `loop3_bzip2cpu0_nopref_lbmorig` | `no_pref / orig` | `1.4231682685848699` | `0.8537850425398398` | `1.6480697623379144` | `-0.3594371426902449` | `-17.90453554007686%` |

Best current point:

- `loop02_orig_fdp_bzip2_lbm_1M_5M`
- exact WS: `2.008543659928995`
- delta vs hetero `orig_orig`: `+0.001036754900835657`
- relative delta vs hetero `orig_orig`: `+0.051643902107589845%`
- miss to target: `0.009000779624305011`

## 5. Auxiliary Homogeneous Diagnostics

These are not the final-goal baselines, but they were still useful to understand the controller behavior.

### 5.1 Homogeneous `bzip2 + lbm`

Corrected homogeneous baseline:

- `orig = 1.9120916374190942`

Best symmetric pressure-family point:

- `iter2_pressure_avg = 1.8942865932030166`
- delta vs homogeneous `orig`: `-0.017805044216077568`
- relative delta: `-0.9311815327067929%`

Takeaway:

- symmetric global pressure shaping was already dead before moving into the true hetero space

### 5.2 Homogeneous `mcf + lbm`

Corrected homogeneous baseline:

- `orig = 1.282646900577691`

Best homogeneous asymmetry point tested in this cycle:

- `mcf_lbm_asym_guard_v1 = 1.2190569246596739`
- delta vs homogeneous `orig`: `-0.06358997591801718`
- relative delta: `-4.957874141502662%`

Takeaway:

- the per-core asymmetry idea as tested in the homogeneous controller space was also clearly dead

## 6. Quantitative Failure Analysis

### 6.1 Why the single-core `lbm` goal failed

The intermediate target was:

- `0.8749066330981096`

Best exact point found:

- `0.8728014354438657`

Miss:

- `0.0021051976542438844` IPC

What the data says:

- `spp_orig` threshold changes do not create headroom; they only move `lbm` around baseline or below it
- `fdp` is consistently below `orig`
- `bop` is not competitive at all for `lbm`
- the only positive new headroom came from a structural `spp_orig` variant with moderate deep-chain fill expansion (`deep_fill_bonus=12`)
- therefore pair headroom has to come from coordination around `lbm`, not from replacing `lbm`'s single-core winner

### 6.2 Why hetero `mcf + lbm` missed

Best point:

- `orig_fdp = 1.294978831807267`

Compared to hetero `orig_orig`:

- `mcf`: `0.033382548304568486 -> 0.033295509641944825`
  - delta: `-0.000087038662623661`
- `lbm`: `0.814687741656703 -> 0.8158351217788197`
  - delta: `+0.001147380122116734`
- net WS gain: `+0.00038256564595795517`

Remaining gap to target:

- `0.006090415684848471` WS

Equivalent additional gain still needed:

- if only `mcf` improves: `+0.0005666972342389219` IPC
- if only `lbm` improves: `+0.005302034906466362` IPC

Interpretation:

- the pair can already recover a little more `lbm` with `orig_fdp`
- but it is still not extracting enough of the large single-core `mcf` headroom
- that makes the likely next step a true hetero controller that protects `lbm` while carrying more of `mcf`'s single-core `bop`-like gain into the pair

### 6.3 Why hetero `bzip2 + lbm` missed

Best point:

- `orig_fdp = 2.008543659928995`

Compared to hetero `orig_orig`:

- `bzip2`: `2.2111608720282567 -> 2.2105244775649586`
  - delta: `-0.0006363944632981466`
- `lbm`: `0.845029670681797 -> 0.8461920028811128`
  - delta: `+0.001162332199315832`
- net WS gain: `+0.001036754900835657`

Remaining gap to target:

- `0.009000779624305011` WS

Equivalent additional gain still needed:

- if only `bzip2` improves: `+0.0191952788500487` IPC
- if only `lbm` improves: `+0.007835663478963946` IPC

Interpretation:

- `bzip2` is already very strong in the hetero baseline
- the missing WS is mostly an `lbm` protection / coordination problem
- more aggressive `bzip2`-side policy swaps do not help; they mostly damage the pair

## 7. Idea Families Run In This Cycle

At least seven distinct logical idea families were exercised. The useful ones are already in the ledgers above; the full family list was:

1. single-core `mcf` stock landscape and FDP retune
2. single-core `lbm` `spp_orig` threshold / continuation sweep
3. single-core `lbm` alternative stock family: `fdp`
4. single-core `lbm` alternative stock family: `bop`
5. single-core `lbm` structural deep-lookahead / continuation-gating changes
6. homogeneous `bzip2 + lbm` symmetric GSP env family
7. homogeneous `mcf + lbm` asymmetric GSP family
8. true hetero `bzip2 + lbm` stock-mix family
9. true hetero `mcf + lbm` stock-mix family

Dead directions from this cycle:

- `lbm` on `bop`
- `lbm` on `fdp`
- homogeneous symmetric GSP for `bzip2 + lbm`
- homogeneous asymmetric GSP for `mcf + lbm`
- `bzip2` on `bop` or `no_pref` inside the hetero pair
- `lbm` on `no_pref` inside either pair

## 8. Execution Manual

### 8.1 Local to `office-kijun` sync

Use local-to-remote sync only for source files, never for remote build metadata:

```bash
rsync -av --delete \
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

### 8.2 Safe remote worker-copy procedure

For a worker copy on `office-kijun`, clone from the remote main tree, not directly from local:

```bash
MAIN=~/Developers/ChampSim
ROOT=~/Developers/ChampSim_agents/<worker>
rsync -a --delete --exclude 'bin' --exclude 'results/logs' --exclude 'traces' "$MAIN/" "$ROOT/"
ln -sfn "$MAIN/traces" "$ROOT/traces"
```

This avoids breaking `fmt` and other remote include paths.

### 8.3 Clean rebuild

```bash
rm -f _configuration.mk
make configclean
./config.sh <config>
make -j4 bin/<target>
```

### 8.4 Pair-result sanity check

`quick_pair_eval.py` now prefers the exact `instructions / cycles` from the `CPU n cumulative IPC` lines, which preserves both precision and CPU ordering.

If you need to validate an old pair run manually:

```bash
grep -n 'CPU [01] runs\|CPU [01] cumulative IPC' <log>
```

Trust the CPU-to-trace mapping from the log, then recompute:

```text
WS = IPC_workload0 / single_core_orig_workload0
   + IPC_workload1 / single_core_orig_workload1
```

## 9. Source-of-Truth Paths

Current cycle artifacts:

- sliding-window selector research artifacts:
  - [results/cycle_sliding_window_selector_20260420](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420)
- retained in-sim selector prefetcher:
  - [adaptive_selector.h](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.h)
  - [adaptive_selector.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/adaptive_selector/adaptive_selector.cc)
- retained selector config:
  - [adaptive_selector_config.json](/Users/shinkijun/Developers/ChampSim/configs/adaptive_selector_config.json)
- retained selector build helper:
  - [scripts/build_singlecore_adaptive_selector_binaries.sh](/Users/shinkijun/Developers/ChampSim/scripts/build_singlecore_adaptive_selector_binaries.sh)
- retained selector run helper:
  - [scripts/run_singlecore_adaptive_selector.py](/Users/shinkijun/Developers/ChampSim/scripts/run_singlecore_adaptive_selector.py)
- office-kijun clean-rebuild selector validation:
  - [reprofix2_full.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/main_thread/summaries/reprofix2_full.json)
  - [reprofix3_lbm_r1.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/main_thread/summaries/reprofix3_lbm_r1.json)
  - [reprofix3_lbm_r2.json](/Users/shinkijun/Developers/ChampSim/results/cycle_sliding_window_selector_20260420/main_thread/summaries/reprofix3_lbm_r2.json)
- retained stability fixes:
  - [spp_dev.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_dev/spp_dev.cc)
  - [spp_orig.cc](/Users/shinkijun/Developers/ChampSim/prefetcher/spp_orig/spp_orig.cc)
- single-core baselines:
  - [results/cycle_mcf_bzip2_lbm_20260420/single_core](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_bzip2_lbm_20260420/single_core)
- `lbm` alternative-family artifacts:
  - [results/cycle_mcf_bzip2_lbm_20260420/agent_runs/lbm_alt](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_bzip2_lbm_20260420/agent_runs/lbm_alt)
- `lbm` structural `spp_orig` artifacts:
  - [results/cycle_mcf_bzip2_lbm_20260420/agent_runs/lbm_orig_structural](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_bzip2_lbm_20260420/agent_runs/lbm_orig_structural)
- hetero `bzip2 + lbm` fast-mix artifacts:
  - [results/cycle_mcf_bzip2_lbm_20260420/agent_runs/pair_bzip2_lbm_hetero_fast](/Users/shinkijun/Developers/ChampSim/results/cycle_mcf_bzip2_lbm_20260420/agent_runs/pair_bzip2_lbm_hetero_fast)
- true hetero mixed-binary artifacts on `office-kijun`:
  - `~/Developers/ChampSim_agents/cycle20260420_pair_hetero_mix/results/cycle_mcf_bzip2_lbm_20260420/agent_runs/pair_hetero_mix`

Retained previous-cycle reference:

- [results/retained_astar_lbm_1M_5M](/Users/shinkijun/Developers/ChampSim/results/retained_astar_lbm_1M_5M)

Retained code change from this cycle:

- [scripts/quick_pair_eval.py](/Users/shinkijun/Developers/ChampSim/scripts/quick_pair_eval.py)

Previous `astar + lbm` findings are no longer the active scope for this file, but their retained summaries remain in the path above.
