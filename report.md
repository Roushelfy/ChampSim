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

## Phase 1 Final Scale Sweep

Baseline is `spp_orig`; current is the phase 1 final `adaptive_selector`. Single-core rows report IPC, and two-core rows report weighted speedup (WS). Runs were completed on `office-kijun` with at most 3 sweep jobs in parallel.

| scale | single-core average gain | two-core average gain |
|---|---:|---:|
| `1M + 5M` | `+1.5464%` | `+1.3677%` |
| `2M + 10M` | `+1.8540%` | `+1.6013%` |
| `4M + 20M` | `+2.6782%` | `+1.3017%` |

## 1M + 5M Single-Core IPC

| workload | baseline IPC | adaptive IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.114681681637` | `0.117696837623` | `+2.6292%` |
| `mcf` | `0.0921060128519` | `0.0955303095423` | `+3.7178%` |
| `lbm` | `0.888119994224` | `0.887491209405` | `-0.0708%` |
| `bzip2` | `2.15285110626` | `2.15089886045` | `-0.0907%` |

## 1M + 5M Two-Core WS

| pair | baseline WS | adaptive WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.79889602604` | `1.84664106396` | `+2.6541%` |
| `astar + bzip2` | `1.8733459097` | `1.90206011146` | `+1.5328%` |
| `bzip2 + mcf` | `1.86822168103` | `1.89396475041` | `+1.3779%` |
| `astar + lbm` | `1.34207381864` | `1.36131871857` | `+1.4340%` |
| `bzip2 + lbm` | `1.90987686659` | `1.92099258692` | `+0.5820%` |
| `mcf + lbm` | `1.28367655124` | `1.28212254127` | `-0.1211%` |
| `astar + astar` | `1.62053632111` | `1.68882841105` | `+4.2142%` |
| `mcf + mcf` | `1.72062980105` | `1.75127275557` | `+1.7809%` |
| `lbm + lbm` | `1.03379930233` | `1.03556150363` | `+0.1705%` |
| `bzip2 + bzip2` | `1.99733913116` | `1.99836823288` | `+0.0515%` |

## 2M + 10M Single-Core IPC

| workload | baseline IPC | adaptive IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.123092697685` | `0.127849553135` | `+3.8644%` |
| `mcf` | `0.133869243938` | `0.138126056966` | `+3.1798%` |
| `lbm` | `0.866440779824` | `0.869665591905` | `+0.3722%` |
| `bzip2` | `2.15021252696` | `2.15019865681` | `-0.0006%` |

## 2M + 10M Two-Core WS

| pair | baseline WS | adaptive WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.67757932858` | `1.75129060943` | `+4.3939%` |
| `astar + bzip2` | `1.85535929286` | `1.89702527774` | `+2.2457%` |
| `bzip2 + mcf` | `1.85014350971` | `1.89079633825` | `+2.1973%` |
| `astar + lbm` | `1.32822308169` | `1.34662973574` | `+1.3858%` |
| `bzip2 + lbm` | `1.7922349941` | `1.7917228544` | `-0.0286%` |
| `mcf + lbm` | `1.2563002705` | `1.24353980771` | `-1.0157%` |
| `astar + astar` | `1.61198327646` | `1.69157360275` | `+4.9374%` |
| `mcf + mcf` | `1.68285731121` | `1.7132872924` | `+1.8082%` |
| `lbm + lbm` | `1.05260903871` | `1.05335202216` | `+0.0706%` |
| `bzip2 + bzip2` | `1.99892024318` | `1.99929371345` | `+0.0187%` |

## 4M + 20M Single-Core IPC

| workload | baseline IPC | adaptive IPC | gain |
|---|---:|---:|---:|
| `astar` | `0.127089889173` | `0.134589236644` | `+5.9008%` |
| `mcf` | `0.12300024918` | `0.128947331077` | `+4.8350%` |
| `lbm` | `0.868004562405` | `0.867861433966` | `-0.0165%` |
| `bzip2` | `2.17156526525` | `2.17142200753` | `-0.0066%` |

## 4M + 20M Two-Core WS

| pair | baseline WS | adaptive WS | gain |
|---|---:|---:|---:|
| `mcf + astar` | `1.67968369463` | `1.75831774679` | `+4.6815%` |
| `astar + bzip2` | `1.90015563102` | `1.95292292993` | `+2.7770%` |
| `bzip2 + mcf` | `1.91154295712` | `1.93582008508` | `+1.2700%` |
| `astar + lbm` | `1.30142044017` | `1.29975024337` | `-0.1283%` |
| `bzip2 + lbm` | `1.7056566415` | `1.70318728384` | `-0.1448%` |
| `mcf + lbm` | `1.25495645906` | `1.24672830361` | `-0.6557%` |
| `astar + astar` | `1.64374110449` | `1.71254953306` | `+4.1861%` |
| `mcf + mcf` | `1.71524235015` | `1.73465860811` | `+1.1320%` |
| `lbm + lbm` | `1.05203463194` | `1.05080569528` | `-0.1168%` |
| `bzip2 + bzip2` | `1.96177282621` | `1.96207781216` | `+0.0155%` |
