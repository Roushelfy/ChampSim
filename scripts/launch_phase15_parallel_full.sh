#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

WARMUP=10000000
SIM=50000000

POLLUTER="mcf"
VICTIM_2CORE="gemsfdtd"
VICTIM_A="gemsfdtd"
VICTIM_B="leslie3d"
VICTIM_C="lbm"

declare -A TRACE_PATHS=(
  [mcf]="traces/429.mcf-51B.champsimtrace.xz"
  [lbm]="traces/470.lbm-1274B.champsimtrace.xz"
  [gemsfdtd]="traces/459.GemsFDTD-765B.champsimtrace.xz"
  [leslie3d]="traces/437.leslie3d-134B.champsimtrace.xz"
)

LAUNCH_ID="${1:-$(date +%Y%m%d_%H%M%S)}"
LAUNCH_DIR="results/logs/phase15/launches/${LAUNCH_ID}"
MULTI_DIR="results/logs/phase15/multicore"
JOBS_CSV="${LAUNCH_DIR}/jobs.csv"
MANIFEST_CSV="${LAUNCH_DIR}/manifest.csv"

mkdir -p "${LAUNCH_DIR}" "${MULTI_DIR}"

printf "job_name,pid,policy,binary,scenario,setting,log,workloads\n" > "${JOBS_CSV}"
printf "run_name,policy,scenario,setting,log,workloads\n" > "${MANIFEST_CSV}"

launch_job() {
  local job_name="$1"
  local run_name="$2"
  local policy="$3"
  local binary="$4"
  local scenario="$5"
  local setting="$6"
  local log_path="$7"
  local workloads="$8"
  shift 8

  setsid bash -c '
    root_dir="$1"
    binary_name="$2"
    log_file="$3"
    warmup_count="$4"
    sim_count="$5"
    shift 5
    cd "$root_dir"
    exec "./bin/$binary_name" \
      --warmup-instructions "$warmup_count" \
      --simulation-instructions "$sim_count" \
      "$@" >"$log_file" 2>&1
  ' bash "$PWD" "$binary" "$log_path" "$WARMUP" "$SIM" "$@" >/dev/null 2>&1 < /dev/null &
  local pid=$!

  printf "%s,%s,%s,%s,%s,%s,%s,%s\n" \
    "${job_name}" "${pid}" "${policy}" "${binary}" "${scenario}" "${setting}" "${log_path}" "${workloads}" >> "${JOBS_CSV}"
  printf "%s,%s,%s,%s,%s,%s\n" \
    "${run_name}" "${policy}" "${scenario}" "${setting}" "${log_path}" "${workloads}" >> "${MANIFEST_CSV}"

  echo "[launched] ${job_name} pid=${pid} log=${log_path}"
}

launch_job \
  "2core_orig" "2core_main" "orig" "champsim_orig_2core" "heterogeneous" "nominal" \
  "${MULTI_DIR}/orig_2core_nominal_${POLLUTER}_${VICTIM_2CORE}.txt" "${POLLUTER};${VICTIM_2CORE}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_2CORE]}"

launch_job \
  "2core_fdp" "2core_main" "fdp" "champsim_fdp_2core" "heterogeneous" "nominal" \
  "${MULTI_DIR}/fdp_2core_nominal_${POLLUTER}_${VICTIM_2CORE}.txt" "${POLLUTER};${VICTIM_2CORE}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_2CORE]}"

launch_job \
  "2core_bwc" "2core_main" "bwc" "champsim_bwc_2core" "heterogeneous" "nominal" \
  "${MULTI_DIR}/bwc_2core_nominal_${POLLUTER}_${VICTIM_2CORE}.txt" "${POLLUTER};${VICTIM_2CORE}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_2CORE]}"

launch_job \
  "4core_nominal_orig" "4core_main_nominal" "orig" "champsim_orig_4core" "main_mix" "nominal" \
  "${MULTI_DIR}/orig_4core_nominal_${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}.txt" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"

launch_job \
  "4core_nominal_fdp" "4core_main_nominal" "fdp" "champsim_fdp_4core" "main_mix" "nominal" \
  "${MULTI_DIR}/fdp_4core_nominal_${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}.txt" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"

launch_job \
  "4core_nominal_bwc" "4core_main_nominal" "bwc" "champsim_bwc_4core" "main_mix" "nominal" \
  "${MULTI_DIR}/bwc_4core_nominal_${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}.txt" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"

launch_job \
  "4core_stress_orig" "4core_main_stress" "orig" "champsim_orig_4core_stress" "main_mix" "stress" \
  "${MULTI_DIR}/orig_4core_stress_${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}.txt" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"

launch_job \
  "4core_stress_fdp" "4core_main_stress" "fdp" "champsim_fdp_4core_stress" "main_mix" "stress" \
  "${MULTI_DIR}/fdp_4core_stress_${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}.txt" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"

launch_job \
  "4core_stress_bwc" "4core_main_stress" "bwc" "champsim_bwc_4core_stress" "main_mix" "stress" \
  "${MULTI_DIR}/bwc_4core_stress_${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}.txt" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}" \
  "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"

echo
echo "Launch manifest: ${MANIFEST_CSV}"
echo "Launch jobs: ${JOBS_CSV}"
