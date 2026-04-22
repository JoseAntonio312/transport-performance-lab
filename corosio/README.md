# COROSIO - File transfer and benchmarking in C++23

This project provides a **C++23** experimental baseline for studying file transfer over **Corosio**, with a strong focus on:

- functional **TCP server** implementation;
- functional **TCP client** implementation;
- support for **multiple concurrent clients**;
- **Release** builds with **CMake**;
- comparison between **G++** and **Clang++** builds;
- benchmarking with **Google Benchmark**;
- concurrent campaigns with multiple `bench_tcp` processes;
- energy measurement through **RAPL / powercap**;
- automatic generation of **JSON**, **CSV**, **plots**, and **PDF reports**.

The goal is to keep the implementation as simple, reproducible, and comparable as possible so it can be used in controlled benchmarking campaigns.

---

## Project goal

The current goal of the project is to maintain a clean Corosio-based implementation that can be used as an experimental baseline to study:

- file transfer performance;
- scalability with multiple concurrent clients;
- throughput under different concurrency levels;
- energy consumption;
- statistical behavior across repeated runs;
- differences between binaries compiled with **G++** and **Clang++**.

This version is intentionally designed to reduce experimental noise:

- the server loads the file into memory once before accepting clients;
- file system activity is kept outside the measured network-serving path;
- the transfer protocol is kept minimal;
- console output is minimized during measurements;
- the automation script aggregates results and computes summary statistics.

---

## Project structure

```text
corosio/
├── benchmarks/
│   ├── CMakeLists.txt
│   └── bench_tcp.cpp
├── build-gcc/
├── build-clang/
├── results/
│   ├── macro_bench_results.json
│   ├── macro_bench_summary.json
│   ├── macro_bench_results.csv
│   ├── macro_bench_report.pdf
│   └── micro_*.json
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

The server:

- receives the path of the file to serve from the command line;
- loads the full file into memory before starting the benchmarked serving phase;
- listens for incoming TCP connections;
- accepts multiple concurrent clients;
- sends the same in-memory payload to every connected client;
- minimizes console output to avoid measurement noise.

The current simplified design avoids per-client disk reads during the transfer phase, so the benchmark focuses on the networking and runtime behavior rather than storage effects.

### 2. TCP client

The client:

- connects to the server using the provided IP and port;
- receives raw bytes until the server closes the connection;
- writes the received stream directly to an output file;
- keeps runtime output minimal.

In the simplified version currently used for the experiments, the client does **not** depend on a custom application-level header. It simply receives the byte stream until EOF.

### 3. TCP benchmark

The benchmark:

- acts as a measurement-oriented client;
- connects to the real server;
- downloads the full byte stream;
- does **not** write the data to disk during the benchmarked path;
- measures download time using **Google Benchmark**;
- emits JSON output for later aggregation.

Several benchmark processes can be launched concurrently by the automation script in order to emulate multiple simultaneous clients.

### 4. Automation script

The Python automation script:

- starts the server;
- waits until the server is actually ready;
- launches benchmark campaigns;
- can run multiple `bench_tcp` processes in parallel;
- measures energy before and after each campaign;
- optionally subtracts an idle baseline from the measured energy;
- validates and parses the generated JSON files;
- produces aggregated **JSON**, **CSV**, **plots**, and **PDF reports**.

---

## Transfer model

The current simplified Corosio version uses a **raw byte-stream model**:

```text
[file bytes only]
```

### Meaning

- the server sends the file contents directly;
- the client keeps reading until the server closes the TCP connection;
- no filename field is transmitted;
- no file-size field is transmitted;
- connection closure marks the end of the transfer.

This keeps the data path as small and simple as possible, which is useful for low-noise performance and energy measurements.

> Note: older versions of the project used a custom header such as `[u32 filename_size][filename][u64 file_size][payload]`. That is **not** the current simplified design.

---

## Environment requirements

### System

Reference environment used in the project:

- Ubuntu 24.04 LTS
- Linux kernel with `powercap` / RAPL support
- x86_64 CPU with C++23-capable toolchains

### Compilers

The project is intended to be built and tested with:

- **G++**
- **Clang++**

The experimental goal is to compare the same codebase under both compilers, especially because coroutine-heavy code may show relevant optimization differences depending on the toolchain.

### CMake

```bash
sudo apt update
sudo apt install cmake
```

### Google Benchmark

```bash
sudo apt install libbenchmark-dev
```

### Python

```bash
sudo apt install python3 python3-pip python3-matplotlib
```

If PDF reports are merged or post-processed at repository level, `pypdf` may also be required.

---

## Corosio dependency

This project uses **Corosio** as the networking library and relies on **C++23 coroutines**.

Depending on your local setup, Corosio may be:

- provided through the project configuration;
- downloaded through CMake dependency resolution;
- or installed separately if your repository layout expects it.

At minimum, you need:

- a compiler with working **C++23 coroutine** support;
- **CMake**;
- **Google Benchmark** for `bench_tcp`;
- the Corosio headers and libraries accessible to the build.

---

## Building the project

The project is expected to be built in **Release** mode.

### Recommended build script

```bash
sudo ./build_release.sh
```

### Typical build targets

This project usually builds:

- `tcpserver`
- `tcpclient`
- `bench_tcp`

and may generate separate build directories such as:

- `build-gcc/`
- `build-clang/`

so both toolchains can be benchmarked independently.

---

## Running the project

### Start the server manually

```bash
./build-gcc/tcpserver/tcpserver ../files/1GB.bin 8120 1
```

Example meaning:

- serve `../files/1GB.bin`;
- listen on port `8120`;
- run with `1` server thread.

### Run the client manually

```bash
./build-gcc/tcpclient/tcpclient 127.0.0.1 8120 downloaded.bin
```

### Run a single benchmark manually

```bash
./build-gcc/benchmarks/bench_tcp \
  --server_port=8120 \
  --benchmark_out=results/micro.json \
  --benchmark_out_format=json
```

### Run the automated campaign

```bash
sudo python3 scripts/run_bench.py
```

or:

```bash
sudo ./scripts/run_bench.py
```

---

## Benchmark methodology

The campaign script is prepared to explore combinations such as:

- server thread counts: `1`, `2`, `4`, `8`;
- parallel benchmark clients: `1`, `2`, `4`, `8`, `16`;
- repeated executions per case;
- separate GCC and Clang campaigns.

Typical measured quantities are:

- total campaign time;
- raw energy;
- estimated idle energy;
- net energy;
- aggregate throughput;
- downloads per process;
- valid and failed runs.

The script also computes summary statistics such as:

- count;
- mean;
- median;
- standard deviation;
- p25;
- p50;
- p95;
- minimum;
- maximum.

---

## Generated outputs

The campaign produces files such as:

### `results/micro_*.json`

Per-process Google Benchmark JSON outputs.

### `results/macro_bench_results.json`

Aggregated raw campaign results.

### `results/macro_bench_summary.json`

Statistical summary grouped by:

- compiler;
- server thread count;
- number of parallel benchmark processes.

### `results/macro_bench_results.csv`

Flat tabular export of the raw campaign results.

### `results/macro_bench_report.pdf`

A generated PDF report that can include:

- experiment metadata;
- statistical summary tables;
- auxiliary summary tables;
- raw execution tables;
- plots for time, energy, throughput, and downloads per process.

---

## Idle baseline and net energy

The benchmarking workflow can optionally use an idle-power baseline stored in a repository-level file such as:

```text
idle_baseline.json
```

When enabled, the script can compute:

- **raw energy** measured through RAPL;
- **estimated idle energy** during the same elapsed interval;
- **net energy** = raw energy - estimated idle energy.

This makes the energy analysis more meaningful than reporting raw RAPL deltas alone.

---

## Recommendations for reliable measurements

To obtain more stable and scientifically useful results, it is recommended to:

- always build in **Release** mode;
- keep the machine as idle and stable as possible;
- disable heavy background tasks when possible;
- keep the CPU governor consistent;
- allow cooldown periods between libraries and between distinct benchmark cases;
- optionally flush caches or perform cache trashing between cases if that is part of the methodology;
- run enough repetitions to obtain meaningful distributions rather than single-point results;
- compare GCC and Clang under the same machine conditions.

Example CPU governor configuration:

```bash
sudo cpupower frequency-set -g performance
```

---

## Notes on the current simplified design

The current Corosio version is intentionally more minimal than earlier protocol-based versions:

- the server sends only the file bytes;
- the client reads until EOF;
- the benchmark avoids disk output in the measured path;
- the code tries to minimize unnecessary allocations and noisy runtime behavior;
- the goal is to isolate networking and coroutine/runtime behavior as much as possible.

This simplified version is better aligned with low-noise benchmarking than a richer application protocol, because it avoids adding metadata parsing and extra protocol logic to the measured path.

---

## Author

**José Antonio García Montañez**