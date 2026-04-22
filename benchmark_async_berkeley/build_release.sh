#!/usr/bin/env bash
set -euo pipefail

ASYNC_BERKELEY_DIR="/home/jagarcia/Escritorio/TFM/async-berkeley/async-berkeley" #YOUR PATH

if [ ! -f "$ASYNC_BERKELEY_DIR/CMakeLists.txt" ]; then
    echo "Error: async-berkeley was not found at $ASYNC_BERKELEY_DIR"
    exit 1
fi

build_one() {
    local compiler="$1"
    local build_dir=""
    local compiler_label=""
    local c_compiler=""
    local cxx_compiler=""

    case "$compiler" in
      gcc)
        build_dir="build-gcc"
        compiler_label="GCC 14"
        c_compiler="/usr/local/gcc-14.1.0/bin/gcc-14.1.0"
        cxx_compiler="/usr/local/gcc-14.1.0/bin/g++-14.1.0"
        ;;
      clang)
        build_dir="build-clang"
        compiler_label="Clang"
        c_compiler="clang"
        cxx_compiler="clang++"
        ;;
      *)
        echo "Unsupported compiler: $compiler"
        exit 1
        ;;
    esac

    if ! command -v "$c_compiler" >/dev/null 2>&1; then
        echo "Error: $c_compiler is not installed or not available in PATH"
        exit 1
    fi

    if ! command -v "$cxx_compiler" >/dev/null 2>&1; then
        echo "Error: $cxx_compiler is not installed or not available in PATH"
        exit 1
    fi

    rm -rf "$build_dir"

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DASYNC_BERKELEY_DIR="$ASYNC_BERKELEY_DIR" \
        -DCMAKE_C_COMPILER="$c_compiler" \
        -DCMAKE_CXX_COMPILER="$cxx_compiler"

    cmake --build "$build_dir" --config Release -j"$(nproc)" \
        --target tcpserver tcpclient bench_tcp

    echo ""
    echo "ASYNC-BERKELEY build completed in Release mode with $compiler_label."
    echo "Executables:"
    echo "  $build_dir/tcpserver/tcpserver"
    echo "  $build_dir/tcpclient/tcpclient"
    echo "  $build_dir/benchmarks/bench_tcp"
    echo ""
}

if [ $# -eq 0 ]; then
    build_one gcc
    build_one clang
elif [ $# -eq 1 ]; then
    case "$1" in
      gcc|clang)
        build_one "$1"
        ;;
      *)
        echo "Usage: $0 [gcc|clang]"
        exit 1
        ;;
    esac
else
    echo "Usage: $0 [gcc|clang]"
    exit 1
fi