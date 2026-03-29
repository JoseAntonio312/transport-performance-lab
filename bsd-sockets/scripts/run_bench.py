#!/usr/bin/env python3

import subprocess
import time
import json
import os
import socket

# CONFIG
SERVER_BIN = "./build/tcpserver/tcpserver"
BENCH_BIN = "./build/benchmarks/bench_tcp"
FILE_TO_SERVE = "/home/jagarcia/Escritorio/TFM/files/1GB.bin"
PORT = 8080
HOST = "127.0.0.1"

ENERGY_PATH = "/sys/class/powercap/intel-rapl:0/energy_uj"
MAX_ENERGY_PATH = "/sys/class/powercap/intel-rapl:0/max_energy_range_uj"

RESULTS_DIR = "./results"

MACRO_BENCH_CASES = [1, 2, 4, 8, 16] #Adjusts the type and the quantity of the clients
MACRO_REPETITIONS = 1 #Adjust for the quality of the measurements

BENCH_ARGS = [
    "--benchmark_min_time=1s",
    "--benchmark_repetitions=1",
    "--benchmark_out_format=json"
]

FINAL_RESULTS = os.path.join(RESULTS_DIR, "macro_bench_results.json")

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
    # Correccion wrap-around
    if e2 >= e1:
        delta_uj = e2 - e1
    else:
        max_range = read_max_energy()
        delta_uj = (max_range - e1) + e2

    return delta_uj / 1_000_000.0


def get_file_size():
    return os.path.getsize(FILE_TO_SERVE)


def start_server():
    return subprocess.Popen(
        [SERVER_BIN, FILE_TO_SERVE, str(PORT)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )


def stop_server(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def wait_for_server(host, port, timeout=60.0):
    start = time.perf_counter()

    while (time.perf_counter() - start) < timeout:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return True
        except OSError:
            time.sleep(0.2)

    return False


def start_bench_instance(case_clients, repetition, index):
    output_json = os.path.join(
        RESULTS_DIR,
        f"micro_{case_clients}_{repetition}_{index}.json"
    )

    # Borrar JSON anterior si existe
    if os.path.exists(output_json):
        os.remove(output_json)

    cmd = [BENCH_BIN] + BENCH_ARGS + [f"--benchmark_out={output_json}"]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    return proc, output_json


def parse_benchmark_json(path):
    try:
        with open(path) as f:
            data = json.load(f)

        total_iterations = 0

        for entry in data.get("benchmarks", []):
            # Si Google Benchmark marca error, esta ejecucion no vale
            if entry.get("error_occurred", False):
                return False, 0

            if entry.get("run_type") == "iteration":
                total_iterations += entry.get("iterations", 0)

        if total_iterations <= 0:
            return False, 0

        return True, total_iterations

    except Exception:
        return False, 0


def run_macro_bench_case(num_benches, repetition, file_size_bytes):
    processes = []
    outputs = []

    e1 = read_energy()
    t1 = time.perf_counter()

    for i in range(num_benches):
        proc, out_json = start_bench_instance(num_benches, repetition, i)
        processes.append((proc, out_json))
        outputs.append(out_json)

    for proc, _ in processes:
        proc.wait()

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
        "parallel_bench_processes": num_benches,
        "repetition": repetition,
        "success": success,
        "failed": failed,
        "elapsed_s": elapsed_s,
        "energy_j": energy_j,
        "total_iterations": total_iterations,
        "real_transferred_bytes": real_bytes,
        "throughput_mib_s": throughput_mib_s,
        "benchmark_json_files": valid_json_files
    }


def run_campaign():
    file_size_bytes = get_file_size()
    results = []

    for benches in MACRO_BENCH_CASES:
        for rep in range(MACRO_REPETITIONS):
            print(f"Running {benches} parallel bench_tcp...")
            result = run_macro_bench_case(benches, rep, file_size_bytes)
            results.append(result)

    return results


def main():
    ensure_results_dir()

    print("Starting server...")
    server = start_server()

    if not wait_for_server(HOST, PORT):
        stop_server(server)
        raise RuntimeError("Server did not become ready in time.")

    print("Running benchmark campaign...")
    results = run_campaign()

    stop_server(server)

    final = {
        "server_bin": SERVER_BIN,
        "bench_bin": BENCH_BIN,
        "file_served": FILE_TO_SERVE,
        "port": PORT,
        "cases": MACRO_BENCH_CASES,
        "repetitions": MACRO_REPETITIONS,
        "results": results
    }

    with open(FINAL_RESULTS, "w") as f:
        json.dump(final, f, indent=4)

    print("Done.")
    print(f"Results saved in: {RESULTS_DIR}")


if __name__ == "__main__":
    main()