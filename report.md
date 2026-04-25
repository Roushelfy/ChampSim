# BWC Adaptive Selector Report

This is the only active project report. It describes the current retained
prefetcher and the minimum local steps needed to rebuild and rerun it.

## Current Artifact

The retained prefetcher is `adaptive_selector`.

- Source: `prefetcher/adaptive_selector/adaptive_selector.h`
- Source: `prefetcher/adaptive_selector/adaptive_selector.cc`
- Internal expert dependencies: `prefetcher/spp_orig`, `prefetcher/spp_dev`, `prefetcher/bop`, `prefetcher/no`
- Single-core config: `configs/adaptive_selector_config.json`
- Two-core config: `configs/adaptive_selector_2core.json`

The selector uses recent demand-access windows to choose among `orig`, `fdp`,
and `bop`-like behavior. In two-core runs, each core keeps local window state,
and a shared coordination layer adjusts the decision when the peer stream looks
like it should be protected from harmful contention.

No remote host is required. The repo assumes traces are available under the
local `traces/` directory or at paths passed directly on the command line.

## Retained Results

Run length for the retained measurements was `1M warmup + 5M simulation`.

Single-core retained IPCs:

| workload | `orig` IPC | retained IPC | relative gain |
|---|---:|---:|---:|
| `astar` | `0.11624407059812944` | `0.11769683762267738` | `+1.2498%` |
| `mcf` | `0.09304738191331209` | `0.0955303095422819` | `+2.6685%` |
| `lbm` | `0.8705538637792136` | `0.8874912094048452` | `+1.9456%` |
| `bzip2` | `2.132624022725237` | `2.150898860454012` | `+0.8569%` |

Two-core heterogeneous retained weighted speedups:

| pair | `orig_orig` WS | retained WS | relative gain |
|---|---:|---:|---:|
| `astar + lbm` | `1.3562017695341675` | `1.3751206958751774` | `+1.3950%` |
| `bzip2 + lbm` | `1.9383916294876093` | `1.9496149302999837` | `+0.5790%` |
| `mcf + lbm` | `1.282646900577691` | `1.297473125485411` | `+1.1559%` |

The current retained version is the generalized pair-safe selector.

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

Run a fixed expert through the same binary for local comparisons:

```bash
ADAPT_INITIAL_MODE=orig bin/champsim_adaptive_selector \
  --warmup-instructions 1000000 \
  --simulation-instructions 5000000 \
  traces/470.lbm-1274B.champsimtrace.xz
```

Accepted `ADAPT_INITIAL_MODE` values are `none`, `orig`, `fdp`, and `bop`.
The retained adaptive runs use the default `none` start.
