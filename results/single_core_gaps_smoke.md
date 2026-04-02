# Single-Core Coverage Gaps

## Required matrix
- Workloads: mcf, lbm, bzip2
- Policies: no_pref, orig, fdp, bwc

## Missing entries
- mcf x no_pref
- mcf x orig
- mcf x fdp
- mcf x bwc
- lbm x no_pref
- lbm x orig
- lbm x fdp
- lbm x bwc
- bzip2 x orig
- bzip2 x fdp
- bzip2 x bwc

## Notes
- queue_occupancy and demand_stall_cycles are currently marked NA; additional instrumentation is required.
- off-chip bandwidth currently uses AVG DBUS CONGESTED CYCLE as a proxy.
