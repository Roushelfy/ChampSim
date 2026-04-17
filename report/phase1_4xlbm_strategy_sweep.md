# 4xlbm Strategy Sweep

## Purpose

This note records what we learned from the `4 x lbm` strategy search after the fair reruns were complete.

The point is not to archive every temporary script or intermediate log. It is to keep the durable conclusions:

- which algorithms were tried,
- which ones were worth keeping,
- which bottleneck they exposed,
- and what we should change next.

## 1. The Algorithms We Tried

The `4 x lbm` case was used to probe one specific question:

> Can the controller detect and respond to symmetric shared pressure without destroying useful streaming help?

The main candidates were:

1. `bwc`
   - original local bandwidth-aware controller
2. `bwc_gsp v0`
   - first global-symmetric-pressure issue floor
3. `c1_gsp_tiered`
   - stronger tiered shared-pressure throttle
4. `gsp_util`
   - `c1_gsp_tiered` plus a pressure-gated utility filter
5. `c2_mc_prefprio`
   - demand-over-prefetch scheduling in the DRAM controller
6. `c3_sigutil`
   - an earlier, aggressive signature-utility gate

Only `c1_gsp_tiered` and `gsp_util` remained plausible after the first sweep. The others were still useful because they clarified what kind of intervention does not work.

## 2. Fair Rerun Summary

After fixing the config-drift issue in the early `gsp_*` comparisons, the fair short-window `4 x lbm` results are:

| Policy | WS | Delta vs `orig` | PF Issued | DBUS | Row-Hit Rate |
| --- | ---: | ---: | ---: | ---: | ---: |
| `orig` | `1.066559` | baseline | `1,453,328` | `4.462` | `0.04742` |
| `bwc` | `1.066559` | `0.00%` | `1,453,328` | `4.462` | `0.04742` |
| `c1_gsp_tiered` | `1.064832` | `-0.16%` | `529,692` | `4.666` | `0.04896` |
| `gsp_util` | `1.064486` | `-0.19%` | `573,888` | `4.635` | `0.04963` |

Two takeaways dominate:

- plain `bwc` is still effectively a no-op on `4 x lbm`
- the newer shared-pressure controllers finally activate, but they still do not produce a WS gain

So the `4 x lbm` problem has moved from a sensing failure to an actuation failure.

## 3. What Each Algorithm Taught Us

### 3.1 `bwc`: the original blind spot is real

In the fair rerun, `bwc` matches `orig` almost exactly:

- same WS
- same PF volume
- same DBUS
- same row-hit rate

The pressure statistics explain why:

- average epoch MSHR utilization is only about `0.365`
- max epoch MSHR utilization is `0.5`
- average epoch LLC RQ utilization is about `0.0035`
- max epoch LLC RQ utilization is `0.234375`
- all threshold-crossing fractions stay at `0`

So the original local BWC thresholds are simply too high to classify the symmetric `4 x lbm` regime as “pressure.”

### 3.2 `bwc_gsp v0`: the sensor direction was right, but too weak

The first global symmetric-pressure version was useful because it proved the blind spot was not imaginary.

It detected the regime, but the actuator was too weak:

- symmetric mode engaged
- the issue floor only moved to `issue_period=2`
- that was not enough to create a durable win

This was the point where it became clear that `4 x lbm` needed more than one additional if-statement.

### 3.3 `c1_gsp_tiered`: better sensing, still a coarse actuator

`c1_gsp_tiered` is the cleanest proof that the sensing problem is now mostly fixed.

In the fair rerun:

- all four cores finish with `issue_period=4`
- `symmetric_mode_epochs` is about `69-72` per core
- average epoch MSHR utilization is about `0.352-0.355`
- PF volume falls by `63.6%`

But the outcome is still negative:

- WS = `1.064832`
- delta vs `orig` = `-0.16%`

So `c1_gsp_tiered` solved the wrong half of the problem. It can tell that the machine is under shared pressure, but it still acts too bluntly once that happens.

### 3.4 `gsp_util`: utility filtering is the right class of idea, but still too weak

`gsp_util` was meant to keep the shared-pressure detection from `c1_gsp_tiered` while preserving more useful streams.

It does engage the utility path:

- `enabled_epochs` is about `82-86` per core
- filtered prefetches are nonzero

But the magnitude is too small to dominate behavior:

- filtered prefetches are only `0`, `447`, `87`, and `146`
- PF volume still changes mostly because of the global `issue_period=4` floor

The result:

- WS = `1.064486`
- delta vs `orig` = `-0.19%`

This is the key reflection from the current rerun on `4 x lbm`:

> shared-pressure awareness is now present, but the current utility filter is too weak to decide which streams should survive.

### 3.5 `c2_mc_prefprio`: acting only at the memory controller is too late

This branch was valuable because it showed that not every “demand-friendly” policy is actually good for throughput.

The memory-controller prioritization idea:

- improved some local latency and row-buffer metrics,
- but it degraded overall throughput badly,
- because by the time requests reached the MC, the useful and harmful prefetches were already mixed together.

So this intervention point was too late in the pipeline.

### 3.6 `c3_sigutil`: utility filtering can also overreact

The first signature-utility filter failed for the opposite reason:

- it penalized too aggressively,
- usefulness feedback arrived too late,
- and streaming help was cut off before the workload could benefit.

This was still a useful failure because it showed that “utility-aware” is not enough by itself. The credit model and enable conditions matter a lot.

## 4. The Observed Bottleneck

The durable `4 x lbm` bottleneck is now:

1. local BWC thresholds miss symmetric saturation
2. once shared pressure is finally detected, the current global throttle is too coarse
3. the current utility filter is too weak to preserve only the good streams

In one sentence:

> `4 x lbm` needs global pressure awareness and a selective actuator at the same time; either one alone is not enough.

This is why the fair reruns look the way they do:

- `bwc` sees nothing and does nothing
- `c1_gsp_tiered` sees the pressure but over-throttles indiscriminately
- `gsp_util` is directionally better as a design idea, but still behaves mostly like a coarse global throttle

## 5. Reflection

The early exploratory sweep made it look as if `c1_gsp_tiered` or `gsp_util` might already be slightly positive on `4 x lbm`.

The fair reruns corrected that:

- those small positive deltas did not survive the config-drift fix
- the real result is slightly negative for both controllers

That correction matters because it changes the project story in a healthy way:

- the win is not “we already fixed `4 x lbm`”
- the win is “we now know exactly why the old controller fails and why the first fix was not selective enough”

So the most honest reading of the current `4 x lbm` work is:

- diagnosis success
- implementation progress
- but not yet a performance success

## 6. What To Change Next

The next controller should preserve the part that is now working:

- shared-pressure detection

but replace the part that is still too crude:

- a one-size-fits-all issue floor

That means the next useful design step is not another threshold tweak. It is a controller that can make finer utility distinctions under pressure, for example by:

- delaying penalties until usefulness has enough time to be observed
- targeting only clearly negative-utility prefetch traffic
- or adding a narrower shared-resource intervention instead of a uniform global slowdown

The `4 x lbm` case no longer looks mysterious. It looks like an actuator-design problem.
