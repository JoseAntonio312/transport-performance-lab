#!/usr/bin/env python3

import argparse
import os
import sys
from collections import defaultdict

from pypdf import PdfReader, PdfWriter


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def collect_pdf_groups(input_dir):
    """
    Expected layout under input_dir:

    reports/
      main_with_raw/
      main_without_raw/
      comparison_with_raw/
      comparison_without_raw/
      per_library/
    """

    groups = defaultdict(list)

    for root, _, files in os.walk(input_dir):
        for name in sorted(files):
            if not name.lower().endswith(".pdf"):
                continue

            full_path = os.path.join(root, name)
            rel_dir = os.path.relpath(root, input_dir)

            if rel_dir == ".":
                group_name = "misc"
            else:
                group_name = rel_dir.replace(os.sep, "__")

            groups[group_name].append(full_path)

    return dict(groups)


def merge_pdfs(pdf_paths, output_path):
    writer = PdfWriter()

    for pdf_path in pdf_paths:
        try:
            reader = PdfReader(pdf_path)
            for page in reader.pages:
                writer.add_page(page)
        except Exception as exc:
            print(f"WARNING: failed to read {pdf_path}: {exc}", file=sys.stderr)

    if not writer.pages:
        return False

    with open(output_path, "wb") as f:
        writer.write(f)

    return True


def merge_all_combined(groups, output_path):
    ordered_paths = []

    preferred_order = [
        "main_without_raw",
        "comparison_without_raw",
        "main_with_raw",
        "comparison_with_raw",
        "per_library",
        "misc",
    ]

    used = set()

    for key in preferred_order:
        for real_key in groups:
            if real_key.endswith(key) and real_key not in used:
                ordered_paths.extend(groups[real_key])
                used.add(real_key)

    for real_key in sorted(groups):
        if real_key not in used:
            ordered_paths.extend(groups[real_key])

    return merge_pdfs(ordered_paths, output_path)


def main():
    parser = argparse.ArgumentParser(description="Merge benchmark report PDFs by category.")
    parser.add_argument("--input-dir", required=True, help="Directory containing categorized PDF reports.")
    parser.add_argument("--output-dir", required=True, help="Directory where merged PDFs will be written.")
    args = parser.parse_args()

    if not os.path.isdir(args.input_dir):
        print(f"Error: input directory does not exist: {args.input_dir}", file=sys.stderr)
        sys.exit(1)

    ensure_dir(args.output_dir)

    groups = collect_pdf_groups(args.input_dir)
    if not groups:
        print("No PDFs were found to merge.", file=sys.stderr)
        sys.exit(1)

    merged_any = False

    for group_name, pdfs in sorted(groups.items()):
        if not pdfs:
            continue

        output_name = f"{group_name.replace('__', '_')}_merged.pdf"
        output_path = os.path.join(args.output_dir, output_name)

        if merge_pdfs(pdfs, output_path):
            print(f"Merged PDF written to: {output_path}")
            merged_any = True

    combined_output = os.path.join(args.output_dir, "benchmark_global_report_all.pdf")
    if merge_all_combined(groups, combined_output):
        print(f"Combined global PDF written to: {combined_output}")
        merged_any = True

    if not merged_any:
        print("No valid PDFs could be merged.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()