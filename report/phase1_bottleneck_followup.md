# Phase 1 Bottleneck Follow-up
## Astar Expansion, Fair Reruns, And Weighted-Speedup Diagnosis

This note refines the original Phase 1 diagnosis after two additional steps:

1. extending the workload set with `473.astar`, and
2. rerunning the short-window experiments after fixing a config-drift issue in the early `gsp_*` comparisons.

The goal here is to keep only the parts that survived the fair reruns:

- what algorithms were tried,
- what actually happened when they ran,
- what bottleneck was observed quantitatively,
- and what that says about the next design iteration.

## 1. Workload Roles

The current short-window suite still spans three useful behavior classes:

- `429.mcf`: latency-bound, near-zero prefetch accuracy, extremely fragile to over-throttling
- `470.lbm`: high-throughput streaming case, useful for studying symmetric shared pressure
- `473.astar`: a midpoint workload that is still stall-dominated but has nontrivial prefetch utility

For the fair short-window `orig` runs (`1M` warmup + `5M` simulation), the anchor behavior is:

| Workload | IPC | PF Accuracy | Demand Stall % | DBUS | Row-Hit Rate |
|----------|-----|-------------|----------------|------|--------------|
| `mcf` | `0.09305` | `0.0157%` | `95.90%` | `7.552` | about `0.14%` |
| `astar` | `0.1161` | `3.73%` | `94.95%` | `9.038` | about `2.6%` |
| `lbm` | `0.8684` | `4.99%` | `78.55%` | `7.479` | about `6.3%` |

The main classification result remains intact:

- `astar` is still much closer to `mcf` than to `bzip2` in stall behavior,
- but unlike `mcf`, it does have some real prefetch utility,
- which makes it a good discriminator for whether a controller understands utility instead of just aggressiveness.

## 2. Algorithms Tried

Three controller ideas matter for the current diagnosis:

1. `BWC`
   - local bandwidth-aware throttling with an accuracy fallback
2. `c1_gsp_tiered`
   - the same base controller plus shared-pressure tiers meant to catch symmetric congestion
3. `gsp_util`
   - `c1_gsp_tiered` plus a light signature-utility filter that only engages under shared pressure

Two negative exploratory branches were also informative:

- `c2_mc_prefprio`
  - demand-over-prefetch scheduling at the DRAM controller
- `c3_sigutil`
  - an earlier, more aggressive signature utility filter that penalized too early

Those two side branches were useful as diagnosis, but they are not the direction to keep.

## 3. Verified Single-Core Results

### 3.1 `mcf`

| Policy | IPC | Delta vs `orig` | PF Issued | DBUS | Demand Stall |
|--------|-----|-----------------|-----------|------|--------------|
| `orig` | `0.09305` | baseline | `516,895` | `7.552` | `51,530,306` |
| `bwc` | `0.09048` | `-2.76%` | `131,593` | `8.078` | `53,039,075` |
| `c1_gsp_tiered` | `0.09048` | `-2.76%` | `131,593` | `8.078` | `53,039,075` |
| `gsp_util` | `0.09305` | `0.00%` | `516,895` | `7.552` | `51,530,306` |

Quantitatively, the `mcf` bottleneck is now very clear:

- `bwc` / `c1_gsp_tiered` cut prefetch volume by `74.5%`
- DRAM `RQ row-buffer hit` falls from `846` to `501` (`-40.8%`)
- `Demand Stall Cycles` increase by `2.93%`
- `AVG DBUS CONGESTED CYCLE` increases by `6.97%`
- `avg_epoch_mshr_util` stays at only `0.0376`
- `max_epoch_mshr_util` is only `0.46875`

So the controller is not reacting to sustained queue pressure. It is reacting mostly to the low-accuracy escape hatch, and that destroys a small but real row-buffer / latency benefit that `mcf` was getting from otherwise inaccurate prefetches.

`gsp_util` is important here even though it does not improve IPC. On single-core `mcf`, it does the right thing by **not interfering**:

- `issue_period=1`
- `enabled_epochs=0`
- `filtered_prefetches=0`

That is a meaningful correction over plain `bwc`.

### 3.2 `astar`

| Policy | IPC | Delta vs `orig` | PF Issued | DBUS | Demand Stall |
|--------|-----|-----------------|-----------|------|--------------|
| `orig` | `0.1161` | baseline | `287,628` | `9.038` | `40,892,633` |
| `c1_gsp_tiered` | `0.1146` | `-1.29%` | `81,728` | `9.308` | `41,465,125` |
| `gsp_util` | `0.1161` | `0.00%` | `287,628` | `9.038` | `40,892,633` |

`astar` confirms the same lesson more gently:

- pure throttling still loses performance,
- but the utility-gated version can avoid that loss.

That means `astar` really is the useful midpoint we hoped it would be. It is fragile enough to punish utility-blind throttling, but not as degenerate as `mcf`.

### 3.3 `lbm`

| Policy | IPC | Delta vs `orig` |
|--------|-----|-----------------|
| `orig` | `0.8684` | baseline |
| `c1_gsp_tiered` | `0.8684` | `0.00%` |
| `gsp_util` | `0.8684` | `0.00%` |

Single-core `lbm` is effectively unchanged by the new logic in the short fair reruns.

## 4. Verified 2-Core Results

### 4.1 `mcf + lbm`

| Policy | `mcf` IPC | `lbm` IPC | WS | Delta vs `orig` |
|--------|-----------|-----------|----|-----------------|
| `orig` | `0.03333` | `0.8163` | `1.29820` | baseline |
| `bwc` | `0.03181` | `0.8192` | `1.28520` | `-1.00%` |
| `c1_gsp_tiered` | `0.03181` | `0.8192` | `1.28520` | `-1.00%` |
| `gsp_util` | `0.03173` | `0.8168` | `1.28158` | `-1.28%` |

This mix exposes the central failure mode of the current controller family:

- `lbm` is preserved or slightly helped
- `mcf` is still the core that gets sacrificed
- weighted speedup therefore goes down

`c1_gsp_tiered` is identical to `bwc` here because the shared-pressure mode never actually activates in the 2-core heterogeneous case.

`gsp_util` also fails to improve WS:

- it filters `334,744` prefetches on the `mcf` side,
- only `10,524` on the `lbm` side,
- and ends at `cpu0=mcf -> level 1 / issue_period 4`, `cpu1=lbm -> level 3 / issue_period 1`.

That is still the wrong tradeoff.

### 4.2 Reverse-designed `mcf + astar`

| Policy | `mcf` IPC | `astar` IPC | WS | Delta vs `orig` |
|--------|-----------|-------------|----|-----------------|
| `orig` | `0.08736` | `0.1014` | `1.81224` | baseline |
| `gsp_util` | `0.08472` | `0.1025` | `1.79334` | `-1.04%` |

This was the first reverse-designed mix because it looked like the best chance to create a “trim the noisy core, protect the fragile core” case.

What actually happened:

- `mcf` loses about `3.0%` IPC
- `astar` gains about `1.1%` IPC
- net weighted speedup still falls by `1.04%`

So even on a deliberately chosen mix, the controller still sacrifices the fragile latency-bound core.

No verified reverse-designed positive WS case has been found yet.

## 5. What The Bottleneck Actually Is

The fair reruns reduce the project diagnosis to three clear statements.

### 5.1 `mcf` is a latency-protection problem, not an average-pressure problem

What hurts `mcf` is not high average queue occupancy. It is the loss of accidental latency help:

- row-buffer warm-up,
- miss overlap,
- and other timing benefits that are not captured by prefetch accuracy.

This is the clearest reason the current BWC family fails.

### 5.2 `astar` shows that utility is the right abstraction

`astar` is exactly the kind of workload where confidence and accuracy are not enough:

- there is some genuine prefetch value,
- but over-throttling still loses IPC,
- and the only controller that avoids that loss is the one that gates intervention on shared pressure.

### 5.3 The current multicore logic still chooses the wrong victim

Across both informative 2-core mixes:

- `mcf+lbm`
- `mcf+astar`

the current controller family still protects the less fragile core and throttles the more fragile one. That is why weighted speedup remains negative even when one component IPC improves.

## 6. Reflection

There are two important things we got right and two important things we got wrong.

What the current work got right:

- the project correctly identified that confidence / accuracy alone are not the right control signals
- `astar` was a useful workload addition and made the utility problem much easier to see

What the current work got wrong:

- we initially over-trusted provisional positive short-run `gsp_*` results before fixing config drift
- we assumed that “shared pressure + lighter utility filter” would be enough by itself to move WS, but the current actuator still sacrifices the wrong core

So the current contribution is strongest as a **bottleneck diagnosis**:

- `mcf` needs latency protection, not blunt traffic suppression
- `4x lbm` needs shared-pressure sensing plus a more selective actuator
- reverse-designed mixes still need a controller that can explicitly avoid wrong-core sacrifice

## 7. Next Design Implication

The next step should not be “throttle harder.”

It should be a more selective intervention that can:

- preserve the row-buffer / timeliness help that fragile workloads still get,
- prune only traffic with genuinely low utility under shared pressure,
- and avoid treating the most latency-sensitive core as the default victim.

That makes the most promising next mechanisms:

1. gentler utility discrimination before issue
2. a narrower shared-resource intervention that only deprioritizes clearly negative-utility prefetch traffic
3. a controller that reasons about system outcome rather than only local per-core state

The main project risk is no longer “we do not know why WS is flat.” We now know why. The remaining challenge is turning that diagnosis into a controller that can act selectively enough to create a real WS gain.
