# Single-Core Coverage Gaps

## Required matrix
- Workloads: mcf, lbm, bzip2
- Policies: no_pref, orig, fdp, bwc, bop

## Missing entries
- None

## Notes
- queue_occupancy comes from L2C 'AVERAGE UPPER RQ OCCUPANCY' (% of capacity).
- demand_stall_cycles comes from CPU 'Demand Stall Cycles'.
- If either field is NA for a row, that log was likely generated before this instrumentation was added.
- off-chip bandwidth currently uses AVG DBUS CONGESTED CYCLE as a proxy.
