# Log Organization

This folder groups text logs by purpose while preserving original files in the project root.

## single_core
Canonical single-core logs used by `results/single_core_metrics.csv`:
- `no_pref_mcf_50M.txt`
- `orig_mcf_50M.txt`
- `fdp_mcf_50M.txt`
- `bwc2_mcf_50M.txt`
- `no_pref_lbm_50M.txt`
- `orig_lbm_50M.txt`
- `fdp_lbm_50M.txt`
- `bwc_lbm_50M.txt`
- `no_pref_bzip2_5M.txt`
- `orig_bzip2_5M.txt`
- `fdp_epoch10k_bzip2.txt`
- `bwc_bzip2_5M.txt`

Source-of-truth note:
- `scripts/extract_single_core_metrics.py` reads this folder by default when no `--inputs` are provided.

## multicore
Collected multicore output logs (`2core`/`4core`).

## misc
Legacy/debug/alternate-run logs kept for reference.

Root slimming note:
- Non-canonical root `*.txt` logs were moved here to keep project root clean.
- Canonical single-core logs live in `results/logs/single_core`.
