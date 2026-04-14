#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLOBAL_RESULTS_DIR="$ROOT_DIR/global_results"
MERGED_PDF="$GLOBAL_RESULTS_DIR/benchmark_global_report.pdf"
MASTER_JSON="$GLOBAL_RESULTS_DIR/master_summary.json"
MASTER_CSV="$GLOBAL_RESULTS_DIR/master_summary.csv"
SYSTEM_INFO_TXT="$GLOBAL_RESULTS_DIR/system_info.txt"
RUN_LOG="$GLOBAL_RESULTS_DIR/run_log.txt"
RUN_MANIFEST_JSON="$GLOBAL_RESULTS_DIR/run_manifest.json"

PROJECT_DIRS=(
  "asio-standalone"
  "bechmark_taps"
  "becnhmark_async_berkeley"
  "boost-asio"
  "bsd-sockets"
  "corosio"
)

SETTLE_SECONDS_BEFORE="${SETTLE_SECONDS_BEFORE:-20}"
SETTLE_SECONDS_AFTER="${SETTLE_SECONDS_AFTER:-20}"
WARMUP_ENABLED="${WARMUP_ENABLED:-1}"
WARMUP_PROJECTS="${WARMUP_PROJECTS:-1}"
CACHE_TRASH_ENABLED="${CACHE_TRASH_ENABLED:-1}"
CACHE_TRASH_SIZE_MB="${CACHE_TRASH_SIZE_MB:-2048}"
RANDOMIZE_ORDER="${RANDOMIZE_ORDER:-0}"
MERGE_PDFS_AT_END="${MERGE_PDFS_AT_END:-1}"
MEASURE_IDLE_AT_START="${MEASURE_IDLE_AT_START:-1}"

log() {
  local msg="[$(date '+%H:%M:%S')] $*"
  echo "$msg"
  echo "$msg" >> "$RUN_LOG"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

prepare_dirs() {
  mkdir -p "$GLOBAL_RESULTS_DIR"
  : > "$RUN_LOG"
}

save_manifest() {
  cat > "$RUN_MANIFEST_JSON" <<EOF
{
  "root_dir": "$ROOT_DIR",
  "global_results_dir": "$GLOBAL_RESULTS_DIR",
  "merged_pdf": "$MERGED_PDF",
  "master_json": "$MASTER_JSON",
  "master_csv": "$MASTER_CSV",
  "system_info_txt": "$SYSTEM_INFO_TXT",
  "run_log": "$RUN_LOG",
  "settle_seconds_before": $SETTLE_SECONDS_BEFORE,
  "settle_seconds_after": $SETTLE_SECONDS_AFTER,
  "warmup_enabled": $WARMUP_ENABLED,
  "warmup_projects": $WARMUP_PROJECTS,
  "cache_trash_enabled": $CACHE_TRASH_ENABLED,
  "cache_trash_size_mb": $CACHE_TRASH_SIZE_MB,
  "randomize_order": $RANDOMIZE_ORDER,
  "merge_pdfs_at_end": $MERGE_PDFS_AT_END,
  "measure_idle_at_start": $MEASURE_IDLE_AT_START
}
EOF
}

save_system_info() {
  {
    echo "==== BENCHMARK SYSTEM INFO ===="
    echo "Timestamp: $(date '+%Y-%m-%d %H:%M:%S')"
    echo
    echo "==== uname -a ===="
    uname -a || true
    echo
    echo "==== hostnamectl ===="
    hostnamectl || true
    echo
    echo "==== lscpu ===="
    lscpu || true
    echo
    echo "==== free -h ===="
    free -h || true
    echo
    echo "==== lsblk ===="
    lsblk || true
    echo
    echo "==== governors ===="
    grep . /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null || true
    echo
    echo "==== /proc/cmdline ===="
    cat /proc/cmdline || true
    echo
    echo "==== env ===="
    env | sort
  } > "$SYSTEM_INFO_TXT"
}

measure_idle_if_available() {
  if [ "$MEASURE_IDLE_AT_START" != "1" ]; then
    log "Medicion idle desactivada"
    return
  fi

  local idle_script="$ROOT_DIR/measure_idle_energy.py"

  if [ ! -f "$idle_script" ]; then
    log "No existe measure_idle_energy.py en la raiz; se continua sin nueva medicion idle"
    return
  fi

  log "Midiendo baseline idle del sistema..."
  python3 "$idle_script"
}

trash_caches_best_effort() {
  if [ "$CACHE_TRASH_ENABLED" != "1" ]; then
    return
  fi

  log "Cache trashing best-effort (${CACHE_TRASH_SIZE_MB} MB)..."

  python3 - <<PY
size_mb = int("${CACHE_TRASH_SIZE_MB}")
chunk = 1024 * 1024
buf = bytearray(chunk)
acc = 0

for i in range(size_mb):
    for j in range(0, len(buf), 4096):
        buf[j] = (i + j) & 0xFF
        acc ^= buf[j]

print(acc)
PY

  if [ -w /proc/sys/vm/drop_caches ]; then
    log "Dropping page cache (best-effort)"
    sync || true
    echo 3 > /proc/sys/vm/drop_caches || true
  else
    log "No se puede escribir en /proc/sys/vm/drop_caches; continuando"
  fi
}

settle_before() {
  log "Cooldown BEFORE next project: ${SETTLE_SECONDS_BEFORE}s"
  sleep "$SETTLE_SECONDS_BEFORE"
}

settle_after() {
  log "Cooldown AFTER project: ${SETTLE_SECONDS_AFTER}s"
  sleep "$SETTLE_SECONDS_AFTER"
}

copy_project_artifacts_if_exist() {
  local project_dir="$1"

  local pdf_path="$ROOT_DIR/$project_dir/results/macro_bench_report.pdf"
  local summary_json="$ROOT_DIR/$project_dir/results/macro_bench_summary.json"
  local raw_json="$ROOT_DIR/$project_dir/results/macro_bench_results.json"
  local csv_path="$ROOT_DIR/$project_dir/results/macro_bench_results.csv"

  if [ -f "$pdf_path" ]; then
    cp "$pdf_path" "$GLOBAL_RESULTS_DIR/${project_dir}_report.pdf"
    log "PDF copiado: $GLOBAL_RESULTS_DIR/${project_dir}_report.pdf"
  fi

  if [ -f "$summary_json" ]; then
    cp "$summary_json" "$GLOBAL_RESULTS_DIR/${project_dir}_summary.json"
    log "Summary JSON copiado: $GLOBAL_RESULTS_DIR/${project_dir}_summary.json"
  fi

  if [ -f "$raw_json" ]; then
    cp "$raw_json" "$GLOBAL_RESULTS_DIR/${project_dir}_raw.json"
    log "Raw JSON copiado: $GLOBAL_RESULTS_DIR/${project_dir}_raw.json"
  fi

  if [ -f "$csv_path" ]; then
    cp "$csv_path" "$GLOBAL_RESULTS_DIR/${project_dir}_raw.csv"
    log "CSV copiado: $GLOBAL_RESULTS_DIR/${project_dir}_raw.csv"
  fi
}

run_project_once() {
  local project_dir="$1"
  local full_dir="$ROOT_DIR/$project_dir"
  local run_script="$full_dir/scripts/run_bench.py"

  if [ ! -d "$full_dir" ]; then
    log "Saltando $project_dir: directorio no encontrado"
    return
  fi

  if [ ! -f "$run_script" ]; then
    log "Saltando $project_dir: scripts/run_bench.py no encontrado"
    return
  fi

  trash_caches_best_effort
  settle_before

  log "Ejecutando benchmarks de $project_dir"
  (
    cd "$full_dir"
    python3 scripts/run_bench.py
  )

  copy_project_artifacts_if_exist "$project_dir"
  settle_after
}

warmup_phase() {
  if [ "$WARMUP_ENABLED" != "1" ]; then
    log "Warmup desactivado"
    return
  fi

  log "Fase warmup activada"

  local count=0
  for project_dir in "${PROJECT_DIRS[@]}"; do
    if [ "$count" -ge "$WARMUP_PROJECTS" ]; then
      break
    fi

    local full_dir="$ROOT_DIR/$project_dir"
    local run_script="$full_dir/scripts/run_bench.py"

    if [ -d "$full_dir" ] && [ -f "$run_script" ]; then
      log "Warmup con $project_dir"
      (
        cd "$full_dir"
        python3 scripts/run_bench.py || true
      )
      count=$((count + 1))
      settle_after
    fi
  done
}

build_master_tables() {
  local script="$ROOT_DIR/scripts/build_master_summary.py"

  if [ ! -f "$script" ]; then
    log "No existe $script; no se genera tabla maestra"
    return
  fi

  log "Generando tabla maestra comparativa..."
  python3 "$script" \
    --input-dir "$GLOBAL_RESULTS_DIR" \
    --json-out "$MASTER_JSON" \
    --csv-out "$MASTER_CSV"
}

merge_reports() {
  if [ "$MERGE_PDFS_AT_END" != "1" ]; then
    log "Fusion de PDFs desactivada"
    return
  fi

  local script="$ROOT_DIR/scripts/merge_reports.py"

  if [ ! -f "$script" ]; then
    log "No existe $script; no se fusionan PDFs"
    return
  fi

  log "Fusionando PDFs finales..."
  python3 "$script" \
    --input-dir "$GLOBAL_RESULTS_DIR" \
    --output "$MERGED_PDF"
}

main() {
  prepare_dirs
  save_manifest
  save_system_info

  log "Raiz del repositorio: $ROOT_DIR"
  log "Resultados globales: $GLOBAL_RESULTS_DIR"

  if ! need_cmd python3; then
    echo "Error: python3 no esta disponible"
    exit 1
  fi

  measure_idle_if_available

  local projects=("${PROJECT_DIRS[@]}")

  if [ "$RANDOMIZE_ORDER" = "1" ]; then
    if need_cmd shuf; then
      mapfile -t projects < <(printf '%s\n' "${PROJECT_DIRS[@]}" | shuf)
      log "Orden aleatorio activado: ${projects[*]}"
    else
      log "shuf no disponible; se mantiene el orden original"
    fi
  fi

  warmup_phase

  for project_dir in "${projects[@]}"; do
    run_project_once "$project_dir"
  done

  build_master_tables
  merge_reports

  log "Ejecucion global terminada"
  log "Master JSON: $MASTER_JSON"
  log "Master CSV: $MASTER_CSV"
  log "PDF conjunto: $MERGED_PDF"
}

main "$@"