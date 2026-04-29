#!/usr/bin/env bash
set -euo pipefail

TAPS_PROJECT_DIR="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps"

TAPS_INSTALL_GCC="$TAPS_PROJECT_DIR/install-gcc"
TAPS_INSTALL_CLANG="$TAPS_PROJECT_DIR/install-clang"

ASIO_INCLUDE_DIR="/usr/include"

BENCHMARK_REPO_DIR="${HOME}/Escritorio/TFM/google-benchmark-src"
BENCHMARK_INSTALL_GCC="${HOME}/Escritorio/TFM/google-benchmark-install-gcc"
BENCHMARK_INSTALL_CLANG="${HOME}/Escritorio/TFM/google-benchmark-install-clang"

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
        return 1
    fi

    if [ ! -f "$taps_prefix/lib/libtaps.a" ]; then
        return 1
    fi

    return 0
}

ensure_taps_gcc_install() {
    if check_taps_install "$TAPS_INSTALL_GCC"; then
        echo "Using existing TAPS GCC install:"
        echo "  $TAPS_INSTALL_GCC"
        return
    fi

    echo "TAPS GCC install not found. Building proyecto_taps with GCC..."

    (
        cd "$TAPS_PROJECT_DIR"

        rm -rf build-gcc install-gcc

        cmake -S . -B build-gcc \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER="$GCC_C_COMPILER" \
            -DCMAKE_CXX_COMPILER="$GCC_CXX_COMPILER" \
            -DCMAKE_INSTALL_PREFIX="$TAPS_INSTALL_GCC" \
            -DASIO_INCLUDE_DIR="$ASIO_INCLUDE_DIR" \
            -DCMAKE_CXX_STANDARD=23 \
            -DCMAKE_CXX_STANDARD_REQUIRED=ON \
            -DCMAKE_CXX_EXTENSIONS=OFF

        cmake --build build-gcc -j"$(nproc)"
        cmake --install build-gcc
    )
}

ensure_taps_clang_install() {
    if check_taps_install "$TAPS_INSTALL_CLANG"; then
        echo "Using existing TAPS Clang install:"
        echo "  $TAPS_INSTALL_CLANG"
        return
    fi

    echo "TAPS Clang install not found. Building proyecto_taps with Clang"

    (
        cd "$TAPS_PROJECT_DIR"

        rm -f ./*.o
        rm -rf build-clang-manual install-clang

        mkdir -p build-clang-manual/obj
        mkdir -p install-clang/include
        mkdir -p install-clang/lib

        for src in src/*.cpp; do
            obj="build-clang-manual/obj/$(basename "${src%.cpp}.o")"

            "$CLANG_CXX_COMPILER" \
                -Wall -Wextra \
                -std=c++23 \
                -I./include \
                -I"$ASIO_INCLUDE_DIR" \
                -pthread \
                -O3 -DNDEBUG \
                -c "$src" \
                -o "$obj"
        done

        ar rcs install-clang/lib/libtaps.a build-clang-manual/obj/*.o

        cp -r include/taps install-clang/include/
    )
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

ensure_benchmark_install() {
    local compiler="$1"
    local install_dir=""
    local build_dir=""
    local c_compiler=""
    local cxx_compiler=""

    case "$compiler" in
      gcc)
        install_dir="$BENCHMARK_INSTALL_GCC"
        build_dir="$BENCHMARK_REPO_DIR/build-gcc"
        c_compiler="$GCC_C_COMPILER"
        cxx_compiler="$GCC_CXX_COMPILER"
        ;;
      clang)
        install_dir="$BENCHMARK_INSTALL_CLANG"
        build_dir="$BENCHMARK_REPO_DIR/build-clang"
        c_compiler="$CLANG_C_COMPILER"
        cxx_compiler="$CLANG_CXX_COMPILER"
        ;;
      *)
        echo "Unsupported compiler for benchmark: $compiler"
        exit 1
        ;;
    esac

    local config_file="$install_dir/lib/cmake/benchmark/benchmarkConfig.cmake"

    if [ -f "$config_file" ]; then
        echo "Using existing Google Benchmark install:"
        echo "  $install_dir"
        return
    fi

    echo "Google Benchmark not found for $compiler. Building it automatically..."

    ensure_google_benchmark_repo

    rm -rf "$build_dir"

    cmake -S "$BENCHMARK_REPO_DIR" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$c_compiler" \
        -DCMAKE_CXX_COMPILER="$cxx_compiler" \
        -DCMAKE_CXX_STANDARD=23 \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_CXX_EXTENSIONS=OFF \
        -DBENCHMARK_ENABLE_GTEST_TESTS=OFF \
        -DBENCHMARK_DOWNLOAD_DEPENDENCIES=OFF \
        -DCMAKE_INSTALL_PREFIX="$install_dir"

    cmake --build "$build_dir" -j"$(nproc)"
    cmake --install "$build_dir"
}

build_one() {
    local compiler="$1"
    local build_dir=""
    local compiler_label=""
    local c_compiler=""
    local cxx_compiler=""
    local taps_prefix=""
    local benchmark_prefix=""
    local benchmark_dir=""
    local cmake_prefix_path=""
    local extra_cmake_args=()

    case "$compiler" in
      gcc)
        build_dir="build-gcc"
        compiler_label="GCC 14.1.0"
        c_compiler="$GCC_C_COMPILER"
        cxx_compiler="$GCC_CXX_COMPILER"
        taps_prefix="$TAPS_INSTALL_GCC"
        benchmark_prefix="$BENCHMARK_INSTALL_GCC"

        ensure_taps_gcc_install
        ensure_benchmark_install gcc
        ;;

      clang)
        build_dir="build-clang"
        compiler_label="Clang 19"
        c_compiler="$CLANG_C_COMPILER"
        cxx_compiler="$CLANG_CXX_COMPILER"
        taps_prefix="$TAPS_INSTALL_CLANG"
        benchmark_prefix="$BENCHMARK_INSTALL_CLANG"

        ensure_taps_clang_install
        ensure_benchmark_install clang
        ;;

      *)
        echo "Unsupported compiler: $compiler"
        exit 1
        ;;
    esac

    benchmark_dir="$benchmark_prefix/lib/cmake/benchmark"
    cmake_prefix_path="$taps_prefix;$benchmark_prefix"

    extra_cmake_args=(
      -DTAPS_ROOT="$taps_prefix"
      -Dbenchmark_DIR="$benchmark_dir"
      -DTAPS_TCP_USE_LIBCXX=OFF
      -DTAPS_ENABLE_BENCHMARKS=ON
      -DCMAKE_CXX_STANDARD=23
      -DCMAKE_CXX_STANDARD_REQUIRED=ON
      -DCMAKE_CXX_EXTENSIONS=OFF
    )

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
    echo "Using Google Benchmark from:"
    echo "  $benchmark_prefix"
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

    if ! need_cmd ar; then
        echo "Error: ar is not installed"
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