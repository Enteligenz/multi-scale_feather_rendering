#!/bin/bash

OUTPUT_DIR="/home/franziska/lightwave-msc-thesis-feathers/tests/owlbear"
OUTPUT_FILE="/home/franziska/lightwave-msc-thesis-feathers/tests/owlbear/owlbear_cub_hd_reference_1024spp.exr"
SCENE="./tests/owlbear/owlbear_baby.xml"
RUNS=3

for i in $(seq 1 $RUNS); do
    echo "--- Run $i ---"
    ./build/unnamed "$SCENE"
    mv "$OUTPUT_FILE" "${OUTPUT_FILE%.*}_${i}.${OUTPUT_FILE##*.}"
done

echo "All $RUNS runs completed."
