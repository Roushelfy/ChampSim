#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

SCREEN_DIR="results/logs/phase15/screening"
MULTI_DIR="results/logs/phase15/multicore"
MANIFEST="results/logs/phase15/manifest.csv"
SCREEN_CSV="results/phase15_single_core_screening.csv"
SELECTION_JSON="results/phase15_selection.json"
MULTI_CSV="results/phase15_multicore_metrics.csv"

mkdir -p "$SCREEN_DIR" "$MULTI_DIR"
printf "run_name,policy,scenario,setting,log,workloads\n" > "$MANIFEST"

run_and_log() {
  local binary="$1"
  local warmup="$2"
  local sim="$3"
  local log_path="$4"
  shift 4

  if [[ -s "$log_path" ]] && grep -q "CPU 0 cumulative IPC" "$log_path"; then
    echo "[skip] $log_path already looks complete"
    return
  fi

  echo "[run] $binary -> $log_path"
  "./bin/$binary" --warmup-instructions "$warmup" --simulation-instructions "$sim" "$@" > "$log_path" 2>&1
}

append_manifest() {
  local run_name="$1"
  local policy="$2"
  local scenario="$3"
  local setting="$4"
  local log_path="$5"
  local workloads="$6"
  printf "%s,%s,%s,%s,%s,%s\n" \
    "$run_name" "$policy" "$scenario" "$setting" "$log_path" "$workloads" >> "$MANIFEST"
}

python_json() {
  local expr="$1"
  python3 - <<PY
import json
with open("$SELECTION_JSON") as f:
    data = json.load(f)
print($expr)
PY
}

python_csv_expr() {
  local expr="$1"
  python3 - <<PY
import csv
rows = list(csv.DictReader(open("$MULTI_CSV")))
print($expr)
PY
}

./scripts/download_phase15_traces.sh

screen_traces=(
  "omnetpp traces/471.omnetpp-188B.champsimtrace.xz"
  "gemsfdtd traces/459.GemsFDTD-765B.champsimtrace.xz"
  "leslie3d traces/437.leslie3d-134B.champsimtrace.xz"
)

step=1
for entry in "${screen_traces[@]}"; do
  workload="${entry%% *}"
  trace="${entry#* }"
  run_and_log "champsim_no_pref" 10000000 30000000 "$SCREEN_DIR/no_pref_${workload}_30M.txt" "$trace"
  run_and_log "champsim_orig" 10000000 30000000 "$SCREEN_DIR/orig_${workload}_30M.txt" "$trace"
  step=$((step + 1))
done

python3 scripts/screen_phase15_candidates.py \
  --existing-csv results/single_core_metrics.csv \
  --input-dir "$SCREEN_DIR" \
  --out-csv "$SCREEN_CSV" \
  --out-json "$SELECTION_JSON"

POLLUTER="$(python_json 'data["selected_polluter"]')"
VICTIM="$(python_json 'data["selected_victim"]')"
VICTIMS_4CORE="$(python_json '";".join(data["selected_victims_4core"])')"
IFS=';' read -r VICTIM_A VICTIM_B VICTIM_C <<< "$VICTIMS_4CORE"

declare -A TRACE_PATHS=(
  [mcf]="traces/429.mcf-51B.champsimtrace.xz"
  [lbm]="traces/470.lbm-1274B.champsimtrace.xz"
  [omnetpp]="traces/471.omnetpp-188B.champsimtrace.xz"
  [gemsfdtd]="traces/459.GemsFDTD-765B.champsimtrace.xz"
  [leslie3d]="traces/437.leslie3d-134B.champsimtrace.xz"
)

MIX2="${POLLUTER}_${VICTIM}"
MIX4="${POLLUTER}_${VICTIM_A}_${VICTIM_B}_${VICTIM_C}"

for policy in orig fdp bwc; do
  binary="champsim_${policy}_2core"
  log="$MULTI_DIR/${policy}_2core_nominal_${MIX2}.txt"
  run_and_log "$binary" 10000000 50000000 "$log" \
    "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM]}"
  append_manifest "2core_main" "$policy" "heterogeneous" "nominal" "$log" "${POLLUTER};${VICTIM}"
done

for policy in orig bwc; do
  binary="champsim_${policy}_4core"
  log="$MULTI_DIR/${policy}_4core_nominal_${MIX4}.txt"
  run_and_log "$binary" 10000000 50000000 "$log" \
    "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"
  append_manifest "4core_main_nominal" "$policy" "main_mix" "nominal" "$log" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}"
done

python3 config.sh configs/orig_4core_stress.json >/tmp/phase15_stress_orig_config.log
make -j"$(nproc)" bin/champsim_orig_4core_stress >/tmp/phase15_stress_orig_make.log

python3 config.sh configs/fdp_4core_stress.json >/tmp/phase15_stress_fdp_config.log
make -j"$(nproc)" bin/champsim_fdp_4core_stress >/tmp/phase15_stress_fdp_make.log

python3 config.sh configs/bwc_4core_stress.json >/tmp/phase15_stress_bwc_config.log
make -j"$(nproc)" bin/champsim_bwc_4core_stress >/tmp/phase15_stress_bwc_make.log

for policy in orig bwc; do
  binary="champsim_${policy}_4core_stress"
  log="$MULTI_DIR/${policy}_4core_stress_${MIX4}.txt"
  run_and_log "$binary" 10000000 50000000 "$log" \
    "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"
  append_manifest "4core_main_stress" "$policy" "main_mix" "stress" "$log" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}"
done

python3 scripts/extract_multicore_metrics.py \
  --manifest "$MANIFEST" \
  --single-core-csv "$SCREEN_CSV" \
  --out-csv "$MULTI_CSV"

RUN_FDP_STRESS="$(python_csv_expr '
\"yes\" if float(next(r for r in rows if r[\"run_name\"] == \"4core_main_stress\" and r[\"policy\"] == \"bwc\")[\"weighted_speedup\"]) >= float(next(r for r in rows if r[\"run_name\"] == \"4core_main_stress\" and r[\"policy\"] == \"orig\")[\"weighted_speedup\"]) * 0.995 else \"no\"
')"

if [[ "$RUN_FDP_STRESS" == "yes" ]]; then
  log="$MULTI_DIR/fdp_4core_stress_${MIX4}.txt"
  run_and_log "champsim_fdp_4core_stress" 10000000 50000000 "$log" \
    "${TRACE_PATHS[$POLLUTER]}" "${TRACE_PATHS[$VICTIM_A]}" "${TRACE_PATHS[$VICTIM_B]}" "${TRACE_PATHS[$VICTIM_C]}"
  append_manifest "4core_main_stress" "fdp" "main_mix" "stress" "$log" "${POLLUTER};${VICTIM_A};${VICTIM_B};${VICTIM_C}"

  python3 scripts/extract_multicore_metrics.py \
    --manifest "$MANIFEST" \
    --single-core-csv "$SCREEN_CSV" \
    --out-csv "$MULTI_CSV"
fi

echo "Phase 1.5 batch complete"
echo "Selected polluter: $POLLUTER"
echo "Selected victims: $VICTIM_A, $VICTIM_B, $VICTIM_C"
