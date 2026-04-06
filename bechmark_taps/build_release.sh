#!/usr/bin/env bash
set -euo pipefail

TAPS_INSTALL_DIR="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install"
BUILD_DIR="build"

C_COMPILER="/usr/local/gcc-14.1.0/bin/gcc-14.1.0"
CXX_COMPILER="/usr/local/gcc-14.1.0/bin/g++-14.1.0"

if [ ! -d "$TAPS_INSTALL_DIR" ]; then
    echo "Error: TAPS no encontrada en $TAPS_INSTALL_DIR"
    exit 1
fi

if [ ! -x "$C_COMPILER" ]; then
    echo "Error: no existe $C_COMPILER"
    exit 1
fi

if [ ! -x "$CXX_COMPILER" ]; then
    echo "Error: no existe $CXX_COMPILER"
    exit 1
fi

rm -rf "$BUILD_DIR"

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$TAPS_INSTALL_DIR" \
    -DCMAKE_C_COMPILER="$C_COMPILER" \
    -DCMAKE_CXX_COMPILER="$CXX_COMPILER"

cmake --build "$BUILD_DIR" --config Release -j"$(nproc)"

echo ""
echo "Compilacion TAPS TCP completada en modo Release con GCC 14.1.0."
echo "Compiladores forzados:"
echo "  C   = $C_COMPILER"
echo "  CXX = $CXX_COMPILER"
echo "Ejecutables:"
echo "  $BUILD_DIR/tcpserver/tcpserver"
echo "  $BUILD_DIR/tcpclient/tcpclient"

if [ -f "$BUILD_DIR/benchmarks/bench_tcp" ]; then
    echo "  $BUILD_DIR/benchmarks/bench_tcp"
fi

echo ""