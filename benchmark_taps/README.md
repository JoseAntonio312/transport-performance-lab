# TAPS - File Transfer and Benchmarking in C++23

This project provides an experimental **C++23** codebase to study file transfer over **TAPS**, with a focus on:

- functional **TCP server** implementation;
- functional **TCP client** implementation;
- support for **multiple concurrent clients**;
- **Release-mode** builds with **CMake**;
- compilation and comparison with **G++** and **Clang++**;
- benchmarking with **Google Benchmark**;
- concurrent benchmark campaigns with multiple `bench_tcp` processes;
- energy measurements through **RAPL/powercap**.

The goal is to build a clean, reproducible, and experimentally comparable implementation and then use it to run measurement campaigns for:

- execution time;
- scalability;
- throughput;
- energy consumption;
- mean;
- median;
- percentiles;
- minimum and maximum values.

In addition to comparing transport technologies, this version also compares binaries compiled with **G++** and binaries compiled with **Clang++**, in order to study whether the compiler itself introduces measurable differences in a **TAPS-based** implementation.

---

## Project goal

The current goal of the project is to maintain an experimental implementation based on **TAPS** that can be used to study:

- file transfer;
- concurrency with multiple clients;
- performance benchmarking;
- energy consumption;
- behavioral differences between binaries compiled with **G++** and **Clang++**.

At this stage, the implementation is based on **TAPS over standalone Asio**, while keeping the experimental methodology aligned with the rest of the repository.

---

## About the TAPS library

The **TAPS** library used in this project is **not authored by the author of this benchmark project**. It is an external dependency developed by a third party, and here it is used as the transport substrate for the experimental implementations of:

- TCP server;
- TCP client;
- TCP benchmark.

The work carried out in this project focuses on:

- integrating the TAPS library into a separate benchmarking project;
- adapting the transport logic to the benchmark framework;
- keeping the implementation experimentally comparable to BSD sockets, standalone Asio, Boost.Asio, Corosio, and other variants in the repository;
- running performance and energy campaigns on top of that implementation;
- repeating those campaigns with different compilers.

---

## Current implementation notes

The current implementation follows the same **high-level experimental philosophy** used in the rest of the repository:

- minimal console output during measurements;
- reproducible command-line interface;
- one server binary, one client binary, and one benchmark binary;
- support for campaigns driven by the common Python automation script;
- GCC and Clang build variants stored in separate build directories.

For the current TAPS version, the implementation is intentionally simplified around the same ideas used in the Corosio variant:

- the server sends **raw file bytes**;
- the client reads until the transport closes the connection;
- the benchmark also reads until end-of-stream;
- unnecessary application-level protocol parsing is avoided where possible.

That said, the **final memory behavior is still partially constrained by the TAPS API itself**. Even if the application code avoids large extra allocations, the TAPS library may still allocate internally for connection and message management.

---

## Project structure

```text
benchmark_taps/
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

The TAPS library is expected to live **outside this benchmarking project**, for example in a sibling directory:

```text
taps-asio/
├── proyecto_taps/        # original TAPS library installation/build
└── benchmark_taps/       # this benchmarking project
```

This layout is cleaner because it clearly separates:

- the **base transport library**;
- the **experimental benchmark project** built on top of it.

---

## Main components

### 1. TCP server

The server:

- receives from the command line the path of the file that must be served;
- prepares the file before the measurement phase;
- listens for incoming TCP connections through TAPS;
- accepts multiple clients;
- sends the same raw file contents to every connected client;
- keeps console output minimal to avoid introducing noise into the measurements.

The concurrency model follows the same spirit as the Corosio version:

- one `asio::io_context`;
- one asynchronous accept loop;
- multiple worker threads calling `io_context.run()`.

### 2. TCP client

The client:

- connects to the server using IP and port;
- receives raw bytes from the transport;
- writes them directly to an output file;
- keeps console output minimal for more stable measurements.

The client reads until the connection is closed by the server. No extra application-level transfer header is required in the current version.

### 3. TCP benchmark

The benchmark:

- acts as a measurement client;
- connects to the real server;
- consumes the full incoming byte stream;
- measures download time with **Google Benchmark**;
- can generate console output and **JSON** output through the benchmark framework.

It is the basic measurement unit. Concurrent campaigns are constructed by launching several `bench_tcp` processes in parallel from the automation script.

### 4. Automation script

The Python script:

- starts the server;
- runs complete benchmark campaigns;
- can launch one or several `bench_tcp` processes in parallel;
- measures energy before and after each campaign;
- validates and processes the generated JSON files;
- stores combined results in JSON, CSV, plots, and PDF reports.

The same automation pipeline is also used to compare GCC and Clang builds under equivalent conditions.

---

## Transfer model

The current implementation is based on a **raw-byte stream** model:

```text
[file bytes only]
```

### Meaning

- the server sends only the file payload;
- the client receives bytes until the connection closes;
- the benchmark follows the same logic.

This keeps the TAPS version closer to the simplified Corosio model currently used for experimental consistency.

---

## TAPS-specific considerations

Several TAPS-specific decisions are important to understand this implementation.

### 1. External installation is required

TAPS is **not bundled** inside this project. You must install or build it yourself first, and then point this benchmark project to your local TAPS installation paths.

This is why the build script contains user-specific path variables such as:

```bash
TAPS_INSTALL_GCC="/your/path/to/taps/install-gcc"
TAPS_INSTALL_CLANG="/your/path/to/taps/install-clang"
```

You must edit these paths to match **your own local environment** before building.

### 2. Separate GCC and Clang TAPS installs

The project assumes that TAPS may be built separately for GCC and Clang, for example:

- one install tree for GCC;
- one install tree for Clang + libc++.

The benchmark build then links each variant against its corresponding TAPS installation.

### 3. Not all allocations can be removed

The benchmark project avoids unnecessary large application-level buffers where possible, but **TAPS itself may still allocate internally**. In particular:

- `taps::Connection` ownership is library-managed;
- `taps::Message` may require owned payload buffers;
- some internal transport abstractions may allocate regardless of the benchmark code.

So the implementation is designed to be **minimal and experimentally clean**, but not every allocation can be eliminated if the TAPS API itself requires it.

### 4. Raw-byte model instead of a custom header

Unlike older versions that reconstructed a custom header such as filename and file size, the current implementation intentionally keeps the logic simpler:

- no application-level filename exchange;
- no file-size field;
- the server just sends the raw payload;
- the client and benchmark read until EOF.

This reduces parsing overhead and keeps the TAPS variant aligned with the current simplified methodology.

---

## Environment requirements

### System

- Ubuntu 24.04 LTS
- Linux kernel with `powercap` support
- Reference CPU: AMD Ryzen 7 7700
- Reference RAM: 32 GB DDR5 6000 MT/s

### Compilers

Two **C++23** compilers are used:

- **G++**
- **Clang++**

The goal is to compile the same code with both compilers and compare performance, scalability, and energy behavior.

Example GCC setup with `update-alternatives`:

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

### Standalone Asio

TAPS depends on standalone Asio. In this environment it is expected to be available from the system packages:

```bash
sudo apt update
sudo apt install libasio-dev
```

### Google Benchmark

```bash
sudo apt install libbenchmark-dev
```

### Python

```bash
sudo apt install python3 python3-pip
```

---

## Building the TAPS library

If the TAPS library has not been built yet, you must build it first.

A typical directory layout might be:

```text
~/Escritorio/TFM/taps-asio/
├── proyecto_taps/
└── benchmark_taps/
```

Typical commands to build the TAPS library may look like this:

```bash
cd ~/Escritorio/TFM/taps-asio/proyecto_taps
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

If you maintain separate install trees for GCC and Clang, build and install both variants accordingly.

---

## Building the TAPS benchmark project

The project is prepared for **Release** builds.

### Recommended script

```bash
sudo ./build_release.sh
```

### Important manual path configuration

Before running the build script, **edit it** and set your own local TAPS installation paths.

For example:

```bash
TAPS_INSTALL_GCC="/your/path/to/taps/install-gcc"
TAPS_INSTALL_CLANG="/your/path/to/taps/install-clang"
```

These values are machine-specific. The project will not build correctly until they point to valid local TAPS installs.

### Example manual build

```bash
rm -rf build-gcc build-clang

cmake -S . -B build-gcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/your/path/to/taps/install-gcc" \
  -DCMAKE_CXX_COMPILER="/usr/local/gcc-14.1.0/bin/g++-14.1.0"

cmake --build build-gcc --config Release -j"$(nproc)" \
  --target tcpserver tcpclient bench_tcp
```

And similarly for Clang, adapting:

- the compiler path;
- the TAPS installation path;
- any libc++ options required by your TAPS build.

### GCC vs Clang builds

The current experimental workflow explicitly supports building and testing this implementation with both:

- **G++**
- **Clang++**

The goal is to generate two comparable executable sets:

- one built with **G++**;
- one built with **Clang++**.

This allows the same benchmark campaigns to be repeated without changing the source code, isolating the effect of the compiler itself.

---

## Running the project

### Build

```bash
sudo ./build_release.sh
```

### Launch the server manually

```bash
sudo ./build-gcc/tcpserver/tcpserver ../files/100MB.bin 8080 1
```

### Launch the client manually

```bash
sudo ./build-gcc/tcpclient/tcpclient 127.0.0.1 8080 downloaded.bin
```

### Launch a single benchmark run

```bash
sudo ./build-gcc/benchmarks/bench_tcp \
  --server_port=8080 \
  --benchmark_out=results/raw/micro.json \
  --benchmark_out_format=json
```

### Launch the full automated campaign

```bash
sudo python3 scripts/run_bench.py
```

or:

```bash
sudo ./scripts/run_bench.py
```

---

## Automation script

### Path

```text
scripts/run_bench.py
```

### What it does

The script:

1. starts the server;
2. waits until it is actually ready;
3. records initial energy;
4. launches a benchmark campaign;
5. records final energy;
6. validates and processes the generated `micro_*.json` files;
7. produces aggregated JSON, CSV, plots, and PDF reports.

The script can also:

- separate raw data, plots, logs, and report outputs into different subdirectories;
- generate human-readable PDF reports;
- generate compiler comparison reports;
- compare GCC and Clang under equivalent settings;
- handle port conflicts more robustly than earlier versions.

### Script compatibility

The automation script is designed so that it can be reused across transport implementations as long as the binaries preserve the same names and command-line interface.

For the TAPS project, the important binaries are:

- `build-gcc/tcpserver/tcpserver`
- `build-gcc/benchmarks/bench_tcp`
- `build-clang/tcpserver/tcpserver`

Depending on your build configuration, the Clang benchmark binary may or may not be enabled.

---

## Generated results

Single benchmark runs with Google Benchmark produce results including:

- real time;
- CPU time;
- iteration count;
- repetitions;
- aggregate metrics such as mean, median, standard deviation, and coefficient of variation.

Concurrent campaigns additionally produce:

- total campaign time;
- total energy consumption;
- number of launched benchmark processes;
- valid iterations;
- transferred bytes;
- aggregate throughput.

Compiler-aware campaigns also distinguish:

- the compiler used;
- the executable family used;
- the comparability of equivalent runs between GCC and Clang.

---

## Recommended measurement setup

### Set the CPU governor

```bash
sudo cpupower frequency-set -g performance
```

### Additional recommendations

- always compile in `Release`;
- avoid `Debug` builds;
- repeat the same case with **G++** and **Clang++** under the same conditions;
- close heavy background applications;
- repeat experiments multiple times;
- control the initial temperature as much as possible;
- avoid thermal and frequency noise;
- keep the system as idle as possible outside the benchmark itself.

---

## Output files

### `results/raw/micro_*.json`

These files contain the Google Benchmark JSON output for each `bench_tcp` process executed during a campaign.

### `results/raw/macro_bench_results.json`

This file contains a processed summary including:

- total campaign time;
- total consumed energy;
- valid and failed benchmark processes;
- valid iterations;
- transferred bytes;
- aggregate throughput.

### `results/raw/macro_bench_summary.json`

This file contains grouped statistical summaries used later to build the final reports.

### `results/reports/*.pdf`

These files contain the human-readable benchmark reports and comparison reports.

### `results/plots/*.png`

These files contain per-compiler and cross-compiler plots.

### `results/logs/*.log`

These files contain server and benchmark stderr/stdout logs used for debugging failed runs.

---

## Methodological note

If a TAPS-based version performs better than a BSD sockets or `poll()`-based version in a specific benchmark campaign, that does **not** automatically imply that TAPS is always faster in general.

The comparison must always be interpreted as:

- dependent on the concrete implementation;
- dependent on the traffic pattern;
- dependent on the number of clients;
- dependent on file size and chunk size;
- dependent on the internal overhead introduced by each abstraction;
- dependent on the compiler and build configuration.

Therefore, all conclusions should be presented as **experimental results for the evaluated environment and implementation**, not as universal claims.

---

## Author

**José Antonio García Montañez**

---

## External authorship of TAPS

The **TAPS library used as a dependency was not developed by the author of this benchmark project**. In any academic or public distribution, it is recommended to explicitly acknowledge the original authorship of the TAPS library, along with its source location, license, and corresponding reference.