#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <input.pdf>"
    exit 1
fi

INPUT_PDF=$1
OUTPUT_BASE="output/bench_out"

mkdir -p output

echo "========================================="
echo " Parallel PDF Extractor Benchmark Script "
echo "========================================="
echo "Input file: $INPUT_PDF"
echo ""

# Make sure it's compiled
make clean && make

echo ""
echo "Running Benchmarks..."
./pdf_extractor "$INPUT_PDF" "$OUTPUT_BASE" --mode benchmark
