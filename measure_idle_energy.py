#!/usr/bin/env python3

import json
import statistics
import time
from pathlib import Path

ENERGY_PATH = "/sys/class/powercap/intel-rapl:0/energy_uj"
MAX_ENERGY_PATH = "/sys/class/powercap/intel-rapl:0/max_energy_range_uj"

SAMPLE_SECONDS = 60
REPETITIONS = 5

ROOT_DIR = Path(__file__).resolve().parent
OUTPUT_JSON = ROOT_DIR / "idle_baseline.json"


def read_energy():
    with open(ENERGY_PATH, "r", encoding="utf-8") as f:
        return int(f.read().strip())


def read_max_energy():
    with open(MAX_ENERGY_PATH, "r", encoding="utf-8") as f:
        return int(f.read().strip())


def energy_delta_j(e1, e2):
    if e2 >= e1:
        delta_uj = e2 - e1
    else:
        max_range = read_max_energy()
        delta_uj = (max_range - e1) + e2
    return delta_uj / 1_000_000.0


def main():
    joules = []
    watts = []

    print(f"Measuring idle power consumption: {REPETITIONS} repetitions of {SAMPLE_SECONDS} s")
    print("Leave the system idle, with no benchmarks or extra workload.\n")

    for i in range(REPETITIONS):
        print(f"Repetition {i + 1}/{REPETITIONS}...")
        e1 = read_energy()
        time.sleep(SAMPLE_SECONDS)
        e2 = read_energy()

        j = energy_delta_j(e1, e2)
        w = j / SAMPLE_SECONDS

        joules.append(j)
        watts.append(w)

        print(f"  Joules: {j:.6f} J")
        print(f"  Watts: {w:.6f} W\n")

    result = {
        "sample_seconds": SAMPLE_SECONDS,
        "repetitions": REPETITIONS,
        "joules_mean": statistics.mean(joules),
        "joules_median": statistics.median(joules),
        "watts_mean": statistics.mean(watts),
        "watts_median": statistics.median(watts),
        "joules_samples": joules,
        "watts_samples": watts,
    }

    with open(OUTPUT_JSON, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=4)

    print("==== SUMMARY ====")
    print(f"Mean joules over {SAMPLE_SECONDS} s: {result['joules_mean']:.6f} J")
    print(f"Median joules over {SAMPLE_SECONDS} s: {result['joules_median']:.6f} J")
    print(f"Mean idle power: {result['watts_mean']:.6f} W")
    print(f"Median idle power: {result['watts_median']:.6f} W")
    print(f"\nSaved to: {OUTPUT_JSON}")


if __name__ == "__main__":
    main()