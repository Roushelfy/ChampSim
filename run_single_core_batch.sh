#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

LOGDIR=results/logs/single_core
mkdir -p "$LOGDIR"

echo "[1/15] no_pref mcf"
./bin/champsim_no_pref --warmup-instructions 10000000 --simulation-instructions 50000000 traces/429.mcf-51B.champsimtrace.xz > "$LOGDIR/no_pref_mcf_50M.txt"

echo "[2/15] orig mcf"
./bin/champsim_orig --warmup-instructions 10000000 --simulation-instructions 50000000 traces/429.mcf-51B.champsimtrace.xz > "$LOGDIR/orig_mcf_50M.txt"

echo "[3/15] fdp mcf"
./bin/champsim_fdp --warmup-instructions 10000000 --simulation-instructions 50000000 traces/429.mcf-51B.champsimtrace.xz > "$LOGDIR/fdp_mcf_50M.txt"

echo "[4/15] bwc mcf"
./bin/champsim_bwc --warmup-instructions 10000000 --simulation-instructions 50000000 traces/429.mcf-51B.champsimtrace.xz > "$LOGDIR/bwc2_mcf_50M.txt"

echo "[5/15] bop mcf"
./bin/champsim_bop --warmup-instructions 10000000 --simulation-instructions 50000000 traces/429.mcf-51B.champsimtrace.xz > "$LOGDIR/bop_mcf_50M.txt"

echo "[6/15] no_pref lbm"
./bin/champsim_no_pref --warmup-instructions 10000000 --simulation-instructions 50000000 traces/470.lbm-1274B.champsimtrace.xz > "$LOGDIR/no_pref_lbm_50M.txt"

echo "[7/15] orig lbm"
./bin/champsim_orig --warmup-instructions 10000000 --simulation-instructions 50000000 traces/470.lbm-1274B.champsimtrace.xz > "$LOGDIR/orig_lbm_50M.txt"

echo "[8/15] fdp lbm"
./bin/champsim_fdp --warmup-instructions 10000000 --simulation-instructions 50000000 traces/470.lbm-1274B.champsimtrace.xz > "$LOGDIR/fdp_lbm_50M.txt"

echo "[9/15] bwc lbm"
./bin/champsim_bwc --warmup-instructions 10000000 --simulation-instructions 50000000 traces/470.lbm-1274B.champsimtrace.xz > "$LOGDIR/bwc_lbm_50M.txt"

echo "[10/15] bop lbm"
./bin/champsim_bop --warmup-instructions 10000000 --simulation-instructions 50000000 traces/470.lbm-1274B.champsimtrace.xz > "$LOGDIR/bop_lbm_50M.txt"

echo "[11/15] no_pref bzip2"
./bin/champsim_no_pref --warmup-instructions 1000000 --simulation-instructions 5000000 traces/401.bzip2-226B.champsimtrace.xz > "$LOGDIR/no_pref_bzip2_5M.txt"

echo "[12/15] orig bzip2"
./bin/champsim_orig --warmup-instructions 1000000 --simulation-instructions 5000000 traces/401.bzip2-226B.champsimtrace.xz > "$LOGDIR/orig_bzip2_5M.txt"

echo "[13/15] fdp bzip2"
./bin/champsim_fdp --warmup-instructions 1000000 --simulation-instructions 5000000 traces/401.bzip2-226B.champsimtrace.xz > "$LOGDIR/fdp_epoch10k_bzip2.txt"

echo "[14/15] bwc bzip2"
./bin/champsim_bwc --warmup-instructions 1000000 --simulation-instructions 5000000 traces/401.bzip2-226B.champsimtrace.xz > "$LOGDIR/bwc_bzip2_5M.txt"

echo "[15/15] bop bzip2"
./bin/champsim_bop --warmup-instructions 1000000 --simulation-instructions 5000000 traces/401.bzip2-226B.champsimtrace.xz > "$LOGDIR/bop_bzip2_5M.txt"

echo "all runs complete"
