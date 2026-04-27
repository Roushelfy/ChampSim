# BWC Adaptive Selector Report

## Source Versions

| version | source |
|---|---|
| phase 1 final prefetcher | `prefetcher/adaptive_selector/adaptive_selector.cc` |
| phase 2 OpenEvolve best prefetcher | `prefetcher/adaptive_selector_phase2_best/adaptive_selector.cc` |

## Phase 2 OpenEvolve Best (2M + 10M)

| case | SPP_orig baseline | phase 1 source | phase 1 gain | phase 2 best | phase 2 gain | delta |
|---|---:|---:|---:|---:|---:|---:|
| `lbm` IPC | `0.866441` | `0.8696655919051541` | `+0.3722%` | `0.8696655919051541` | `+0.3722%` | `+0.0000 pp` |
| `bzip2 + lbm` WS | `1.792235` | `1.7917226370235841` | `-0.0286%` | `1.7937053010534283` | `+0.0820%` | `+0.1106 pp` |
| `mcf + lbm` WS | `1.256300` | `1.243539575921284` | `-1.0157%` | `1.2495028378164286` | `-0.5410%` | `+0.4747 pp` |

| metric | phase 1 source | phase 2 best | delta |
|---|---:|---:|---:|
| `combined_score` | `-6.2665` | `-3.0105` | `+3.2559` |
| `mean_gain_pct` | `-0.2240%` | `-0.0289%` | `+0.1951 pp` |
| `min_gain_pct` | `-1.0157%` | `-0.5410%` | `+0.4747 pp` |

Phase 2 changed only the shared pair-coordination decision logic in `coordinate_candidate()`. The best point remaps several pair-mode fallback cases from `orig` to `FDP`: dense-peer split handling, pair-scope BOP demotion, and lbm-like peer protection. It also adds two runtime-signal guards that promote FDP when a sparse/high-page-growth core is paired with an lbm-like peer, or when an lbm-like core is paired with a high-page-growth peer. This leaves single-core `lbm` unchanged while reducing shared-resource interference in `bzip2 + lbm` and `mcf + lbm`.

## Single-Core IPC

| workload | baseline IPC | current IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.11624407059812944` | `0.11769683762267738` | `+1.2498%` |
| `mcf` | `0.09304738191331209` | `0.0955303095422819` | `+2.6685%` |
| `lbm` | `0.8705538637792136` | `0.8874912094048452` | `+1.9456%` |
| `bzip2` | `2.132624022725237` | `2.150898860454012` | `+0.8569%` |

## Two-Core Heterogeneous WS

| pair | baseline WS | current WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.7777866560490196` | `1.8249906616335736` | `+2.6552%` |
| `astar + bzip2` | `1.8710239670004363` | `1.8993594471803694` | `+1.5144%` |
| `bzip2 + mcf` | `1.8688840696702544` | `1.8943434680436386` | `+1.3623%` |
| `astar + lbm` | `1.3562017695341675` | `1.3751206958751774` | `+1.3950%` |
| `bzip2 + lbm` | `1.9383916294876093` | `1.9496149302999837` | `+0.5790%` |
| `mcf + lbm` | `1.282646900577691` | `1.297473125485411` | `+1.1559%` |

## Two-Core Homogeneous WS

| pair | baseline WS | current WS | gain |
|---|---:|---:|---:|
| `astar + astar` | `1.5987553558826044` | `1.6661295598016541` | `+4.2142%` |
| `mcf + mcf` | `1.7032220284999422` | `1.733554965378456` | `+1.7809%` |
| `lbm + lbm` | `1.0546594169690442` | `1.0564571761565638` | `+0.1705%` |
| `bzip2 + bzip2` | `2.016283091759642` | `2.0173219540962366` | `+0.0515%` |
