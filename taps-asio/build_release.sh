#!/usr/bin/env bash
set -euo pipefail

TAPS_INSTALL_GCC="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install-gcc"
TAPS_INSTALL_CLANG="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install-clang"

BENCHMARK_REPO_DIR="${HOME}/Escritorio/TFM/google-benchmark-src"
BENCHMARK_INSTALL_CLANG="${HOME}/Escritorio/TFM/google-benchmark-install-clang-libcxx"

CLANG_C_COMPILER="/usr/bin/clang-19"
CLANG_CXX_COMPILER="/usr/bin/clang++-19"

GCC_C_COMPILER="/usr/local/gcc-14.1.0/bin/gcc-14.1.0"
GCC_CXX_COMPILER="/usr/local/gcc-14.1.0/bin/g++-14.1.0"

need_cmd() {
    command -v "$1" >/dev/null 2>&1
}

check_taps_install() {
    local taps_prefix="$1"

    if [ ! -f "$taps_prefix/include/taps/taps_api.h" ]; then
        echo "Error: TAPS header not found:"
        echo "  $taps_prefix/include/taps/taps_api.h"
        echo ""
        echo "Build and install proyecto_taps first."
        exit 1
    fi

    if [ ! -f "$taps_prefix/lib/libtaps.a" ]; then
        echo "Error: TAPS static library not found:"
        echo "  $taps_prefix/lib/libtaps.a"
        echo ""
        echo "Build and install proyecto_taps first."
        exit 1
    fi
}

ensure_google_benchmark_repo() {
    if [ -d "$BENCHMARK_REPO_DIR/.git" ]; then
        return
    fi

    rm -rf "$BENCHMARK_REPO_DIR"
    git clone https://github.com/google/benchmark.git "$BENCHMARK_REPO_DIR"

    if [ ! -d "$BENCHMARK_REPO_DIR/googletest/.git" ]; then
        git -C "$BENCHMARK_REPO_DIR" clone https://github.com/google/googletest.git googletest
    fi
}

ensure_clang_benchmark_install() {
    local config_file="$BENCHMARK_INSTALL_CLANG/lib/cmake/benchmark/benchmarkConfig.cmake"

    if [ -f "$config_file" ]; then
        echo "Using existing Clang/libc++ Google Benchmark install:"
        echo "  $BENCHMARK_INSTALL_CLANG"
        return
    fi

    echo "Clang/libc++ Google Benchmark not found. Building it automatically..."

    ensure_google_benchmark_repo

    rm -rf "$BENCHMARK_REPO_DIR/build-clang-libcxx"

    cmake -S "$BENCHMARK_REPO_DIR" -B "$BENCHMARK_REPO_DIR/build-clang-libcxx" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$CLANG_C_COMPILER" \
        -DCMAKE_CXX_COMPILER="$CLANG_CXX_COMPILER" \
        -DCMAKE_CXX_STANDARD=23 \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_CXX_EXTENSIONS=OFF \
        -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
        -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++" \
        -DCMAKE_SHARED_LINKER_FLAGS="-stdlib=libc++" \
        -DBENCHMARK_ENABLE_GTEST_TESTS=OFF \
        -DBENCHMARK_DOWNLOAD_DEPENDENCIES=OFF \
        -DCMAKE_INSTALL_PREFIX="$BENCHMARK_INSTALL_CLANG"

    cmake --build "$BENCHMARK_REPO_DIR/build-clang-libcxx" -j"$(nproc)"
    cmake --install "$BENCHMARK_REPO_DIR/build-clang-libcxx"
}

build_one() {
    local compiler="$1"
    local build_dir=""
    local compiler_label=""
    local c_compiler=""
    local cxx_compiler=""
    local taps_prefix=""
    local cmake_prefix_path=""
    local benchmark_dir=""
    local extra_cmake_args=()

    case "$compiler" in
      gcc)
        build_dir="build-gcc"
        compiler_label="GCC 14.1.0"
        c_compiler="$GCC_C_COMPILER"
        cxx_compiler="$GCC_CXX_COMPILER"
        taps_prefix="$TAPS_INSTALL_GCC"
        cmake_prefix_path="$taps_prefix"

        check_taps_install "$taps_prefix"

        extra_cmake_args=(
          -DTAPS_ROOT="$taps_prefix"
          -DTAPS_TCP_USE_LIBCXX=OFF
          -DTAPS_ENABLE_BENCHMARKS=ON
          -DCMAKE_CXX_STANDARD=23
          -DCMAKE_CXX_STANDARD_REQUIRED=ON
          -DCMAKE_CXX_EXTENSIONS=OFF
        )
        ;;

      clang)
        build_dir="build-clang"
        compiler_label="Clang 19 + libc++"
        c_compiler="$CLANG_C_COMPILER"
        cxx_compiler="$CLANG_CXX_COMPILER"
        taps_prefix="$TAPS_INSTALL_CLANG"

        check_taps_install "$taps_prefix"
        ensure_clang_benchmark_install

        benchmark_dir="$BENCHMARK_INSTALL_CLANG/lib/cmake/benchmark"
        cmake_prefix_path="$taps_prefix;$BENCHMARK_INSTALL_CLANG"

        extra_cmake_args=(
          -DTAPS_ROOT="$taps_prefix"
          -Dbenchmark_DIR="$benchmark_dir"
          -DTAPS_TCP_USE_LIBCXX=ON
          -DTAPS_ENABLE_BENCHMARKS=ON
          -DCMAKE_CXX_STANDARD=23
          -DCMAKE_CXX_STANDARD_REQUIRED=ON
          -DCMAKE_CXX_EXTENSIONS=OFF
          -DCMAKE_CXX_FLAGS="-stdlib=libc++"
          -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"
          -DCMAKE_SHARED_LINKER_FLAGS="-stdlib=libc++"
        )
        ;;

      *)
        echo "Unsupported compiler: $compiler"
        exit 1
        ;;
    esac

    rm -rf "$build_dir"

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$cmake_prefix_path" \
        -DCMAKE_C_COMPILER="$c_compiler" \
        -DCMAKE_CXX_COMPILER="$cxx_compiler" \
        "${extra_cmake_args[@]}"

    cmake --build "$build_dir" --config Release -j"$(nproc)" \
        --target tcpserver tcpclient bench_tcp

    echo ""
    echo "TAPS TCP Release build completed with $compiler_label."
    echo "Using TAPS from:"
    echo "  $taps_prefix"

    if [ "$compiler" = "clang" ]; then
        echo "Using Google Benchmark from:"
        echo "  $BENCHMARK_INSTALL_CLANG"
    fi

    echo "Executables:"
    echo "  $build_dir/tcpserver/tcpserver"
    echo "  $build_dir/tcpclient/tcpclient"
    echo "  $build_dir/benchmarks/bench_tcp"
    echo ""
}

main() {
    if ! need_cmd cmake; then
        echo "Error: cmake is not installed"
        exit 1
    fi

    if ! need_cmd git; then
        echo "Error: git is not installed"
        exit 1
    fi

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
}

main "$@"