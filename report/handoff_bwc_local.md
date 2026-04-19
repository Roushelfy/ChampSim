# `bwc_local` Handoff Note

## Files

- `prefetcher/spp_bwc/spp_bwc.cc`
- `prefetcher/spp_bwc/spp_bwc.h`

## What These Local Changes Are

### 1. GHR invalid-slot fix

`GLOBAL_REGISTER::update_entry()` now prefers filling an invalid entry before
evicting a valid low-confidence entry. This should be treated as a real
correctness and safety fix, not as an experimental controller change.

### 2. Unified `bwc_local` stats output

`spp_bwc` final stats now emit `variant_tag`, `level`, `issue_period`, and
pressure-summary style fields so the existing parser and multicore metrics
scripts can consume `bwc_local` logs in the same format as the newer
controller family.

### 3. Dormant symmetric-saturation scaffold

The code includes `congested_epochs` / `relaxed_epochs` tracking and a
symmetric-saturation path, but it is intentionally disabled by default through
`BWC_ENABLE_SYMMETRIC_SATURATION = false`.

This path should not be treated as active `bwc_local` baseline behavior unless
it is explicitly enabled for a named experiment or ablation.

## How To Interpret `bwc_local`

- `bwc_local` is the hand-designed local baseline.
- `gsp_tiered_seed` is the OpenEvolve seed family.
- These two semantics should remain separate.
- The symmetric path should not be reported as an active baseline feature.

## What Reports Should Continue To Say

- `bwc_local` includes the GHR invalid-slot fix.
- `bwc_local` includes unified logging for parser compatibility.
- Symmetric saturation exists only as disabled scaffold, not as a main result.
- OpenEvolve mainline should continue to evolve `spp_gsp_tiered`, not
  `spp_bwc`.

## Practical Guidance For The Next Owner

- If you rerun `bwc_local` experiments, keep the current default-off setting.
- If you test the symmetric path, treat it as a separate variant or ablation.
- Any result labeled `bwc_local` should correspond to this preserved baseline
  semantics.
