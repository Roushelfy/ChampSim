# 4xlbm Strategy Sweep

## Purpose

This note implements the three follow-up strategy candidates from the quick diagnosis pass on the same short exploratory setup:

- workload: `4 x 470.lbm-1274B`
- window: `1M` warmup + `5M` simulation instructions
- WS denominator: quick single-core `orig` IPC from `results/quick/phase_a_single_core_metrics.csv`

These results are meant to explain behavior and rank strategy directions quickly. They do **not** replace the longer Phase 1 results in `phase1.md`.

The baseline rows reused from the earlier quick pass are:

- `orig`
- `fdp`
- `bwc`
- `bwc_gsp v0`

The three implemented candidates in this sweep are:

1. `c1_gsp_tiered`: tiered shared-resource-aware throttling
2. `c2_mc_prefprio`: memory-controller demand-over-prefetch scheduling
3. `c3_sigutil`: signature utility filtering

The final repo state keeps **candidate 1** only. After removing the loser implementations, I reran candidate 1 and reproduced the same metrics (`WS=1.065147`, `DBUS=4.871`, `row-hit=0.05107`) on the final code state.

## Unified Comparison

| Policy | WS | Delta vs orig | Delta vs bwc | PF issued | PF useful | DBUS | RQ row-hit | Key signal |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `orig` | 1.063191 | +0.00% | -0.27% | 1,454,031 | 75,406 | 4.468 | 0.0477 | `SPP_ORIG`, no throttling |
| `fdp` | 1.057781 | -0.51% | -0.78% | 226,961 | 51,644 | 5.807 | 0.0561 | all cores ended at `level=1`, `pf_threshold=80` |
| `bwc` | 1.066068 | +0.27% | +0.00% | 1,453,328 | 75,676 | 4.462 | 0.0474 | all cores stayed `L3`, `issue_period=1` |
| `bwc_gsp` | 1.063421 | +0.02% | -0.25% | 978,726 | 39,775 | 4.497 | 0.0484 | symmetric mode detected, but only `issue_period=2` floor |
| `c1_gsp_tiered` | 1.065147 | +0.18% | -0.09% | 595,058 | 24,132 | 4.871 | 0.0511 | tier epochs total `57 / 145 / 81` for tiers `1 / 2 / 3` |
| `c2_mc_prefprio` | 0.995166 | -6.40% | -6.65% | 1,257,269 | 60,444 | 12.920 | 0.0643 | `demand_scheduled=780,955`, `prefetch_scheduled=346,100`, `prefetch_bypassed=3,755,509` |
| `c3_sigutil` | 1.048343 | -1.40% | -1.66% | 265,977 | 25,554 | 6.145 | 0.0592 | filtered `2,492,786` prefetches, final `level=5` |

Two points stand out immediately:

- The best **new** strategy is `c1_gsp_tiered`.
- The best **overall** short-run result is still the older `bwc` baseline.

So the sweep did find a better direction than `bwc_gsp v0`, but it did **not** yet beat the original `bwc` on `4xlbm`.

## Candidate 1: Tiered Shared-Pressure Throttling

### What changed

I extended `bwc_gsp v0` into a three-tier global throttle:

- tier 1: `global_avg_mshr >= 0.30` => `issue_period >= 2`
- tier 2: `global_avg_mshr >= 0.38` => `issue_period >= 4`
- tier 3: `global_avg_mshr >= 0.44` => `issue_period >= 4` and `pf_threshold >= 60`

The original local BWC controller stayed intact; the new logic only adds a global floor/guard.

### What happened

`c1_gsp_tiered` fixed the original blind spot:

- `bwc_gsp v0` had symmetric mode, but only a weak `issue_period=2` floor.
- `c1` actually spent meaningful time in stronger tiers: total tier counts were `57 / 145 / 81`.
- PF volume fell from `978,726` in `bwc_gsp v0` to `595,058`.
- WS recovered from `1.063421` to `1.065147`.

Relative to `orig`, candidate 1:

- improved WS by `+0.18%`
- cut PF issues by `-59.1%`
- raised row-hit rate by `+7.2%`
- raised DBUS by `+9.0%`

Per-core IPC moved unevenly:

- cpu0: `-1.15%`
- cpu1: `+0.39%`
- cpu2: `+3.53%`
- cpu3: `-2.00%`

At the same time, L1D miss latency dropped from roughly `1090` cycles in `orig` to roughly `974-989` cycles here, which means the stronger global throttle did reduce some bad queueing. But it still over-throttled useful traffic on two cores.

### Verdict

This is the best of the three new ideas because it **does** solve the symmetric-pressure detection problem that `bwc` and `bwc_gsp v0` missed. But its actuator is still too coarse:

- it is global rather than utility-aware,
- it reduces PF volume aggressively,
- and it helps some cores while over-throttling others.

So candidate 1 is a good **directional winner**, not a final solution.

## Candidate 2: MC Demand-Over-Prefetch Scheduling

### What changed

I patched the DRAM controller so that:

- DRAM requests preserve `type` and `cpu`,
- in read mode, if any unscheduled demand request exists, the scheduler only considers demand requests,
- tie-breaker inside a class is `bank free > row-buffer hit > earlier ready_time`.

I also logged:

- `RQ_DEMAND_SCHEDULED`
- `RQ_PREFETCH_SCHEDULED`
- `RQ_PREFETCH_BYPASSED`

### What happened

The policy clearly activated:

- `demand_scheduled=780,955`
- `prefetch_scheduled=346,100`
- `prefetch_bypassed=3,755,509`

It even improved some local metrics:

- row-hit rate increased to `0.0643` (`+35.0%` vs `orig`)
- LLC miss latency dropped to about `3717-3774` cycles from about `7145-7420`
- L1D miss latency also fell sharply

But the global outcome was disastrous:

- WS fell to `0.995166`
- DBUS exploded to `12.920`
- every core lost IPC (`-5.0%` to `-9.0%`)

### Verdict

This strategy acts **too late** in the pipeline. By the time requests reach the MC:

- the useful and harmful prefetches are already mixed together,
- the controller is no longer shaping upstream issue behavior,
- and prioritizing demand requests disrupts the traffic pattern badly enough that bus congestion becomes the new dominant problem.

So candidate 2 shows that “just prioritize demand at the MC” is not sufficient for this workload. It improves some latency metrics while making total throughput much worse.

## Candidate 3: Signature Utility Filter

### What changed

I added a direct-mapped `512`-entry signature utility table inside the `spp_bwc` path:

- issue: signature credit `-1`
- useful hit: signature credit `+4`
- epoch end: decay each valid entry by `1` toward zero

The gate is enabled only when `global_avg_mshr_util >= 0.30`. Under pressure, signatures with negative credit are skipped.

### What happened

This logic was extremely aggressive:

- filtered prefetches: `2,492,786`
- utility-positive hits recorded: `26,417`
- PF issued dropped to `265,977` (`-81.7%` vs `orig`)
- all cores ended at `level=5`, `pf_threshold=5`, `fill_threshold=50`

The result was better than `fdp`, but still clearly below the best baselines:

- WS = `1.048343`
- DBUS = `6.145`
- row-hit = `0.0592`

Per-core IPC was mixed:

- cpu0: `-7.81%`
- cpu1: `-2.58%`
- cpu2: `+4.40%`
- cpu3: `+0.57%`

### Verdict

The idea is promising, but this first implementation is too eager under short epochs:

- signatures are penalized immediately on issue,
- usefulness feedback arrives later,
- and once pressure is high, the gate shuts off too much streaming help too early.

So candidate 3 confirms that **utility-aware filtering is the right class of idea**, but the credit model needs softer penalties, longer observation, or delayed enablement.

## Root-Cause Summary

The sweep makes the current `4xlbm` bottleneck much clearer:

1. `bwc` really does have a sensing blind spot for symmetric pressure. Candidate 1 proves that a global pressure signal can detect and react to that regime.
2. But sensing alone is not enough. Candidate 1 still uses a coarse actuator, so it cannot distinguish useful vs harmful prefetch streams.
3. Acting only at the DRAM controller is too late. Candidate 2 improved local latency and row locality but destroyed overall throughput.
4. Utility filtering is necessary, but the first signed-credit design was too aggressive on a short run. Candidate 3 cut too much help before the workload could repay it.

The one-sentence conclusion is:

> `4xlbm` needs **global pressure awareness plus gentle utility-aware filtering**, not pure confidence throttling and not MC-only demand priority.

## Winner And Repo State

### Winner among the three new candidates

`c1_gsp_tiered` is the winner of this sweep.

Why:

- it is the only new strategy that improved over `orig` and over `bwc_gsp v0`,
- it solved the original symmetric-pressure blind spot,
- and it reproduced cleanly when rerun on the final repo state.

### Why the other two lost

- `c2_mc_prefprio` lost because it moved the intervention too late and turned bus congestion into the dominant bottleneck.
- `c3_sigutil` lost because its utility gate was too aggressive and cut off too much useful streaming support.

### Final repo state

The final source tree keeps the candidate 1 implementation:

- `prefetcher/spp_bwc/*` contains the shared instrumentation base
- `prefetcher/spp_gsp_tiered/*` contains the selected policy wrapper
- the DRAM controller is back on the baseline path

This means the repo now matches the final rerun result for `c1_gsp_tiered`.

