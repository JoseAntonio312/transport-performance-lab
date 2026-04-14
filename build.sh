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

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

run_privileged() {
  if [ "${EUID:-$(id -u)}" -eq 0 ]; then
    "$@"
  elif need_cmd sudo; then
    sudo "$@"
  else
    echo "Error: se necesitan privilegios para ejecutar: $*"
    exit 1
  fi
}

ensure_python_pkg() {
  local module="$1"
  local pip_name="$2"

  if python3 -c "import ${module}" >/dev/null 2>&1; then
    log "Modulo Python '${module}' ya disponible"
    return
  fi

  log "Instalando modulo Python '${pip_name}'..."
  if ! python3 -m pip install --user "$pip_name"; then
    echo "Error: no se pudo instalar $pip_name"
    exit 1
  fi
}

ensure_matplotlib() {
  if python3 -c 'import matplotlib' >/dev/null 2>&1; then
    log "python3-matplotlib ya esta disponible"
    return
  fi

  log "Instalando python3-matplotlib..."
  if need_cmd apt-get; then
    run_privileged apt-get update
    run_privileged apt-get install -y python3-matplotlib
  else
    log "apt-get no disponible; intentando con pip"
    if ! python3 -m pip install --user matplotlib; then
      echo "Error: no se pudo instalar matplotlib automaticamente"
      exit 1
    fi
  fi
}

build_project() {
  local project_dir="$1"
  local full_dir="$ROOT_DIR/$project_dir"
  local build_script="$full_dir/build_release.sh"

  if [ ! -d "$full_dir" ]; then
    log "Saltando $project_dir: directorio no encontrado"
    return
  fi

  if [ ! -f "$build_script" ]; then
    log "Saltando $project_dir: build_release.sh no encontrado"
    return
  fi

  log "Compilando $project_dir"
  chmod +x "$build_script"

  (
    cd "$full_dir"
    ./build_release.sh
  )
}

main() {
  log "Raiz del repositorio: $ROOT_DIR"

  ensure_matplotlib
  ensure_python_pkg pypdf pypdf

  for project_dir in "${PROJECT_DIRS[@]}"; do
    build_project "$project_dir"
  done

  log "Compilacion global terminada"
}

main "$@"