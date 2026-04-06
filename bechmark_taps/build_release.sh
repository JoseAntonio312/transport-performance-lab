#!/usr/bin/env bash
set -euo pipefail

TAPS_INSTALL_GCC="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install-gcc"
TAPS_INSTALL_CLANG="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install-clang"

build_one() {
    local compiler="$1"
    local build_dir=""
    local compiler_label=""
    local cxx_compiler=""
    local taps_prefix=""
    local extra_cmake_args=()

    case "$compiler" in
      gcc)
        build_dir="build-gcc"
        compiler_label="GCC 14.1.0"
        cxx_compiler="/usr/local/gcc-14.1.0/bin/g++-14.1.0"
        taps_prefix="$TAPS_INSTALL_GCC"
        extra_cmake_args=(
          -DTAPS_TCP_USE_LIBCXX=OFF
        )
        ;;
      clang)
        build_dir="build-clang"
        compiler_label="CLANG 18 + libc++"
        cxx_compiler="/usr/bin/clang++-18"
        taps_prefix="$TAPS_INSTALL_CLANG"
        extra_cmake_args=(
          -DTAPS_TCP_USE_LIBCXX=ON
        )
        ;;
      *)
        echo "Compilador no soportado: $compiler"
        exit 1
        ;;
    esac

    if [ ! -x "$cxx_compiler" ]; then
        echo "Error: no existe $cxx_compiler"
        exit 1
    fi

    if [ ! -f "$taps_prefix/lib/cmake/TAPS/TAPSConfig.cmake" ]; then
        echo "Error: TAPS no encontrada en $taps_prefix"
        exit 1
    fi

    rm -rf "$build_dir"

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$taps_prefix" \
        -DCMAKE_CXX_COMPILER="$cxx_compiler" \
        "${extra_cmake_args[@]}"

    if [ "$compiler" = "gcc" ]; then
        cmake --build "$build_dir" --config Release -j"$(nproc)" \
            --target tcpserver tcpclient bench_tcp
    else
        cmake --build "$build_dir" --config Release -j"$(nproc)" \
            --target tcpserver tcpclient
    fi

    echo ""
    echo "Compilacion TAPS TCP completada en modo Release con $compiler_label."
    echo "Usando TAPS desde:"
    echo "  $taps_prefix"
    echo "Ejecutables:"
    echo "  $build_dir/tcpserver/tcpserver"
    echo "  $build_dir/tcpclient/tcpclient"

    if [ "$compiler" = "gcc" ]; then
        echo "  $build_dir/benchmarks/bench_tcp"
    fi

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
        echo "Uso: $0 [gcc|clang]"
        exit 1
        ;;
    esac
else
    echo "Uso: $0 [gcc|clang]"
    exit 1
fi