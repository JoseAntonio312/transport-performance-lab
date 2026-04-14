#!/usr/bin/env python3

import argparse
import csv
import json
import os
import sys

def flatten_stats(prefix, stats, row):
    if not stats:
        row[f"{prefix}_count"] = None
        row[f"{prefix}_mean"] = None
        row[f"{prefix}_median"] = None
        row[f"{prefix}_stdev"] = None
        row[f"{prefix}_p25"] = None
        row[f"{prefix}_p50"] = None
        row[f"{prefix}_p95"] = None
        row[f"{prefix}_min"] = None
        row[f"{prefix}_max"] = None
        return

    row[f"{prefix}_count"] = stats.get("count")
    row[f"{prefix}_mean"] = stats.get("mean")
    row[f"{prefix}_median"] = stats.get("median")
    row[f"{prefix}_stdev"] = stats.get("stdev")
    row[f"{prefix}_p25"] = stats.get("p25")
    row[f"{prefix}_p50"] = stats.get("p50")
    row[f"{prefix}_p95"] = stats.get("p95")
    row[f"{prefix}_min"] = stats.get("min")
    row[f"{prefix}_max"] = stats.get("max")

def collect_summary_files(input_dir):
    files = []
    for name in sorted(os.listdir(input_dir)):
        if name.endswith("_summary.json"):
            files.append(os.path.join(input_dir, name))
    return files

def build_master_rows(summary_files):
    rows = []

    for path in summary_files:
        lib_name = os.path.basename(path).replace("_summary.json", "")

        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)

        for item in data.get("summary", []):
            row = {
                "library": lib_name,
                "compiler": item.get("compiler"),
                "server_threads": item.get("server_threads"),
                "parallel_bench_processes": item.get("parallel_bench_processes"),
                "runs": item.get("runs"),
            }

            flatten_stats("elapsed_s", item.get("elapsed_s_stats"), row)
            flatten_stats("energy_j", item.get("energy_j_stats"), row)
            flatten_stats("throughput_mib_s", item.get("throughput_mib_s_stats"), row)
            flatten_stats("downloads_per_process", item.get("downloads_per_process_stats"), row)
            flatten_stats("total_iterations", item.get("total_iterations_stats"), row)
            flatten_stats("success", item.get("success_stats"), row)
            flatten_stats("failed", item.get("failed_stats"), row)

            mean_energy = row.get("energy_j_mean")
            mean_throughput = row.get("throughput_mib_s_mean")
            row["throughput_per_joule"] = (
                mean_throughput / mean_energy
                if mean_energy not in (None, 0) and mean_throughput is not None
                else None
            )

            rows.append(row)

    return rows

def write_json(rows, output_path):
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump({"rows": rows}, f, indent=4)

def write_csv(rows, output_path):
    if not rows:
        with open(output_path, "w", newline="", encoding="utf-8") as f:
            pass
        return

    fieldnames = list(rows[0].keys())

    with open(output_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True)
    parser.add_argument("--json-out", required=True)
    parser.add_argument("--csv-out", required=True)
    args = parser.parse_args()

    if not os.path.isdir(args.input_dir):
        print(f"Error: no existe el directorio {args.input_dir}", file=sys.stderr)
        sys.exit(1)

    summary_files = collect_summary_files(args.input_dir)
    if not summary_files:
        print("No se encontraron ficheros *_summary.json", file=sys.stderr)
        sys.exit(1)

    rows = build_master_rows(summary_files)
    write_json(rows, args.json_out)
    write_csv(rows, args.csv_out)

    print(f"Master JSON generado en: {args.json_out}")
    print(f"Master CSV generado en: {args.csv_out}")

if __name__ == "__main__":
    main()