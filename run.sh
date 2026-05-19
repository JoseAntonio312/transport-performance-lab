#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLOBAL_RESULTS_DIR="$ROOT_DIR/global_results"

RAW_DIR="$GLOBAL_RESULTS_DIR/raw"
SUMMARIES_DIR="$GLOBAL_RESULTS_DIR/summaries"
CSV_DIR="$GLOBAL_RESULTS_DIR/csv"
REPORTS_DIR="$GLOBAL_RESULTS_DIR/reports"
REPORTS_MAIN_WITH_RAW_DIR="$REPORTS_DIR/main_with_raw"
REPORTS_MAIN_WITHOUT_RAW_DIR="$REPORTS_DIR/main_without_raw"
REPORTS_COMPARISON_WITH_RAW_DIR="$REPORTS_DIR/comparison_with_raw"
REPORTS_COMPARISON_WITHOUT_RAW_DIR="$REPORTS_DIR/comparison_without_raw"
REPORTS_PER_LIBRARY_DIR="$REPORTS_DIR/per_library"
MERGED_REPORTS_DIR="$REPORTS_DIR/merged"
GLOBAL_REPORTS_DIR="$REPORTS_DIR/global"

PLOTS_DIR="$GLOBAL_RESULTS_DIR/plots"
GLOBAL_PLOTS_DIR="$PLOTS_DIR/global"
PER_PROJECT_DIR="$GLOBAL_RESULTS_DIR/per_project"
LOGS_DIR="$GLOBAL_RESULTS_DIR/logs"
MANIFESTS_DIR="$GLOBAL_RESULTS_DIR/manifests"

MASTER_JSON="$SUMMARIES_DIR/master_summary.json"
MASTER_CSV="$CSV_DIR/master_summary.csv"
MASTER_PDF="$GLOBAL_REPORTS_DIR/global_master_comparison_report.pdf"

SYSTEM_INFO_TXT="$GLOBAL_RESULTS_DIR/system_info.txt"
RUN_LOG="$LOGS_DIR/run_log.txt"
RUN_MANIFEST_JSON="$MANIFESTS_DIR/run_manifest.json"

PROJECT_DIRS=(
  "asio"
  "taps-asio"
  "async-berkeley"
  "bsd-sockets"
  "capy-corosio"
)

SETTLE_SECONDS_BEFORE="${SETTLE_SECONDS_BEFORE:-20}"
SETTLE_SECONDS_AFTER="${SETTLE_SECONDS_AFTER:-20}"
WARMUP_ENABLED="${WARMUP_ENABLED:-0}"
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
  mkdir -p \
    "$GLOBAL_RESULTS_DIR" \
    "$RAW_DIR" \
    "$SUMMARIES_DIR" \
    "$CSV_DIR" \
    "$REPORTS_DIR" \
    "$REPORTS_MAIN_WITH_RAW_DIR" \
    "$REPORTS_MAIN_WITHOUT_RAW_DIR" \
    "$REPORTS_COMPARISON_WITH_RAW_DIR" \
    "$REPORTS_COMPARISON_WITHOUT_RAW_DIR" \
    "$REPORTS_PER_LIBRARY_DIR" \
    "$MERGED_REPORTS_DIR" \
    "$GLOBAL_REPORTS_DIR" \
    "$PLOTS_DIR" \
    "$GLOBAL_PLOTS_DIR" \
    "$PER_PROJECT_DIR" \
    "$LOGS_DIR" \
    "$MANIFESTS_DIR"

  : > "$RUN_LOG"
}

save_manifest() {
  cat > "$RUN_MANIFEST_JSON" <<EOF
{
  "root_dir": "$ROOT_DIR",
  "global_results_dir": "$GLOBAL_RESULTS_DIR",
  "raw_dir": "$RAW_DIR",
  "summaries_dir": "$SUMMARIES_DIR",
  "csv_dir": "$CSV_DIR",
  "reports_dir": "$REPORTS_DIR",
  "global_reports_dir": "$GLOBAL_REPORTS_DIR",
  "merged_reports_dir": "$MERGED_REPORTS_DIR",
  "plots_dir": "$PLOTS_DIR",
  "global_plots_dir": "$GLOBAL_PLOTS_DIR",
  "per_project_dir": "$PER_PROJECT_DIR",
  "logs_dir": "$LOGS_DIR",
  "manifests_dir": "$MANIFESTS_DIR",
  "master_json": "$MASTER_JSON",
  "master_csv": "$MASTER_CSV",
  "master_pdf": "$MASTER_PDF",
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
    log "Idle measurement disabled"
    return
  fi

  local idle_script="$ROOT_DIR/measure_idle_energy.py"

  if [ ! -f "$idle_script" ]; then
    log "measure_idle_energy.py not found at repository root; continuing without a new idle measurement"
    return
  fi

  log "Measuring system idle baseline..."
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
    log "No permission to write /proc/sys/vm/drop_caches; continuing"
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

copy_if_exists() {
  local src="$1"
  local dst="$2"

  if [ -f "$src" ]; then
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
    log "Copied: $dst"
  fi
}

copy_project_artifacts_if_exist() {
  local project_dir="$1"
  local results_dir="$ROOT_DIR/$project_dir/results"
  local project_out_dir="$PER_PROJECT_DIR/$project_dir"

  mkdir -p \
    "$project_out_dir/raw" \
    "$project_out_dir/summaries" \
    "$project_out_dir/csv" \
    "$project_out_dir/reports" \
    "$project_out_dir/plots"

  copy_if_exists \
    "$results_dir/raw/macro_bench_results.json" \
    "$project_out_dir/raw/macro_bench_results.json"

  copy_if_exists \
    "$results_dir/raw/macro_bench_summary.json" \
    "$project_out_dir/summaries/macro_bench_summary.json"

  copy_if_exists \
    "$results_dir/raw/macro_bench_results.csv" \
    "$project_out_dir/csv/macro_bench_results.csv"

  copy_if_exists \
    "$results_dir/raw/macro_bench_results.json" \
    "$RAW_DIR/${project_dir}_raw.json"

  copy_if_exists \
    "$results_dir/raw/macro_bench_summary.json" \
    "$SUMMARIES_DIR/${project_dir}_summary.json"

  copy_if_exists \
    "$results_dir/raw/macro_bench_results.csv" \
    "$CSV_DIR/${project_dir}_raw.csv"

  copy_if_exists \
    "$results_dir/reports/macro_bench_report_with_raw.pdf" \
    "$project_out_dir/reports/macro_bench_report_with_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_report_no_raw.pdf" \
    "$project_out_dir/reports/macro_bench_report_no_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_comparison_report_with_raw.pdf" \
    "$project_out_dir/reports/macro_bench_comparison_report_with_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_comparison_report_no_raw.pdf" \
    "$project_out_dir/reports/macro_bench_comparison_report_no_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_report_with_raw.pdf" \
    "$REPORTS_MAIN_WITH_RAW_DIR/${project_dir}_main_with_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_report_no_raw.pdf" \
    "$REPORTS_MAIN_WITHOUT_RAW_DIR/${project_dir}_main_without_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_comparison_report_with_raw.pdf" \
    "$REPORTS_COMPARISON_WITH_RAW_DIR/${project_dir}_comparison_with_raw.pdf"

  copy_if_exists \
    "$results_dir/reports/macro_bench_comparison_report_no_raw.pdf" \
    "$REPORTS_COMPARISON_WITHOUT_RAW_DIR/${project_dir}_comparison_without_raw.pdf"

  if [ -d "$results_dir/reports" ]; then
    find "$results_dir/reports" -maxdepth 1 -type f -name "*.pdf" | while read -r pdf; do
      local name
      name="$(basename "$pdf")"
      copy_if_exists "$pdf" "$REPORTS_PER_LIBRARY_DIR/${project_dir}_${name}"
    done
  fi

  if [ -d "$results_dir/plots" ]; then
    mkdir -p "$PLOTS_DIR/$project_dir"
    cp -r "$results_dir/plots/." "$PLOTS_DIR/$project_dir/" 2>/dev/null || true
    cp -r "$results_dir/plots/." "$project_out_dir/plots/" 2>/dev/null || true
    log "Copied plots for $project_dir"
  fi
}

run_project_once() {
  local project_dir="$1"
  local full_dir="$ROOT_DIR/$project_dir"
  local run_script="$full_dir/scripts/run_bench.py"

  if [ ! -d "$full_dir" ]; then
    log "Skipping $project_dir: directory not found"
    return
  fi

  if [ ! -f "$run_script" ]; then
    log "Skipping $project_dir: scripts/run_bench.py not found"
    return
  fi

  trash_caches_best_effort
  settle_before

  log "Running benchmarks for $project_dir"
  (
    cd "$full_dir"
    python3 scripts/run_bench.py
  )

  copy_project_artifacts_if_exist "$project_dir"
  settle_after
}

warmup_phase() {
  if [ "$WARMUP_ENABLED" != "1" ]; then
    log "Warmup disabled"
    return
  fi

  log "Warmup phase enabled"

  local count=0
  for project_dir in "${PROJECT_DIRS[@]}"; do
    if [ "$count" -ge "$WARMUP_PROJECTS" ]; then
      break
    fi

    local full_dir="$ROOT_DIR/$project_dir"
    local run_script="$full_dir/scripts/run_bench.py"

    if [ -d "$full_dir" ] && [ -f "$run_script" ]; then
      log "Warmup with $project_dir"
      (
        cd "$full_dir"
        python3 scripts/run_bench.py >/dev/null 2>&1 || true
      )
      count=$((count + 1))
      settle_after
    fi
  done
}

build_master_tables() {
  local script="$ROOT_DIR/build_master_summary.py"

  if [ ! -f "$script" ]; then
    log "build_master_summary.py not found; global master table will not be generated"
    return
  fi

  log "Generating global comparison tables and PDF report..."
  python3 "$script" \
    --input-dir "$SUMMARIES_DIR" \
    --json-out "$MASTER_JSON" \
    --csv-out "$MASTER_CSV" \
    --pdf-out "$MASTER_PDF" \
    --plots-dir "$GLOBAL_PLOTS_DIR"
}

merge_reports() {
  if [ "$MERGE_PDFS_AT_END" != "1" ]; then
    log "PDF merging disabled"
    return
  fi

  local script="$ROOT_DIR/merge.py"

  if [ ! -f "$script" ]; then
    log "merge.py not found; report merging skipped"
    return
  fi

  log "Merging categorized PDF reports..."
  python3 "$script" \
    --input-dir "$REPORTS_DIR" \
    --output-dir "$MERGED_REPORTS_DIR"
}

main() {
  prepare_dirs
  save_manifest
  save_system_info

  log "Repository root: $ROOT_DIR"
  log "Global results directory: $GLOBAL_RESULTS_DIR"

  if ! need_cmd python3; then
    echo "Error: python3 is not available"
    exit 1
  fi

  measure_idle_if_available

  local projects=("${PROJECT_DIRS[@]}")

  if [ "$RANDOMIZE_ORDER" = "1" ]; then
    if need_cmd shuf; then
      mapfile -t projects < <(printf '%s\n' "${PROJECT_DIRS[@]}" | shuf)
      log "Randomized project order enabled: ${projects[*]}"
    else
      log "shuf not available; keeping the original project order"
    fi
  fi

  warmup_phase

  for project_dir in "${projects[@]}"; do
    run_project_once "$project_dir"
  done

  build_master_tables
  merge_reports

  log "Global execution finished"
  log "Master JSON: $MASTER_JSON"
  log "Master CSV: $MASTER_CSV"
  log "Master PDF: $MASTER_PDF"
  log "Merged reports directory: $MERGED_REPORTS_DIR"
}

main "$@"
