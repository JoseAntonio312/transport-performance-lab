#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_DIRS=(
  "asio-standalone"
  "bechmark_taps"
  "becnhmark_async_berkeley"
  "boost-asio"
  "bsd-sockets"
  "corosio"
)

log() {
  printf '\n[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

run_project() {
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

  log "Ejecutando benchmarks de $project_dir"
  (
    cd "$full_dir"
    python3 scripts/run_bench.py
  )
}

main() {
  log "Raiz del repositorio: $ROOT_DIR"

  for project_dir in "${PROJECT_DIRS[@]}"; do
    run_project "$project_dir"
  done

  log "Ejecucion global terminada"
}

main "$@"