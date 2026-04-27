# BSD-sockets - File transfer and benchmarking in C++23

This project provides a **C++23** experimental baseline for studying file transfer over **BSD sockets with `poll()`**, with a focus on:

- a functional **TCP server**;
- a functional **TCP client**;
- support for **multiple concurrent clients**;
- **Release** builds with **CMake**;
- builds and comparisons with **G++** and **Clang++**;
- benchmarking with **Google Benchmark**;
- concurrent benchmark campaigns using multiple `bench_tcp` processes;
- energy measurements through **RAPL / powercap**.

The goal is to keep the implementation as simple, reproducible, and efficient as possible so that benchmark results are driven by the transport mechanism itself rather than by unnecessary abstraction overhead.

This BSD sockets variant is aligned with the simplified raw-byte design used in the other implementations:

- the server sends **raw file bytes only**;
- the client receives bytes **until EOF**;
- there is **no custom application header**;
- the benchmark downloads the full payload **until the connection is closed**.

In plots, reports, and comparison tables, this implementation should be labeled as:

```text
bsd-sockets
```

The word `benchmark` should not appear in the plotted implementation labels, since all plotted entries are benchmarked variants.

---

## Project objective

The current objective of this project is to provide a BSD sockets implementation that can serve as an experimental baseline for studying:

- file transfer performance;
- concurrency with multiple clients;
- benchmark reproducibility;
- energy consumption;
- differences between binaries compiled with **G++** and **Clang++**.

At this stage, the implementation is intentionally kept close to the simplified raw-byte design used in the rest of the benchmark suite so that comparisons across transport technologies remain as fair as possible.

The expected implementation labels across the full comparison suite are:

```text
bsd-sockets
asio
capy-corosio
taps-asio
asyncberkeley
```

These names are the labels that should appear in generated figures, legends, CSV summaries, and PDF reports.

---

## Current transfer model

This version does **not** use a custom transfer protocol.

### Server behavior

The server:

- receives the file path from the command line;
- opens the file and maps it read-only into memory;
- listens for TCP connections;
- accepts multiple clients concurrently;
- sends the file contents as **raw bytes only**;
- closes the connection after the full file has been sent.

### Client behavior

The client:

- connects to the server using IP and port;
- receives bytes in a fixed-size buffer;
- writes them directly to the output file;
- stops when the server closes the connection.

### Benchmark behavior

The benchmark:

- acts as a minimal client;
- connects to the real server;
- receives bytes until EOF;
- does not write the downloaded data to disk;
- measures one full download per benchmark iteration using **Google Benchmark**.

This makes the BSD sockets implementation more directly comparable to the simplified Corosio version and to the other raw-byte transfer implementations.

---

## Asynchronous model used by this variant

Although this project is named `bsd-sockets`, the current server and client are not written as purely blocking sequential programs.

The implementation uses:

```text
BSD sockets + O_NONBLOCK + poll()
```

This means that the sockets are standard BSD/POSIX sockets, but the I/O model is readiness-based and non-blocking.

### Server-side concurrency

The server uses:

- a non-blocking listening socket;
- non-blocking accepted client sockets;
- `poll()` to wait for readable/writable file descriptors;
- worker threads;
- one client set per worker;
- per-client send progress tracked with byte offsets.

Each client receives the same mapped file contents. The server closes the client socket after the full payload has been sent.

### Client-side behavior

The client also uses:

- a non-blocking socket;
- `poll()` to wait for connection completion;
- `poll()` to wait for readable data when `recv()` would block.

Therefore, this implementation should be understood as a **BSD sockets + poll readiness-based implementation**, not as a purely blocking sequential server.

---

## Project structure

```text
bsd-sockets/
├── benchmarks/
│   ├── CMakeLists.txt
│   └── bench_tcp.cpp
├── build-gcc/
├── build-clang/
├── results/
│   ├── raw/
│   ├── plots/
│   ├── reports/
│   └── logs/
├── scripts/
│   └── run_bench.py
├── tcpclient/
│   ├── CMakeLists.txt
│   └── client.cpp
├── tcpserver/
│   ├── CMakeLists.txt
│   └── server.cpp
├── build_release.sh
└── CMakeLists.txt
```

---

## Main components

### 1. TCP server

The server is a minimal raw-byte TCP sender.

Main properties:

- the input file is exposed as a read-only memory region;
- no application-level header is generated;
- each client receives the exact same byte stream;
- once the full payload has been sent, the socket is closed;
- the implementation uses `poll()` and non-blocking sockets;
- concurrency is handled with **worker threads**, each one polling the shared listening socket and its own client set.

This keeps the implementation simple and efficient while remaining faithful to a low-level BSD sockets design.

### 2. TCP client

The client is a minimal raw-byte TCP receiver.

Main properties:

- non-blocking socket connection;
- connection completion handled with `poll()`;
- data reception in a fixed-size buffer;
- direct write to disk;
- termination on EOF.

### 3. TCP benchmark

The benchmark behaves like the minimal client, but without writing to disk.

Main properties:

- connects to the server;
- downloads until EOF;
- uses a fixed-size receive buffer;
- measures one full transfer per benchmark iteration.

Multiple concurrent benchmark clients are launched externally by the Python automation script.

### 4. Automation script

The Python script:

- starts the server automatically;
- waits until the server is actually ready;
- launches multiple `bench_tcp` processes in parallel;
- measures raw and net energy;
- collects JSON benchmark outputs;
- computes aggregated statistics;
- generates CSV files, plots, and PDF reports.

The automation and plotting pipeline should normalize the displayed implementation name to:

```text
bsd-sockets
```

For example, internal executable names such as `bench_tcp`, `BM_TCP_FileDownload`, or folder names containing `benchmark` should not be used directly as final plot labels.

---

## Environment requirements

### Operating system

- Ubuntu 24.04 LTS
- Linux kernel with `powercap` support

### Compilers

The project is intended to be built with both:

- **G++**
- **Clang++**

Example GCC configuration:

```bash
sudo update-alternatives --install /usr/bin/g++ g++ /usr/local/gcc-14.1.0/bin/g++-14.1.0 14
sudo update-alternatives --install /usr/bin/gcc gcc /usr/local/gcc-14.1.0/bin/gcc-14.1.0 14
```

Example Clang installation:

```bash
sudo apt update
sudo apt install -y clang
```

### Build tools

```bash
sudo apt update
sudo apt install -y cmake make
```

### Google Benchmark

```bash
sudo apt install -y libbenchmark-dev
```

### Python

```bash
sudo apt install -y python3 python3-pip python3-matplotlib
```

---

## Build

The project is prepared for **Release** builds.

### Recommended build command

```bash
sudo ./build_release.sh
```

### Build only one compiler variant

```bash
sudo ./build_release.sh gcc
```

or:

```bash
sudo ./build_release.sh clang
```

### What the build script generates

For each compiler, the build script generates:

- `build-*/tcpserver/tcpserver`
- `build-*/tcpclient/tcpclient`
- `build-*/benchmarks/bench_tcp`

---

## Execution

### Build everything

```bash
sudo ./build_release.sh
```

### Start the server manually

```bash
sudo ./build-gcc/tcpserver/tcpserver ../files/100MB.bin 8080 1
```

Example with Clang build:

```bash
sudo ./build-clang/tcpserver/tcpserver ../files/100MB.bin 8081 2
```

### Start the client manually

```bash
sudo ./build-gcc/tcpclient/tcpclient 127.0.0.1 8080 downloaded.bin
```

### Run one benchmark manually

```bash
sudo ./build-gcc/benchmarks/bench_tcp \
  --server_port=8080 \
  --benchmark_out=results/raw/micro_manual.json \
  --benchmark_out_format=json
```

### Run the automated benchmark campaign

```bash
sudo python3 scripts/run_bench.py
```

or:

```bash
sudo ./scripts/run_bench.py
```

---

## Benchmark campaign model

### Single-case execution

A single benchmark case consists of:

- one server configuration;
- one number of parallel benchmark client processes;
- several repetitions of that same case.

### Concurrent campaign

A concurrent campaign launches multiple `bench_tcp` processes in parallel, for example:

- 1 parallel benchmark process;
- 2 parallel benchmark processes;
- 4 parallel benchmark processes;
- 8 parallel benchmark processes;
- 16 parallel benchmark processes.

This is the relevant scenario for observing whether the non-blocking readiness-based server improves its behavior when the number of simultaneous service requests increases.

### Compiler comparison

The same campaign can be repeated with:

- binaries built with **G++**;
- binaries built with **Clang++**.

This makes it possible to compare:

- execution time;
- throughput;
- net energy consumption;
- stability of repeated runs.

---

## Output files

### Raw benchmark JSON files

Located under `results/raw/`:

- `micro_*.json`
- `macro_bench_results.json`
- `macro_bench_summary.json`
- `macro_bench_results.csv`

### Plots

Located under `results/plots/`.

These typically include:

- execution time plots;
- raw energy plots;
- net energy plots;
- throughput plots;
- per-process completion plots;
- GCC vs Clang comparison plots.

Plot labels should use implementation names only. Recommended labels are:

```text
bsd-sockets
asio
capy-corosio
taps-asio
asyncberkeley
```

Avoid labels such as:

```text
benchmark-bsd-sockets
bsd-sockets-benchmark
BM_TCP_FileDownload
bench_tcp
```

### Reports

Located under `results/reports/`.

Depending on the current reporting configuration, this may include:

- a main PDF report without raw tables;
- a main PDF report with raw tables;
- a compiler-comparison PDF without raw tables;
- a compiler-comparison PDF with raw tables.

### Logs

Located under `results/logs/`.

These include:

- server stdout/stderr logs;
- benchmark stderr logs.

---

## Included metrics

The reporting pipeline can include:

- total execution time;
- raw energy consumption;
- estimated idle energy;
- net energy consumption;
- aggregate throughput;
- completed downloads per benchmark process;
- total successful iterations;
- failed processes.

---

## Included statistics

For each case, the reporting pipeline can compute:

- count;
- mean;
- median;
- standard deviation;
- percentile 25;
- percentile 50;
- percentile 95;
- minimum;
- maximum.

---

## Measurement recommendations

For more stable and scientifically cleaner runs:

```bash
sudo cpupower frequency-set -g performance
```

Additional recommendations:

- always build in **Release** mode;
- avoid running heavy background tasks;
- keep the machine thermally stable before long campaigns;
- repeat each case multiple times;
- keep the same methodology across GCC and Clang;
- use the same served file for all comparable runs;
- compare implementations under the same number of concurrent benchmark processes;
- keep implementation labels stable across all generated figures and reports.

---

## Notes on the current BSD sockets design

This current BSD sockets implementation is intentionally aligned with the simplified raw-byte transport model:

- no filename header;
- no file-size header;
- no extra application protocol;
- transfer ends when the server closes the connection.

This makes it much closer to the simplified transport logic used in the corresponding Corosio version, while preserving the particularities of a BSD sockets + `poll()` implementation.

For the current server implementation:

- file data is exposed through a read-only memory mapping;
- receive buffers use fixed-size stack storage where appropriate;
- worker-side client tracking avoids high-level abstractions as much as possible;
- concurrency is implemented with plain OS threads rather than coroutines;
- readiness-based I/O is implemented with `poll()` and non-blocking sockets.

---

## Author

**José Antonio García Montañez**