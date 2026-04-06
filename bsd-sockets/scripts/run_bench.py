#!/usr/bin/env python3

import subprocess
import time
import json
import os
import socket
import math
import csv
import statistics

import matplotlib.pyplot as plt

# CONFIG
BUILD_DIRS = {
    "gcc": "./build-gcc",
    "clang": "./build-clang",
}

PORTS = {
    "gcc": 8080,
    "clang": 8081,
}

COMPILERS = ["gcc", "clang"]

FILE_TO_SERVE = ".././files/1GB.bin"
HOST = "127.0.0.1"

ENERGY_PATH = "/sys/class/powercap/intel-rapl:0/energy_uj"
MAX_ENERGY_PATH = "/sys/class/powercap/intel-rapl:0/max_energy_range_uj"

RESULTS_DIR = "./results"

MACRO_BENCH_CASES = [1, 2, 4, 8, 16]
SERVER_THREADS = [1, 2, 4, 8]
MACRO_REPETITIONS = 1

BENCH_ARGS = [
    "--benchmark_out_format=json"
]

FINAL_RESULTS = os.path.join(RESULTS_DIR, "macro_bench_results.json")
SUMMARY_RESULTS = os.path.join(RESULTS_DIR, "macro_bench_summary.json")
CSV_RESULTS = os.path.join(RESULTS_DIR, "macro_bench_results.csv")


def ensure_results_dir():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    os.chmod(RESULTS_DIR, 0o777)


def read_energy():
    with open(ENERGY_PATH) as f:
        return int(f.read().strip())


def read_max_energy():
    with open(MAX_ENERGY_PATH) as f:
        return int(f.read().strip())


def energy_delta_j(e1, e2):
    if e2 >= e1:
        delta_uj = e2 - e1
    else:
        max_range = read_max_energy()
        delta_uj = (max_range - e1) + e2

    return delta_uj / 1_000_000.0


def get_file_size():
    return os.path.getsize(FILE_TO_SERVE)


def get_server_bin(compiler):
    return os.path.join(BUILD_DIRS[compiler], "tcpserver", "tcpserver")


def get_bench_bin(compiler):
    return os.path.join(BUILD_DIRS[compiler], "benchmarks", "bench_tcp")


def get_server_log_paths(compiler, server_threads):
    stdout_path = os.path.join(
        RESULTS_DIR,
        f"server_{compiler}_threads_{server_threads}_stdout.log"
    )
    stderr_path = os.path.join(
        RESULTS_DIR,
        f"server_{compiler}_threads_{server_threads}_stderr.log"
    )
    return stdout_path, stderr_path


def get_port(compiler):
    return PORTS[compiler]


def is_port_open(host, port, timeout=0.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def start_server(compiler, server_threads):
    server_bin = get_server_bin(compiler)
    port = get_port(compiler)

    if not os.path.exists(server_bin):
        raise FileNotFoundError(f"No existe el binario del servidor para {compiler}: {server_bin}")

    stdout_path, stderr_path = get_server_log_paths(compiler, server_threads)

    stdout_file = open(stdout_path, "w")
    stderr_file = open(stderr_path, "w")

    proc = subprocess.Popen(
        [server_bin, FILE_TO_SERVE, str(port), str(server_threads)],
        stdout=stdout_file,
        stderr=stderr_file,
        text=True
    )

    return proc, stdout_file, stderr_file


def stop_server(proc, stdout_file=None, stderr_file=None):
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    if stdout_file is not None and not stdout_file.closed:
        stdout_file.close()

    if stderr_file is not None and not stderr_file.closed:
        stderr_file.close()

    time.sleep(1.0)


def read_text_file(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except Exception:
        return ""


def wait_for_server(proc, host, port, compiler, server_threads, timeout=60.0):
    start = time.perf_counter()

    while (time.perf_counter() - start) < timeout:
        if proc.poll() is not None:
            stdout_path, stderr_path = get_server_log_paths(compiler, server_threads)
            stdout_text = read_text_file(stdout_path)
            stderr_text = read_text_file(stderr_path)

            raise RuntimeError(
                f"El servidor para {compiler} con {server_threads} hebras terminó antes de estar listo.\n"
                f"Puerto: {port}\n"
                f"stdout log: {stdout_path}\n"
                f"stderr log: {stderr_path}\n"
                f"--- STDOUT ---\n{stdout_text}\n"
                f"--- STDERR ---\n{stderr_text}"
            )

        if is_port_open(host, port):
            return True

        time.sleep(0.2)

    stdout_path, stderr_path = get_server_log_paths(compiler, server_threads)
    stdout_text = read_text_file(stdout_path)
    stderr_text = read_text_file(stderr_path)

    raise RuntimeError(
        f"El servidor para {compiler} con {server_threads} hebras no quedó listo a tiempo.\n"
        f"Puerto: {port}\n"
        f"stdout log: {stdout_path}\n"
        f"stderr log: {stderr_path}\n"
        f"--- STDOUT ---\n{stdout_text}\n"
        f"--- STDERR ---\n{stderr_text}"
    )


def start_bench_instance(compiler, server_threads, case_clients, repetition, index):
    bench_bin = get_bench_bin(compiler)
    port = get_port(compiler)

    if not os.path.exists(bench_bin):
        raise FileNotFoundError(f"No existe el binario del benchmark para {compiler}: {bench_bin}")

    output_json = os.path.join(
        RESULTS_DIR,
        f"micro_{compiler}_threads_{server_threads}_{case_clients}_{repetition}_{index}.json"
    )

    stderr_log = os.path.join(
        RESULTS_DIR,
        f"bench_{compiler}_threads_{server_threads}_{case_clients}_{repetition}_{index}.stderr.log"
    )

    if os.path.exists(output_json):
        os.remove(output_json)

    cmd = (
        [bench_bin]
        + BENCH_ARGS
        + [f"--server_port={port}"]
        + [f"--benchmark_out={output_json}"]
    )

    err_file = open(stderr_log, "w")

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=err_file
    )

    return proc, output_json, err_file


def parse_benchmark_json(path):
    try:
        with open(path) as f:
            data = json.load(f)

        total_iterations = 0

        for entry in data.get("benchmarks", []):
            if entry.get("error_occurred", False):
                return False, 0

            if entry.get("run_type") == "iteration":
                total_iterations += entry.get("iterations", 0)

        if total_iterations <= 0:
            return False, 0

        return True, total_iterations

    except Exception:
        return False, 0


def run_macro_bench_case(compiler, server_threads, num_benches, repetition, file_size_bytes):
    processes = []
    outputs = []

    e1 = read_energy()
    t1 = time.perf_counter()

    for i in range(num_benches):
        proc, out_json, err_file = start_bench_instance(
            compiler,
            server_threads,
            num_benches,
            repetition,
            i
        )
        processes.append((proc, out_json, err_file))
        outputs.append(out_json)

    for proc, _, err_file in processes:
        proc.wait()
        err_file.close()

    t2 = time.perf_counter()
    e2 = read_energy()

    elapsed_s = t2 - t1
    energy_j = energy_delta_j(e1, e2)

    total_iterations = 0
    valid_json_files = []
    success = 0
    failed = 0

    for path in outputs:
        if os.path.exists(path):
            valid, iters = parse_benchmark_json(path)
            if valid:
                total_iterations += iters
                valid_json_files.append(path)
                success += 1
            else:
                failed += 1
        else:
            failed += 1

    real_bytes = total_iterations * file_size_bytes

    throughput_mib_s = (
        real_bytes / (1024 * 1024) / elapsed_s
        if elapsed_s > 0 else 0.0
    )

    return {
        "compiler": compiler,
        "port": get_port(compiler),
        "server_threads": server_threads,
        "parallel_bench_processes": num_benches,
        "repetition": repetition,
        "success": success,
        "failed": failed,
        "total_iterations": total_iterations,
        "downloads_per_process": (total_iterations / success) if success > 0 else 0.0,
        "elapsed_s": elapsed_s,
        "energy_j": energy_j,
        "real_transferred_bytes": real_bytes,
        "throughput_mib_s": throughput_mib_s,
        "benchmark_json_files": valid_json_files
    }


def run_campaign_for_compiler_and_threads(compiler, server_threads):
    file_size_bytes = get_file_size()
    results = []
    port = get_port(compiler)

    print(f"Starting server for {compiler} on port {port} with {server_threads} threads...")
    server, server_stdout, server_stderr = start_server(compiler, server_threads)

    wait_for_server(server, HOST, port, compiler, server_threads)

    try:
        print(f"Running benchmark campaign for {compiler} with {server_threads} server threads...")
        for benches in MACRO_BENCH_CASES:
            for rep in range(MACRO_REPETITIONS):
                print(
                    f"[{compiler}][server_threads={server_threads}] "
                    f"Running {benches} parallel bench_tcp on port {port}..."
                )
                result = run_macro_bench_case(
                    compiler,
                    server_threads,
                    benches,
                    rep,
                    file_size_bytes
                )
                results.append(result)
    finally:
        stop_server(server, server_stdout, server_stderr)

    return results


def percentile(sorted_values, p):
    if not sorted_values:
        return None
    if len(sorted_values) == 1:
        return sorted_values[0]

    k = (len(sorted_values) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)

    if f == c:
        return sorted_values[int(k)]

    d0 = sorted_values[f] * (c - k)
    d1 = sorted_values[c] * (k - f)
    return d0 + d1


def compute_stats(values):
    if not values:
        return None

    sorted_values = sorted(values)

    return {
        "count": len(values),
        "mean": statistics.mean(values),
        "median": statistics.median(values),
        "min": min(values),
        "max": max(values),
        "p5": percentile(sorted_values, 5),
        "p25": percentile(sorted_values, 25),
        "p75": percentile(sorted_values, 75),
        "p95": percentile(sorted_values, 95),
    }


def summarize_results(results):
    grouped = {}

    for item in results:
        key = (
            item["compiler"],
            item["server_threads"],
            item["parallel_bench_processes"],
        )
        grouped.setdefault(key, []).append(item)

    summary = []

    for (compiler, server_threads, parallel_bench_processes), items in sorted(grouped.items()):
        elapsed_values = [x["elapsed_s"] for x in items]
        energy_values = [x["energy_j"] for x in items]
        throughput_values = [x["throughput_mib_s"] for x in items]
        downloads_values = [x["downloads_per_process"] for x in items]
        iterations_values = [x["total_iterations"] for x in items]
        success_values = [x["success"] for x in items]
        failed_values = [x["failed"] for x in items]

        summary.append({
            "compiler": compiler,
            "server_threads": server_threads,
            "parallel_bench_processes": parallel_bench_processes,
            "runs": len(items),
            "elapsed_s_stats": compute_stats(elapsed_values),
            "energy_j_stats": compute_stats(energy_values),
            "throughput_mib_s_stats": compute_stats(throughput_values),
            "downloads_per_process_stats": compute_stats(downloads_values),
            "total_iterations_stats": compute_stats(iterations_values),
            "success_stats": compute_stats(success_values),
            "failed_stats": compute_stats(failed_values),
        })

    return summary


def write_csv(results):
    fieldnames = [
        "compiler",
        "port",
        "server_threads",
        "parallel_bench_processes",
        "repetition",
        "success",
        "failed",
        "total_iterations",
        "downloads_per_process",
        "elapsed_s",
        "energy_j",
        "real_transferred_bytes",
        "throughput_mib_s",
    ]

    with open(CSV_RESULTS, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for row in results:
            writer.writerow({k: row.get(k) for k in fieldnames})


def make_plot_for_metric(results, compiler, metric_key, metric_label, output_name):
    plt.figure(figsize=(10, 6))

    for server_threads in SERVER_THREADS:
        rows = [
            r for r in results
            if r["compiler"] == compiler and r["server_threads"] == server_threads
        ]

        rows.sort(key=lambda x: x["parallel_bench_processes"])

        x = [r["parallel_bench_processes"] for r in rows]
        y = [r[metric_key] for r in rows]

        if x and y:
            plt.plot(x, y, marker="o", label=f"{server_threads} hebras servidor")

    plt.xlabel("Procesos bench_tcp en paralelo")
    plt.ylabel(metric_label)
    plt.title(f"{metric_label} - {compiler}")
    plt.xticks(MACRO_BENCH_CASES)
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, output_name))
    plt.close()


def generate_plots(results):
    for compiler in COMPILERS:
        make_plot_for_metric(
            results,
            compiler,
            "elapsed_s",
            "Tiempo total de campaña (s)",
            f"plot_elapsed_{compiler}.png"
        )

        make_plot_for_metric(
            results,
            compiler,
            "energy_j",
            "Energía total (J)",
            f"plot_energy_{compiler}.png"
        )

        make_plot_for_metric(
            results,
            compiler,
            "throughput_mib_s",
            "Throughput agregado (MiB/s)",
            f"plot_throughput_{compiler}.png"
        )

        make_plot_for_metric(
            results,
            compiler,
            "downloads_per_process",
            "Descargas por proceso",
            f"plot_downloads_per_process_{compiler}.png"
        )


def print_console_summary(summary):
    print("")
    print("==== RESUMEN ESTADISTICO ====")

    for item in summary:
        compiler = item["compiler"]
        server_threads = item["server_threads"]
        benches = item["parallel_bench_processes"]

        elapsed = item["elapsed_s_stats"]
        energy = item["energy_j_stats"]
        throughput = item["throughput_mib_s_stats"]

        print(
            f"[{compiler}] server_threads={server_threads} "
            f"parallel_bench_processes={benches}"
        )
        print(
            f"  tiempo_s: mean={elapsed['mean']:.6f} "
            f"median={elapsed['median']:.6f} "
            f"min={elapsed['min']:.6f} max={elapsed['max']:.6f}"
        )
        print(
            f"  energia_j: mean={energy['mean']:.6f} "
            f"median={energy['median']:.6f} "
            f"min={energy['min']:.6f} max={energy['max']:.6f}"
        )
        print(
            f"  throughput_mib_s: mean={throughput['mean']:.6f} "
            f"median={throughput['median']:.6f} "
            f"min={throughput['min']:.6f} max={throughput['max']:.6f}"
        )
        print("")


def main():
    ensure_results_dir()

    all_results = []

    for compiler in COMPILERS:
        for server_threads in SERVER_THREADS:
            results = run_campaign_for_compiler_and_threads(compiler, server_threads)
            all_results.extend(results)

    final = {
        "file_served": FILE_TO_SERVE,
        "host": HOST,
        "ports": PORTS,
        "cases": MACRO_BENCH_CASES,
        "server_threads": SERVER_THREADS,
        "repetitions": MACRO_REPETITIONS,
        "compilers": COMPILERS,
        "build_dirs": BUILD_DIRS,
        "results": all_results
    }

    with open(FINAL_RESULTS, "w") as f:
        json.dump(final, f, indent=4)

    summary = summarize_results(all_results)

    with open(SUMMARY_RESULTS, "w") as f:
        json.dump({
            "file_served": FILE_TO_SERVE,
            "host": HOST,
            "ports": PORTS,
            "cases": MACRO_BENCH_CASES,
            "server_threads": SERVER_THREADS,
            "repetitions": MACRO_REPETITIONS,
            "compilers": COMPILERS,
            "build_dirs": BUILD_DIRS,
            "summary": summary
        }, f, indent=4)

    write_csv(all_results)
    generate_plots(all_results)
    print_console_summary(summary)

    print("Done.")
    print(f"Raw results: {FINAL_RESULTS}")
    print(f"Summary: {SUMMARY_RESULTS}")
    print(f"CSV: {CSV_RESULTS}")
    print(f"Plots directory: {RESULTS_DIR}")


if __name__ == "__main__":
    main()