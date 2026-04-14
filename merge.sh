#!/usr/bin/env python3

import argparse
import os
import sys
from pypdf import PdfWriter, PdfReader

def collect_pdfs(input_dir):
    pdfs = []
    for name in sorted(os.listdir(input_dir)):
        if not name.lower().endswith(".pdf"):
            continue
        if name == "benchmark_global_report.pdf":
            continue
        pdfs.append(os.path.join(input_dir, name))
    return pdfs

def merge_pdfs(pdf_paths, output_path):
    writer = PdfWriter()

    for pdf_path in pdf_paths:
        reader = PdfReader(pdf_path)
        for page in reader.pages:
            writer.add_page(page)

    with open(output_path, "wb") as f:
        writer.write(f)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True, help="Directorio con PDFs individuales")
    parser.add_argument("--output", required=True, help="PDF final fusionado")
    args = parser.parse_args()

    if not os.path.isdir(args.input_dir):
        print(f"Error: no existe el directorio {args.input_dir}", file=sys.stderr)
        sys.exit(1)

    pdfs = collect_pdfs(args.input_dir)

    if not pdfs:
        print("No se encontraron PDFs para fusionar.", file=sys.stderr)
        sys.exit(1)

    merge_pdfs(pdfs, args.output)
    print(f"PDF fusionado generado en: {args.output}")

if __name__ == "__main__":
    main()