#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLOBAL_RESULTS_DIR="$ROOT_DIR/global_results"

ORIGINAL_SUMMARIES_DIR="$GLOBAL_RESULTS_DIR/summaries"
FILTERED_SUMMARIES_DIR="$GLOBAL_RESULTS_DIR/summaries_no_taps"

CSV_DIR="$GLOBAL_RESULTS_DIR/csv"
REPORTS_DIR="$GLOBAL_RESULTS_DIR/reports"
GLOBAL_REPORTS_DIR="$REPORTS_DIR/global_no_taps"
MERGED_REPORTS_DIR="$REPORTS_DIR/merged_no_taps"
PLOTS_DIR="$GLOBAL_RESULTS_DIR/plots/global_no_taps"

MASTER_JSON="$FILTERED_SUMMARIES_DIR/master_summary_no_taps.json"
MASTER_CSV="$CSV_DIR/master_summary_no_taps.csv"
MASTER_PDF="$GLOBAL_REPORTS_DIR/global_master_comparison_report_no_taps.pdf"

mkdir -p \
  "$FILTERED_SUMMARIES_DIR" \
  "$CSV_DIR" \
  "$GLOBAL_REPORTS_DIR" \
  "$MERGED_REPORTS_DIR" \
  "$PLOTS_DIR"

rm -f "$FILTERED_SUMMARIES_DIR"/*_summary.json
rm -f "$PLOTS_DIR"/*.png
rm -f "$MASTER_JSON" "$MASTER_CSV" "$MASTER_PDF"

echo "[1/3] Copying existing summaries except TAPS..."

find "$ORIGINAL_SUMMARIES_DIR" -maxdepth 1 -type f -name "*_summary.json" \
  ! -name "taps-asio_summary.json" \
  ! -name "taps_asio_summary.json" \
  ! -name "*taps*summary.json" \
  -exec cp {} "$FILTERED_SUMMARIES_DIR/" \;

echo "[2/3] Generating global metrics without TAPS..."

python3 "$ROOT_DIR/build_master_summary.py" \
  --input-dir "$FILTERED_SUMMARIES_DIR" \
  --json-out "$MASTER_JSON" \
  --csv-out "$MASTER_CSV" \
  --pdf-out "$MASTER_PDF" \
  --plots-dir "$PLOTS_DIR"

echo "[3/3] Merging existing PDFs without TAPS..."

ROOT_DIR="$ROOT_DIR" python3 - <<'PY'
import os
import sys
from pypdf import PdfReader, PdfWriter

root = os.environ["ROOT_DIR"]
reports_dir = os.path.join(root, "global_results", "reports")
out_dir = os.path.join(reports_dir, "merged_no_taps")
os.makedirs(out_dir, exist_ok=True)

skip_terms = ("taps", "taps-asio", "taps_asio")
groups = {}

for current, dirs, files in os.walk(reports_dir):
    rel_parts = os.path.relpath(current, reports_dir).split(os.sep)

    if any(part.startswith("merged") for part in rel_parts):
        dirs[:] = []
        continue

    for name in sorted(files):
        if not name.lower().endswith(".pdf"):
            continue

        low = name.lower()

        if any(term in low for term in skip_terms):
            continue

        rel = os.path.relpath(current, reports_dir)
        group = "misc" if rel == "." else rel.replace(os.sep, "_")
        groups.setdefault(group, []).append(os.path.join(current, name))

def merge(paths, output):
    writer = PdfWriter()

    for path in paths:
        try:
            reader = PdfReader(path)
            for page in reader.pages:
                writer.add_page(page)
        except Exception as exc:
            print(f"WARN: could not read {path}: {exc}", file=sys.stderr)

    if writer.pages:
        with open(output, "wb") as f:
            writer.write(f)
        print(f"Merged: {output}")

all_paths = []

for group, paths in sorted(groups.items()):
    if not paths:
        continue

    all_paths.extend(paths)
    merge(paths, os.path.join(out_dir, f"{group}_no_taps_merged.pdf"))

merge(all_paths, os.path.join(out_dir, "benchmark_global_report_all_no_taps.pdf"))
PY

echo "Done without TAPS."
echo "Master JSON: $MASTER_JSON"
echo "Master CSV:  $MASTER_CSV"
echo "Master PDF:  $MASTER_PDF"
echo "Merged PDFs: $MERGED_REPORTS_DIR"