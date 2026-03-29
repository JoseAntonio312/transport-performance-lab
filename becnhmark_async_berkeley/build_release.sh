#!/usr/bin/env bash
set -e

BUILD_DIR="build"
ASYNC_BERKELEY_DIR="/home/jagarcia/Escritorio/TFM/async-berkeley/async-berkeley"

if [ ! -f "$ASYNC_BERKELEY_DIR/CMakeLists.txt" ]; then
    echo "Error: async-berkeley no encontrado en $ASYNC_BERKELEY_DIR"
    exit 1
fi

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DASYNC_BERKELEY_DIR="$ASYNC_BERKELEY_DIR"

cmake --build "$BUILD_DIR" --config Release -j"$(nproc)" \
    --target tcpserver tcpclient bench_tcp

echo ""
echo "Compilacion completada en modo Release."
echo "Ejecutables:"
echo "  $BUILD_DIR/tcpserver/tcpserver"
echo "  $BUILD_DIR/tcpclient/tcpclient"
echo "  $BUILD_DIR/benchmarks/bench_tcp"