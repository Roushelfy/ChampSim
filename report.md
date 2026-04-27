# BWC Adaptive Selector Report

This is the only active project report. It documents the current retained
prefetcher, its measured behavior, and the compact steps needed to rebuild and
rerun it.

## Current Artifact

The retained prefetcher is `adaptive_selector`.

- Source: `prefetcher/adaptive_selector/adaptive_selector.h`
- Source: `prefetcher/adaptive_selector/adaptive_selector.cc`
- Internal expert dependencies: `prefetcher/spp_orig`, `prefetcher/spp_dev`, `prefetcher/bop`, `prefetcher/no`
- Single-core config: `configs/adaptive_selector_config.json`
- Two-core config: `configs/adaptive_selector_2core.json`

The selector uses recent demand-access windows to choose among `orig`, `fdp`,
and `bop` behavior without workload names. In two-core runs, each core keeps
local window state and a shared signal layer compares peer page growth,
small-delta density, and pressure.

Current control rules:

- Single core: classify by page growth and small-delta ratio.
- High-page homogeneous pairs: keep `bop` for sparse mid-high page growth; remap to `fdp` for sparse ultra-high page growth.
- Low-page pairs: keep dense low-page streams conservative; if an initial low-page `fdp` choice is ambiguous, keep the selector unlocked for 3 evaluations so it can converge to `orig`.
- Heterogeneous pairs: protect dense low-page peers while allowing high-page streams to use the aggressive expert when the shared signals are safe.

## Current Results

All measurements use `1M warmup + 5M simulation`. No runtime env overrides were
used.

For heterogeneous two-core JSON output, ChampSim emitted cores in reverse trace
order in this setup. The WS values below are computed with trace-aligned IPCs.

Single-core IPC:

| workload | `orig` IPC | current IPC | gain vs `orig` |
|---|---:|---:|---:|
| `astar` | `0.11624407059812944` | `0.11769683762267738` | `+1.2498%` |
| `mcf` | `0.09304738191331209` | `0.0955303095422819` | `+2.6685%` |
| `lbm` | `0.8705538637792136` | `0.8874912094048452` | `+1.9456%` |
| `bzip2` | `2.132624022725237` | `2.150898860454012` | `+0.8569%` |

Two-core heterogeneous WS:

| pair | `orig` WS | current WS | gain vs `orig` |
|---|---:|---:|---:|
| `astar + lbm` | `1.3562017695341675` | `1.3751206958751774` | `+1.3950%` |
| `bzip2 + lbm` | `1.9383916294876093` | `1.9496149302999837` | `+0.5790%` |
| `mcf + lbm` | `1.282646900577691` | `1.297473125485411` | `+1.1559%` |

Two-core homogeneous WS:

| pair | `orig` WS | current WS | gain vs `orig` |
|---|---:|---:|---:|
| `astar + astar` | `1.5987553558826044` | `1.6661295598016541` | `+4.2142%` |
| `mcf + mcf` | `1.7032220284999422` | `1.733554965378456` | `+1.7809%` |
| `lbm + lbm` | `1.0546594169690442` | `1.0564571761565638` | `+0.1705%` |
| `bzip2 + bzip2` | `2.016283091759642` | `2.0173219540962366` | `+0.0515%` |

## Compact Manual

Use a clean config rebuild after changing prefetcher code or configs:

```bash
make configclean
./config.sh configs/adaptive_selector_config.json
make -j"$(sysctl -n hw.ncpu)" bin/champsim_adaptive_selector

make configclean
./config.sh configs/adaptive_selector_2core.json
make -j"$(sysctl -n hw.ncpu)" bin/champsim_adaptive_selector_2core
```

Run a single-core case:

```bash
bin/champsim_adaptive_selector \
  --warmup-instructions 1000000 \
  --simulation-instructions 5000000 \
  traces/473.astar-359B.champsimtrace.xz
```

Run a two-core pair:

```bash
bin/champsim_adaptive_selector_2core \
  --warmup-instructions 1000000 \
  --simulation-instructions 5000000 \
  traces/429.mcf-51B.champsimtrace.xz \
  traces/470.lbm-1274B.champsimtrace.xz
```

Run a fixed expert for local comparisons:

```bash
ADAPT_INITIAL_MODE=orig bin/champsim_adaptive_selector \
  --warmup-instructions 1000000 \
  --simulation-instructions 5000000 \
  traces/470.lbm-1274B.champsimtrace.xz
```

Accepted `ADAPT_INITIAL_MODE` values are `none`, `orig`, `fdp`, and `bop`.
The retained adaptive runs use the default `none` start.
