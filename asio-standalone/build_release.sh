#!/usr/bin/env bash
set -e

BUILD_DIR="build"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --config Release -j"$(nproc)"

echo ""
echo "Compilacion ASIO STANDALONE completada en modo Release."
echo "Ejecutables:"
echo "  $BUILD_DIR/tcpserver/tcpserver"
echo "  $BUILD_DIR/tcpclient/tcpclient"
echo "  $BUILD_DIR/benchmarks/bench_tcp"
