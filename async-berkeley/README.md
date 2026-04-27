# ASYNC-BERKELEY - File Transfer and Benchmarking in C++23

This project provides a **C++23** experimental baseline to study file transfer over **async-berkeley**, with a focus on:

- a functional **TCP server** implementation;
- a functional **TCP client** implementation;
- support for **multiple concurrent clients**;
- **Release-mode** builds with **CMake**;
- compilation and comparison with **G++** and **Clang++**;
- benchmarking with **Google Benchmark**;
- concurrent campaigns with multiple `bench_tcp` processes;
- energy measurement through **RAPL / powercap**.

The goal is to first build a clean, reproducible, and minimal implementation and then run measurement campaigns for:

- execution time;
- scalability;
- throughput;
- energy consumption;
- mean;
- median;
- percentiles;
- minimum and maximum values.

In addition to comparing transport technologies, this version also supports comparing executables built with **G++** and **Clang++**, in order to evaluate whether the compiler introduces measurable differences in an **async-berkeley** implementation.

---

## Project goal

The current goal of the project is to maintain a dedicated **async-berkeley** implementation as an experimental baseline to study:

- file transfer;
- concurrency with multiple clients;
- performance benchmarking;
- energy consumption measurement;
- behavioral differences between executables compiled with **G++** and **Clang++**.

At this stage, the implementation follows the same simplified experimental model used in the rest of the repository:

- the server preloads the file before serving clients;
- the server sends **raw file bytes only**;
- **no custom application-level header** is used;
- the client downloads until **EOF**;
- the benchmark measures one complete download per iteration.

This keeps the implementation as small and comparable as possible while avoiding extra protocol logic that could distort the measurements.

---

## About async-berkeley

The **async-berkeley** library used in this project is **not authored by the author of this repository**. The library is used here as a transport layer to build the experimental:

- TCP server;
- TCP client;
- TCP benchmark.

The original library lives in its own repository and should remain clearly separated from the benchmarking project itself.

The work specific to this repository focuses on:

- integrating async-berkeley into a standalone benchmarking project;
- adapting the transport implementation to the experimental methodology;
- keeping the implementation comparable to the BSD sockets version;
- running performance and energy campaigns on top of that base;
- repeating those campaigns with binaries compiled using different compilers.

---

## Current transfer model

The current implementation uses a **raw-byte transfer model**:

- the server loads the input file once before accepting clients;
- the server sends only the file payload;
- the client reads until the connection is closed by the server;
- the benchmark follows the exact same download logic as the client.

### Important note

This means the project **does not** currently use a custom protocol such as:

```text
[u32 filename_size][filename][u64 file_size][file_contents]
```

That protocol belonged to an earlier version. The current version is intentionally simpler in order to reduce measurement noise and keep the logic aligned with the rest of the transport implementations being compared.

---

## Project structure

```text
async-berkeley/
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

A common layout is to keep the library outside the benchmark project, for example:

```text
~/Desktop/TFM/
├── async-berkeley/
└── becnhmark_async_berkeley/
```

This separation makes the setup cleaner because it clearly distinguishes:

- the **base library**;
- the **experimental project** that depends on it.

---

## Main components

### 1. TCP server

The server:

- receives the file path from the command line;
- loads the file before the measurement phase;
- exposes the payload directly from memory;
- listens for TCP connections;
- accepts multiple clients;
- sends the same raw file bytes to every connected client;
- uses minimal console output to avoid adding measurement noise.

The current server is intentionally kept very close in spirit to the BSD sockets implementation, but using **async-berkeley** socket abstractions where appropriate.

### 2. TCP client

The client:

- connects to the server using IP address and port;
- receives raw bytes until EOF;
- writes the received data directly to disk;
- keeps console output minimal to reduce experimental noise.

### 3. TCP benchmark

The benchmark:

- acts as a benchmark client;
- connects to the real server;
- downloads the served file completely;
- measures download time using **Google Benchmark**;
- can export structured output in **JSON**.

It is used as the basic measurement unit. Concurrent campaigns are built by launching several `bench_tcp` processes in parallel from the Python automation script.

### 4. Automation script

The Python script:

- starts the server;
- waits until the server is actually ready;
- runs benchmark campaigns;
- can launch one or more `bench_tcp` processes in parallel;
- measures energy before and after each campaign;
- validates and processes the generated JSON files;
- writes raw results, summaries, plots, and PDF reports.

In the current workflow, the script is also designed to distinguish between **G++** and **Clang++** builds so that both compiler families can be compared under equivalent conditions.

---

## Methodological adaptation for async-berkeley

This project does not try to demonstrate every feature of async-berkeley. Instead, it adapts async-berkeley to the benchmarking methodology used across the repository.

### 1. Separation between library and experiment

The benchmark project is intentionally kept separate from the async-berkeley library itself. This makes it easier to:

- preserve the original library independently;
- avoid mixing library internals with benchmark infrastructure;
- compare this implementation with the other transport implementations;
- keep the project easier to maintain and reproduce.

### 2. async-berkeley as the socket layer

The implementation relies on the async-berkeley API for the socket layer, using abstractions such as:

- `io::socket::socket_handle`
- `io::bind`
- `io::listen`
- `io::accept`
- `io::connect`
- `io::sendmsg`
- `io::recvmsg`
- `io::fcntl`
- `io::setsockopt`

This makes it possible to build the server, client, and benchmark with the library while preserving a structure that stays experimentally comparable to the BSD sockets version.

### 3. Why the server threading model is not written like Corosio

The server **could** be made to look a bit more like the Corosio thread-join style, but the current async-berkeley version is written differently on purpose.

In the Corosio server, the threading model is naturally tied to:

- an `io_context`;
- coroutine scheduling;
- one event system shared across worker threads.

That makes a pattern like this natural:

```cpp
for (int i = 0; i < threads; ++i) {
    pool[i] = std::thread([&ctx]() {
        ctx.run();
    });
}

for (int i = 0; i < threads; ++i) {
    pool[i].join();
}
```

In the async-berkeley version used here, the implementation is closer to a **manual worker model**:

- one accept loop assigns clients to workers;
- each worker manages its own client set;
- each worker uses `poll()` directly on its own descriptors;
- wake-up pipes are used to notify workers about new clients.

So the current code is not centered around a shared coroutine/event-loop runtime in the same way. Because of that, the server is structured around:

- an accept thread path in `main()`;
- a fixed set of worker threads;
- one blocking worker loop per worker.

That is why it does not look exactly like the Corosio pattern.

### 4. Can it be made more similar to Corosio?

Yes, **the thread creation and joining style can be made cosmetically more similar** if you want.

For example, you can still use:

- a fixed-size thread array;
- one loop to create the threads;
- one loop to join the threads.

That part is easy.

What cannot be made truly identical is the deeper execution model:

- Corosio relies on coroutine-driven asynchronous execution;
- this async-berkeley server version is organized around explicit worker loops and `poll()`.

So the answer is:

- **yes**, the thread array and `join()` style can be made more visually similar to Corosio;
- **no**, the underlying control flow is not really the same, and that is the reason the current server was written differently.

---

## Environment requirements

### System

- Ubuntu 24.04 LTS
- Linux kernel with `powercap` support
- Reference CPU: AMD Ryzen 7 7700
- Reference RAM: 32 GB DDR5 6000 MT/s

### Compilers

The project targets **C++23** and uses two compilers:

- **G++**
- **Clang++**

The experimental goal is to compile the same codebase with both compilers and compare performance, scalability, and energy results.

Example GCC configuration with `update-alternatives`:

```bash
sudo update-alternatives --install /usr/bin/g++ g++ /usr/local/gcc-14.1.0/bin/g++-14.1.0 14
sudo update-alternatives --install /usr/bin/gcc gcc /usr/local/gcc-14.1.0/bin/gcc-14.1.0 14
```

Example Clang installation:

```bash
sudo apt update
sudo apt install clang
```

### CMake

```bash
sudo apt update
sudo apt install cmake
```

### Google Benchmark

```bash
sudo apt update
sudo apt install libbenchmark-dev
```

### Python

```bash
sudo apt update
sudo apt install python3 python3-pip
```

### Python packages used by the benchmarking scripts

Some benchmark scripts in this repository generate plots and PDF reports. The most common packages are:

```bash
sudo apt update
sudo apt install python3-matplotlib python3-pypdf
```

If `python3-pypdf` is not available in your distribution, use a virtual environment instead of forcing a system-wide pip install.

---

## async-berkeley location

The build script expects the async-berkeley library to exist at a local path defined inside `build_release.sh`.

Example:

```bash
ASYNC_BERKELEY_DIR="/home/jagarcia/Desktop/TFM/async-berkeley/async-berkeley"
```

You should replace that with the path where **you** have installed the library in your machine.

That means the path in the script is not universal; it is just the local installation path used on the machine where the benchmark project is being built.

---

## Build

The project is intended to be built in **Release** mode.

### Recommended build script

```bash
sudo ./build_release.sh
```

### Build a single compiler configuration

```bash
sudo ./build_release.sh gcc
```

or:

```bash
sudo ./build_release.sh clang
```

### Manual example

```bash
sudo rm -rf build-gcc
sudo cmake -S . -B build-gcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DASYNC_BERKELEY_DIR=/home/jagarcia/Desktop/TFM/async-berkeley/async-berkeley \
  -DCMAKE_C_COMPILER=/usr/local/gcc-14.1.0/bin/gcc-14.1.0 \
  -DCMAKE_CXX_COMPILER=/usr/local/gcc-14.1.0/bin/g++-14.1.0
sudo cmake --build build-gcc --config Release -j"$(nproc)" --target tcpserver tcpclient bench_tcp
```

### Build philosophy

The current experimental workflow generates two comparable executable sets:

- one built with **G++**;
- one built with **Clang++**.

This allows each campaign to be repeated with both compilers without changing the source code, isolating the compiler effect inside the study.

---

## Execution

### Build everything

```bash
sudo ./build_release.sh
```

### Run the server manually

For the GCC build:

```bash
sudo ./build-gcc/tcpserver/tcpserver ../files/100MB.bin 8080 1
```

For the Clang build:

```bash
sudo ./build-clang/tcpserver/tcpserver ../files/100MB.bin 8081 1
```

### Run the client manually

For the GCC build:

```bash
sudo ./build-gcc/tcpclient/tcpclient 127.0.0.1 8080 downloaded.bin
```

For the Clang build:

```bash
sudo ./build-clang/tcpclient/tcpclient 127.0.0.1 8081 downloaded.bin
```

### Run a single benchmark execution

Example with the GCC build:

```bash
sudo ./build-gcc/benchmarks/bench_tcp --server_port=8080 --benchmark_out=results/raw/micro.json --benchmark_out_format=json
```

### Run the full automatic campaign

```bash
sudo python3 scripts/run_bench.py
```

or:

```bash
sudo ./scripts/run_bench.py
```

---

## Types of experiments

### Single benchmark run

A single `bench_tcp` process is executed to measure one controlled full download against the server.

### Concurrent campaign

Several `bench_tcp` processes are launched in parallel, simulating multiple concurrent clients downloading the same file.

Typical campaign cases include:

- 1 benchmark client process
- 2 benchmark client processes
- 4 benchmark client processes
- 8 benchmark client processes
- 16 benchmark client processes

This makes it possible to measure:

- total campaign execution time;
- energy consumption;
- behavior under concurrency;
- server scalability.

### Compiler comparison campaign

In addition to concurrency, the same campaigns can be executed using two different binary families:

- binaries compiled with **G++**;
- binaries compiled with **Clang++**.

This makes it possible to study differences in:

- execution time;
- throughput;
- scalability;
- energy efficiency;
- measurement stability.

---

## Benchmark automation script

### Path

```text
scripts/run_bench.py
```

### What it does

The script:

1. starts the server;
2. waits until the server is actually ready;
3. records initial energy;
4. launches a benchmark campaign;
5. records final energy;
6. validates and processes the `micro_*.json` files;
7. generates machine-readable and human-readable outputs.

Depending on the current `run_bench.py` version, it can also:

- separate raw outputs, plots, reports, and logs into different subdirectories;
- generate a main PDF report;
- generate a separate compiler-comparison PDF report;
- include or exclude raw execution tables depending on the report variant.

---

## Generated outputs

Depending on the current `run_bench.py` version, outputs are usually organized under:

```text
results/
├── raw/
├── plots/
├── reports/
└── logs/
```

### `results/raw/`

Typical contents:

- `macro_bench_results.json`
- `macro_bench_summary.json`
- `macro_bench_results.csv`
- `micro_*.json`

These files store raw and aggregated campaign data.

### `results/plots/`

Typical contents:

- execution time plots
- raw energy plots
- net energy plots
- throughput plots
- downloads-per-process plots
- compiler comparison plots

### `results/reports/`

Typical contents:

- main benchmark PDF report
- compiler comparison PDF report
- report variants with and without raw results

### `results/logs/`

Typical contents:

- server stdout/stderr logs
- benchmark stderr logs

---

## Reported metrics

The automatic campaigns may include, depending on the active script version:

- total execution time;
- raw energy consumption;
- estimated idle energy;
- net energy consumption;
- aggregate throughput;
- downloads per process;
- total iterations;
- successful benchmark processes;
- failed benchmark processes.

The generated summaries and PDFs may also include:

- mean;
- median;
- standard deviation;
- p25;
- p50;
- p95;
- minimum;
- maximum.

---

## Recommended measurement settings

### Set the CPU governor

```bash
sudo cpupower frequency-set -g performance
```

### Additional recommendations

- always compile in `Release`;
- avoid `Debug` builds;
- compare **G++** and **Clang++** under identical conditions;
- close heavy background applications;
- repeat experiments several times;
- control initial thermal conditions;
- reduce thermal and frequency noise as much as possible.

If energy measurements are important, it is also recommended to:

- measure the system idle baseline first;
- subtract the baseline from raw energy measurements when appropriate;
- keep the machine as idle and stable as possible outside the benchmark itself.

---

## Notes on the current implementation

This current async-berkeley version has been simplified to match the experimental design used in the rest of the repository:

- no extra application-level transfer header;
- no transmitted filename metadata;
- no transmitted file-size metadata;
- server sends the raw file bytes only;
- client and benchmark read until EOF;
- server-side file payload is kept out of the hot path as much as possible.

The objective is to make the comparison across transport technologies and compilers as homogeneous as possible while minimizing implementation-specific overhead that is not central to the transport experiment itself.

---

## Methodological note

If the async-berkeley version performs better than the BSD sockets version in a given set of tests, that does **not** automatically imply that async-berkeley is always faster in general.

The result must always be interpreted as:

- dependent on the specific implementation;
- dependent on the traffic pattern;
- dependent on the number of clients;
- dependent on file and buffer size;
- dependent on the internal overhead of each abstraction.

Therefore, the reported results should always be presented as experimental results for the evaluated implementation and environment.

---

## Authorship note

The **async-berkeley library dependency itself was not developed by the author of this benchmarking project**. If this repository is distributed academically or publicly, it is strongly recommended to explicitly reference:

- the original async-berkeley repository;
- the original authorship;
- the applicable license.

---

## Author

**José Antonio García Montañez**