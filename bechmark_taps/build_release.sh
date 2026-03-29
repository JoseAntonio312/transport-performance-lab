#!/usr/bin/env bash
set -e

BUILD_DIR="build"
TAPS_INSTALL_DIR="/home/jagarcia/Escritorio/TFM/taps-asio/proyecto_taps/install"

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$TAPS_INSTALL_DIR"

cmake --build "$BUILD_DIR" --config Release -j"$(nproc)"

echo ""
echo "Compilacion TAPS TCP completada en modo Release."
echo "Ejecutables:"
echo "  $BUILD_DIR/tcpserver/tcpserver"
echo "  $BUILD_DIR/tcpclient/tcpclient"

if [ -f "$BUILD_DIR/benchmarks/bench_tcp" ]; then
    echo "  $BUILD_DIR/benchmarks/bench_tcp"
fi