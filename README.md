# Global Build and Benchmark Automation

This repository groups several experimental file-transfer implementations in **C++23**. The goal is to compare them under a common methodology in terms of:

- execution time
- scalability
- throughput
- energy consumption
- behavior under concurrency
- compiler-dependent differences
- single-threaded and multi-threaded server configurations

Each transport subproject contains its own implementation of:

- a TCP server
- a TCP client
- a Google Benchmark-based benchmark binary
- local automation scripts
- a project-specific `README.md`

In addition to those per-project READMEs, the repository root provides **global automation scripts** to build and run the whole benchmark suite from one place.

---

## Repository overview

The repository is organized as a collection of transport-specific benchmark projects, for example:

- `asio`
- `taps-asio`
- `async-berkeley`
- `bsd-sockets`
- `capy-corosio`

Each subproject may contain:

- its own `build_release.sh`
- its own `scripts/run_bench.py`
- its own `results/` directory
- its own compiler and dependency configuration

The global scripts do **not** replace that internal logic. They coordinate it.

---

## Purpose of the global scripts

At repository root, the workflow is centered around two scripts:

- `build.sh`: builds every transport project by delegating to each subproject's own `build_release.sh`
- `run.sh`: runs every benchmark campaign by delegating to each subproject's own `scripts/run_bench.py`

This makes it possible to:

- launch large build campaigns from one entry point
- run the full benchmark suite without entering each subproject manually
- preserve the internal build and run logic of each transport implementation
- automate the full thesis benchmarking workflow at repository level

---

## Expected subprojects

The global scripts are designed to iterate over these directories when present:

- `asio`
- `taps-asio`
- `async-berkeley`
- `bsd-sockets`
- `capy-corosio`

If a directory does not exist, or if the expected script is missing, it is skipped and reported.

---

## Global build script

### Name

```bash
sudo ./build.sh
```

### What it does

The global build script:

- assumes it is executed from the repository root
- checks that the basic required tools are available
- ensures Python dependencies needed by the reporting pipeline are installed
- enters each transport subproject
- runs its `build_release.sh` if present
- reports progress for the global build process

Each subproject remains fully responsible for:

- how it is compiled
- which compilers it uses
- which flags it enables
- which external libraries it requires
- how special cases such as TAPS or async-berkeley are resolved

The root-level build script only acts as a coordinator.

---

## Python dependencies used by the global workflow

Some benchmark pipelines generate figures and merged PDF reports. For that reason, the global build script may ensure the following Python packages are available:

- `matplotlib`
- `pypdf`

### Installation strategy

The script attempts to install missing dependencies in this order:

1. system packages with `apt-get`
2. fallback installation with `python3 -m pip install --user`

Typical Ubuntu commands are:

```bash
sudo apt-get update
sudo apt-get install -y python3-matplotlib python3-pypdf python3-pip
```

---

## Global benchmark execution script

### Name

```bash
sudo ./run.sh
```

### What it does

The global run script:

- enters each configured subproject
- looks for its `scripts/run_bench.py`
- launches the benchmark campaign if the script exists
- lets each project generate its own raw data, summaries, CSV files, plots, and PDF reports
- collects the resulting artifacts into a global results tree
- builds a repository-wide master summary
- optionally merges categorized PDF reports

This means the detailed benchmark behavior still belongs to each individual subproject, while the root script provides a unified orchestration layer.

---

## Recommended workflow

From repository root:

```bash
sudo ./build.sh
sudo ./run.sh
```

With this workflow:

1. all available implementations are built
2. all configured benchmark campaigns are executed
3. each subproject produces its own local results
4. the repository root collects those results into a unified global structure
5. cross-library comparisons become easier to inspect

---

## Global results layout

The current root-level automation is intended to store consolidated outputs under a structure like:

```text
global_results/
├── raw/
├── summaries/
├── csv/
├── reports/
│   ├── main_with_raw/
│   ├── main_without_raw/
│   ├── comparison_with_raw/
│   ├── comparison_without_raw/
│   ├── per_library/
│   └── merged/
├── plots/
├── per_project/
├── logs/
└── manifests/
```

### Meaning of the main folders

- `raw/`: collected raw JSON data from all subprojects
- `summaries/`: collected per-library summary JSON files plus the global master summary
- `csv/`: collected per-library CSV files plus the global master CSV
- `reports/`: categorized PDF reports produced by each transport implementation
- `plots/`: copied plot outputs from individual projects when available
- `per_project/`: a clean per-library mirror of collected outputs
- `logs/`: global orchestration logs
- `manifests/`: run configuration and execution metadata

This structure is meant to make the full campaign easier to inspect globally, while still preserving per-project separation.

---

## Global master summary

After the full execution, the repository-level workflow can generate a consolidated master table from the collected `*_summary.json` files.

Typical outputs:

- `global_results/summaries/master_summary.json`
- `global_results/csv/master_summary.csv`

These master files are intended to support comparison across:

- libraries
- compilers
- server thread counts
- numbers of parallel benchmark clients

They can also be extended to derive higher-level metrics such as:

- throughput per joule
- best library per case
- average behavior by library
- per-case winners for latency, energy, throughput, or efficiency

---

## Merged global reports

The global PDF merge step can combine categorized PDF reports into grouped outputs, for example:

- merged main reports with raw tables
- merged main reports without raw tables
- merged comparison reports with raw tables
- merged comparison reports without raw tables
- one combined global PDF containing all available report categories

This makes the final inspection of results much easier than opening each project report manually.

---

## Cooldown, cache trashing, and warmup phases

The global run script may expose environment-controlled behavior such as:

- cooldown before each project
- cooldown after each project
- best-effort cache trashing
- optional warmup phase
- optional randomized project order
- optional idle measurement at the beginning

This is useful when trying to reduce cross-project interference and improve the consistency of the measurements.

Examples of environment variables that may be supported:

```bash
SETTLE_SECONDS_BEFORE=20
SETTLE_SECONDS_AFTER=20
WARMUP_ENABLED=1
WARMUP_PROJECTS=1
CACHE_TRASH_ENABLED=1
CACHE_TRASH_SIZE_MB=2048
RANDOMIZE_ORDER=0
MERGE_PDFS_AT_END=1
MEASURE_IDLE_AT_START=1
```

A typical usage pattern is:

```bash
sudo SETTLE_SECONDS_BEFORE=20 SETTLE_SECONDS_AFTER=20 ./run.sh
```

---

## Permissions

If the root scripts do not have execution permissions:

```bash
chmod +x build.sh
chmod +x run.sh
```

---

## Relationship with the per-project READMEs

This root README does **not** replace the `README.md` file inside each transport implementation.

Its role is to complement them by explaining:

- how to build all projects from the repository root
- how to run all benchmark campaigns from the repository root
- how global result collection and cross-library comparison work
- how the root-level scripts fit into the full thesis workflow

For transport-specific details, the corresponding subproject README should always be consulted.

---

## Notes

- Each transport project still owns its own compiler setup, ports, server-thread settings, benchmark cases, and result-generation logic.
- The global scripts only coordinate the complete repository-level workflow.
- If project directories are renamed, the `PROJECT_DIRS` arrays in the root scripts must be updated accordingly.
- This design keeps transport implementations decoupled while still enabling repository-wide automation and comparison.

## Acknowledgements

Special thanks to **Jesús Martínez Cruz** and **José Carlos** for their work on the experimental C++ TAPS implementation and for providing the updated TAPS repository used in this benchmark campaign:

- <https://github.com/jmcruz-uma/taps_cpp/tree/main>

Their contribution made it possible to evaluate TAPS under the same raw TCP file-transfer methodology used for the rest of the C++ middleware implementations. The TAPS code used in this context should be understood as experimental and academic work intended to support reproducible evaluation and discussion within the C++ networking community.

---

## Author

**José Antonio García Montañez**