#! /usr/bin/env python

"""
compare_images.py  —  Pairwise relMSE comparison of EXR images.
Expects compute_mse.py to sit in the same directory.

Usage (command line):
    python compare_images.py <image1.exr> <image2.exr> [image3.exr ...] [-o output.csv]

Usage (run list, recommended for large batches):
    Edit the RUNS list at the bottom of this file, then:
    python compare_images.py

Each entry in RUNS is a dict with:
    "reference"  — path to the reference (ground-truth) image
    "candidate"  — path to the candidate image to compare against it
    "label"      — (optional) human-readable label written into the CSV

Columns in the output CSV:
    label         — from the run entry, or auto-generated if absent
    reference     — path of the reference image
    candidate     — path of the candidate image
    relMSE        — mean relMSE over all pixels
    relMSE*       — relMSE with the top 0.001 % of pixels ignored
    relMSE**      — relMSE with the top 0.01 % of pixels ignored
"""

import argparse
import csv
import itertools
import subprocess
import sys
from pathlib import Path


COMPUTE_MSE_SCRIPT = Path(__file__).parent / "compute_mse.py"
CSV_FIELDNAMES = ["label", "reference", "candidate", "relMSE", "relMSE*", "relMSE**"]

# ---------------------------------------------------------------------------
# Run list — edit this when running as a script rather than from the CLI.
# Each dict must have "reference" and "candidate"; "label" is optional.
# ---------------------------------------------------------------------------
OUTPUT_CSV = "mse_results_penguin.csv"

RUNS = [
    # {"reference": "path/to/ground_truth.exr", "candidate": "path/to/render_a.exr", "label": "Method A 64spp"},
    # {"reference": "path/to/ground_truth.exr", "candidate": "path/to/render_b.exr", "label": "Method B 64spp"},
    # {"reference": "path/to/ground_truth.exr", "candidate": "path/to/render_b.exr"},  # label is optional

    # NOTE Make sure to change the bias of the dummy data point (HD Bias) to Epsilon (1e-8) before rendering the scatterplot!

    # ---------------------------------------------------------------------------
    # Single Bias
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_64spp_seed_1.exr", "label": "Bias Only HD"}, # dummy data
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_64spp_seed_2.exr", "label": "Bias Only HD"}, # dummy data
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_64spp_seed_3.exr", "label": "Bias Only HD"}, # dummy data
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_64spp_seed_1.exr", "label": "Bias Bounce 2"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_64spp_seed_2.exr", "label": "Bias Bounce 2"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_64spp_seed_3.exr", "label": "Bias Bounce 2"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_64spp_seed_1.exr", "label": "Bias Bounce 3"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_64spp_seed_2.exr", "label": "Bias Bounce 3"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_64spp_seed_3.exr", "label": "Bias Bounce 3"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_64spp_seed_1.exr", "label": "Bias Bounce 4"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_64spp_seed_2.exr", "label": "Bias Bounce 4"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_64spp_seed_3.exr", "label": "Bias Bounce 4"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_64spp_seed_1.exr", "label": "Bias Bounce 5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_64spp_seed_2.exr", "label": "Bias Bounce 5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_64spp_seed_3.exr", "label": "Bias Bounce 5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_64spp_seed_1.exr", "label": "Bias Distance 0.01"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_64spp_seed_2.exr", "label": "Bias Distance 0.01"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_64spp_seed_3.exr", "label": "Bias Distance 0.01"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_64spp_seed_1.exr", "label": "Bias Distance 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_64spp_seed_2.exr", "label": "Bias Distance 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_64spp_seed_3.exr", "label": "Bias Distance 1.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_64spp_seed_1.exr", "label": "Bias Distance 10.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_64spp_seed_2.exr", "label": "Bias Distance 10.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_64spp_seed_3.exr", "label": "Bias Distance 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_64spp_seed_1.exr", "label": "Bias Distance 100.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_64spp_seed_2.exr", "label": "Bias Distance 100.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_64spp_seed_3.exr", "label": "Bias Distance 100.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_64spp_seed_1.exr", "label": "Bias Distance 1000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_64spp_seed_2.exr", "label": "Bias Distance 1000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_64spp_seed_3.exr", "label": "Bias Distance 1000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_64spp_seed_1.exr", "label": "Bias Distance 3000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_64spp_seed_2.exr", "label": "Bias Distance 3000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_64spp_seed_3.exr", "label": "Bias Distance 3000.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_64spp_seed_1.exr", "label": "Bias Only LD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_64spp_seed_2.exr", "label": "Bias Only LD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_64spp_seed_3.exr", "label": "Bias Only LD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_64spp_seed_1.exr", "label": "Bias PDF 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_64spp_seed_2.exr", "label": "Bias PDF 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_64spp_seed_3.exr", "label": "Bias PDF 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_64spp_seed_1.exr", "label": "Bias PDF 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_64spp_seed_2.exr", "label": "Bias PDF 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_64spp_seed_3.exr", "label": "Bias PDF 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_64spp_seed_1.exr", "label": "Bias PDF 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_64spp_seed_2.exr", "label": "Bias PDF 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_64spp_seed_3.exr", "label": "Bias PDF 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_64spp_seed_1.exr", "label": "Bias PDF 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_64spp_seed_2.exr", "label": "Bias PDF 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_64spp_seed_3.exr", "label": "Bias PDF 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_64spp_seed_1.exr", "label": "Bias PDF 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_64spp_seed_2.exr", "label": "Bias PDF 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_64spp_seed_3.exr", "label": "Bias PDF 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_64spp_seed_1.exr", "label": "Bias PDF 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_64spp_seed_2.exr", "label": "Bias PDF 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_64spp_seed_3.exr", "label": "Bias PDF 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_64spp_seed_1.exr", "label": "Bias Combined 0.0001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_64spp_seed_2.exr", "label": "Bias Combined 0.0001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_64spp_seed_3.exr", "label": "Bias Combined 0.0001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_64spp_seed_1.exr", "label": "Bias Combined 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_64spp_seed_2.exr", "label": "Bias Combined 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_64spp_seed_3.exr", "label": "Bias Combined 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_64spp_seed_1.exr", "label": "Bias Combined 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_64spp_seed_2.exr", "label": "Bias Combined 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_64spp_seed_3.exr", "label": "Bias Combined 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_64spp_seed_1.exr", "label": "Bias Combined 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_64spp_seed_2.exr", "label": "Bias Combined 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_64spp_seed_3.exr", "label": "Bias Combined 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_64spp_seed_1.exr", "label": "Bias Combined 0.95"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_64spp_seed_2.exr", "label": "Bias Combined 0.95"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_64spp_seed_3.exr", "label": "Bias Combined 0.95"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_64spp_seed_1.exr", "label": "Bias Combined 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_64spp_seed_2.exr", "label": "Bias Combined 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_64spp_seed_3.exr", "label": "Bias Combined 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_64spp_seed_1.exr", "label": "Bias CombinedLessDist 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_64spp_seed_2.exr", "label": "Bias CombinedLessDist 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_64spp_seed_3.exr", "label": "Bias CombinedLessDist 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_64spp_seed_1.exr", "label": "Bias CombinedLessDist 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_64spp_seed_2.exr", "label": "Bias CombinedLessDist 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_64spp_seed_3.exr", "label": "Bias CombinedLessDist 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_64spp_seed_1.exr", "label": "Bias CombinedLessDist 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_64spp_seed_2.exr", "label": "Bias CombinedLessDist 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_64spp_seed_3.exr", "label": "Bias CombinedLessDist 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_64spp_seed_1.exr", "label": "Bias CombinedLessDist 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_64spp_seed_2.exr", "label": "Bias CombinedLessDist 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_64spp_seed_3.exr", "label": "Bias CombinedLessDist 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_64spp_seed_1.exr", "label": "Bias CombinedLessDist 2.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_64spp_seed_2.exr", "label": "Bias CombinedLessDist 2.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_64spp_seed_3.exr", "label": "Bias CombinedLessDist 2.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_64spp_seed_1.exr", "label": "Bias CombinedNew 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_64spp_seed_2.exr", "label": "Bias CombinedNew 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_64spp_seed_3.exr", "label": "Bias CombinedNew 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_64spp_seed_1.exr", "label": "Bias CombinedNew 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_64spp_seed_2.exr", "label": "Bias CombinedNew 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_64spp_seed_3.exr", "label": "Bias CombinedNew 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_64spp_seed_1.exr", "label": "Bias CombinedNew 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_64spp_seed_2.exr", "label": "Bias CombinedNew 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_64spp_seed_3.exr", "label": "Bias CombinedNew 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_64spp_seed_1.exr", "label": "Bias CombinedNew 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_64spp_seed_2.exr", "label": "Bias CombinedNew 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_64spp_seed_3.exr", "label": "Bias CombinedNew 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_64spp_seed_1.exr", "label": "Bias CombinedNew 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_64spp_seed_2.exr", "label": "Bias CombinedNew 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_64spp_seed_3.exr", "label": "Bias CombinedNew 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_64spp_seed_1.exr", "label": "Bias CombinedNew 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_64spp_seed_2.exr", "label": "Bias CombinedNew 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_64spp_seed_3.exr", "label": "Bias CombinedNew 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_64spp_seed_1.exr", "label": "Bias Footprint 0.000000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_64spp_seed_2.exr", "label": "Bias Footprint 0.000000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_64spp_seed_3.exr", "label": "Bias Footprint 0.000000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_64spp_seed_1.exr", "label": "Bias Footprint 0.0000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_64spp_seed_2.exr", "label": "Bias Footprint 0.0000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_64spp_seed_3.exr", "label": "Bias Footprint 0.0000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_64spp_seed_1.exr", "label": "Bias Footprint 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_64spp_seed_2.exr", "label": "Bias Footprint 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_64spp_seed_3.exr", "label": "Bias Footprint 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_64spp_seed_1.exr", "label": "Bias Footprint 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_64spp_seed_2.exr", "label": "Bias Footprint 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_64spp_seed_3.exr", "label": "Bias Footprint 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_64spp_seed_1.exr", "label": "Bias Footprint 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_64spp_seed_2.exr", "label": "Bias Footprint 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_64spp_seed_3.exr", "label": "Bias Footprint 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_64spp_seed_1.exr", "label": "Bias Footprint 1e-12"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_64spp_seed_2.exr", "label": "Bias Footprint 1e-12"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_64spp_seed_3.exr", "label": "Bias Footprint 1e-12"},
    # Single MSE for Efficiency
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_64spp_seed_1.exr", "label": "MSE for Eff. Only HD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_64spp_seed_2.exr", "label": "MSE for Eff. Only HD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_1024spp.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_HD_64spp_seed_3.exr", "label": "MSE for Eff. Only HD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_64spp_seed_1.exr", "label": "MSE for Eff. Bounce 2"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_64spp_seed_2.exr", "label": "MSE for Eff. Bounce 2"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_2_64spp_seed_3.exr", "label": "MSE for Eff. Bounce 2"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_64spp_seed_1.exr", "label": "MSE for Eff. Bounce 3"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_64spp_seed_2.exr", "label": "MSE for Eff. Bounce 3"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_3_64spp_seed_3.exr", "label": "MSE for Eff. Bounce 3"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_64spp_seed_1.exr", "label": "MSE for Eff. Bounce 4"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_64spp_seed_2.exr", "label": "MSE for Eff. Bounce 4"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_4_64spp_seed_3.exr", "label": "MSE for Eff. Bounce 4"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_64spp_seed_1.exr", "label": "MSE for Eff. Bounce 5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_64spp_seed_2.exr", "label": "MSE for Eff. Bounce 5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_bounce_5_64spp_seed_3.exr", "label": "MSE for Eff. Bounce 5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_64spp_seed_1.exr", "label": "MSE for Eff. Distance 0.01"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_64spp_seed_2.exr", "label": "MSE for Eff. Distance 0.01"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_0.01_64spp_seed_3.exr", "label": "MSE for Eff. Distance 0.01"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_64spp_seed_1.exr", "label": "MSE for Eff. Distance 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_64spp_seed_2.exr", "label": "MSE for Eff. Distance 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1_64spp_seed_3.exr", "label": "MSE for Eff. Distance 1.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_64spp_seed_1.exr", "label": "MSE for Eff. Distance 10.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_64spp_seed_2.exr", "label": "MSE for Eff. Distance 10.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_10_64spp_seed_3.exr", "label": "MSE for Eff. Distance 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_64spp_seed_1.exr", "label": "MSE for Eff. Distance 100.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_64spp_seed_2.exr", "label": "MSE for Eff. Distance 100.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_100_64spp_seed_3.exr", "label": "MSE for Eff. Distance 100.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_64spp_seed_1.exr", "label": "MSE for Eff. Distance 1000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_64spp_seed_2.exr", "label": "MSE for Eff. Distance 1000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_1000_64spp_seed_3.exr", "label": "MSE for Eff. Distance 1000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_64spp_seed_1.exr", "label": "MSE for Eff. Distance 3000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_64spp_seed_2.exr", "label": "MSE for Eff. Distance 3000.0"},
    # {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_distance_3000_64spp_seed_3.exr", "label": "MSE for Eff. Distance 3000.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_64spp_seed_1.exr", "label": "MSE for Eff. Only LD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_64spp_seed_2.exr", "label": "MSE for Eff. Only LD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_LD_64spp_seed_3.exr", "label": "MSE for Eff. Only LD"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_64spp_seed_1.exr", "label": "MSE for Eff. PDF 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_64spp_seed_2.exr", "label": "MSE for Eff. PDF 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.1_64spp_seed_3.exr", "label": "MSE for Eff. PDF 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_64spp_seed_1.exr", "label": "MSE for Eff. PDF 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_64spp_seed_2.exr", "label": "MSE for Eff. PDF 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.5_64spp_seed_3.exr", "label": "MSE for Eff. PDF 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_64spp_seed_1.exr", "label": "MSE for Eff. PDF 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_64spp_seed_2.exr", "label": "MSE for Eff. PDF 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_0.9_64spp_seed_3.exr", "label": "MSE for Eff. PDF 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_64spp_seed_1.exr", "label": "MSE for Eff. PDF 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_64spp_seed_2.exr", "label": "MSE for Eff. PDF 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1.5_64spp_seed_3.exr", "label": "MSE for Eff. PDF 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_64spp_seed_1.exr", "label": "MSE for Eff. PDF 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_64spp_seed_2.exr", "label": "MSE for Eff. PDF 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_1_64spp_seed_3.exr", "label": "MSE for Eff. PDF 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_64spp_seed_1.exr", "label": "MSE for Eff. PDF 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_64spp_seed_2.exr", "label": "MSE for Eff. PDF 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_PDF_3_64spp_seed_3.exr", "label": "MSE for Eff. PDF 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_64spp_seed_1.exr", "label": "MSE for Eff. Combined 0.0001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_64spp_seed_2.exr", "label": "MSE for Eff. Combined 0.0001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.0001_64spp_seed_3.exr", "label": "MSE for Eff. Combined 0.0001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_64spp_seed_1.exr", "label": "MSE for Eff. Combined 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_64spp_seed_2.exr", "label": "MSE for Eff. Combined 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.1_64spp_seed_3.exr", "label": "MSE for Eff. Combined 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_64spp_seed_1.exr", "label": "MSE for Eff. Combined 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_64spp_seed_2.exr", "label": "MSE for Eff. Combined 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.5_64spp_seed_3.exr", "label": "MSE for Eff. Combined 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_64spp_seed_1.exr", "label": "MSE for Eff. Combined 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_64spp_seed_2.exr", "label": "MSE for Eff. Combined 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.9_64spp_seed_3.exr", "label": "MSE for Eff. Combined 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_64spp_seed_1.exr", "label": "MSE for Eff. Combined 0.95"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_64spp_seed_2.exr", "label": "MSE for Eff. Combined 0.95"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_0.95_64spp_seed_3.exr", "label": "MSE for Eff. Combined 0.95"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_64spp_seed_1.exr", "label": "MSE for Eff. Combined 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_64spp_seed_2.exr", "label": "MSE for Eff. Combined 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combined_1.5_64spp_seed_3.exr", "label": "MSE for Eff. Combined 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_64spp_seed_1.exr", "label": "MSE for Eff. CombinedLessDist 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_64spp_seed_2.exr", "label": "MSE for Eff. CombinedLessDist 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.1_64spp_seed_3.exr", "label": "MSE for Eff. CombinedLessDist 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_64spp_seed_1.exr", "label": "MSE for Eff. CombinedLessDist 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_64spp_seed_2.exr", "label": "MSE for Eff. CombinedLessDist 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.5_64spp_seed_3.exr", "label": "MSE for Eff. CombinedLessDist 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_64spp_seed_1.exr", "label": "MSE for Eff. CombinedLessDist 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_64spp_seed_2.exr", "label": "MSE for Eff. CombinedLessDist 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_0.9_64spp_seed_3.exr", "label": "MSE for Eff. CombinedLessDist 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_64spp_seed_1.exr", "label": "MSE for Eff. CombinedLessDist 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_64spp_seed_2.exr", "label": "MSE for Eff. CombinedLessDist 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_1.5_64spp_seed_3.exr", "label": "MSE for Eff. CombinedLessDist 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_64spp_seed_1.exr", "label": "MSE for Eff. CombinedLessDist 2.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_64spp_seed_2.exr", "label": "MSE for Eff. CombinedLessDist 2.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedLessDist_2_64spp_seed_3.exr", "label": "MSE for Eff. CombinedLessDist 2.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_64spp_seed_1.exr", "label": "MSE for Eff. CombinedNew 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_64spp_seed_2.exr", "label": "MSE for Eff. CombinedNew 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.1_64spp_seed_3.exr", "label": "MSE for Eff. CombinedNew 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_64spp_seed_1.exr", "label": "MSE for Eff. CombinedNew 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_64spp_seed_2.exr", "label": "MSE for Eff. CombinedNew 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.5_64spp_seed_3.exr", "label": "MSE for Eff. CombinedNew 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_64spp_seed_1.exr", "label": "MSE for Eff. CombinedNew 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_64spp_seed_2.exr", "label": "MSE for Eff. CombinedNew 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_0.9_64spp_seed_3.exr", "label": "MSE for Eff. CombinedNew 0.9"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_64spp_seed_1.exr", "label": "MSE for Eff. CombinedNew 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_64spp_seed_2.exr", "label": "MSE for Eff. CombinedNew 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_1.5_64spp_seed_3.exr", "label": "MSE for Eff. CombinedNew 1.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_64spp_seed_1.exr", "label": "MSE for Eff. CombinedNew 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_64spp_seed_2.exr", "label": "MSE for Eff. CombinedNew 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_3_64spp_seed_3.exr", "label": "MSE for Eff. CombinedNew 3.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_64spp_seed_1.exr", "label": "MSE for Eff. CombinedNew 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_64spp_seed_2.exr", "label": "MSE for Eff. CombinedNew 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_combinedNew_10_64spp_seed_3.exr", "label": "MSE for Eff. CombinedNew 10.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_64spp_seed_1.exr", "label": "MSE for Eff. Footprint 0.000000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_64spp_seed_2.exr", "label": "MSE for Eff. Footprint 0.000000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.000000001_64spp_seed_3.exr", "label": "MSE for Eff. Footprint 0.000000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_64spp_seed_1.exr", "label": "MSE for Eff. Footprint 0.0000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_64spp_seed_2.exr", "label": "MSE for Eff. Footprint 0.0000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.0000001_64spp_seed_3.exr", "label": "MSE for Eff. Footprint 0.0000001"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_64spp_seed_1.exr", "label": "MSE for Eff. Footprint 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_64spp_seed_2.exr", "label": "MSE for Eff. Footprint 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.1_64spp_seed_3.exr", "label": "MSE for Eff. Footprint 0.1"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_64spp_seed_1.exr", "label": "MSE for Eff. Footprint 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_64spp_seed_2.exr", "label": "MSE for Eff. Footprint 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_0.5_64spp_seed_3.exr", "label": "MSE for Eff. Footprint 0.5"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_64spp_seed_1.exr", "label": "MSE for Eff. Footprint 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_64spp_seed_2.exr", "label": "MSE for Eff. Footprint 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1_64spp_seed_3.exr", "label": "MSE for Eff. Footprint 1.0"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_1024spp_seed_1.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_64spp_seed_1.exr", "label": "MSE for Eff. Footprint 1e-12"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_1024spp_seed_2.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_64spp_seed_2.exr", "label": "MSE for Eff. Footprint 1e-12"},
    {"reference": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_1024spp_seed_3.exr", "candidate": "C:/Uni/Msc Thesis/render_images/penguin/penguin_footprint_1e-12_64spp_seed_3.exr", "label": "MSE for Eff. Footprint 1e-12"},
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compute relMSE between EXR image pairs and write results to CSV. "
            "With no arguments the RUNS list inside the script is used."
        )
    )
    parser.add_argument(
        "images",
        nargs="*",
        metavar="IMAGE",
        help="Two or more EXR paths — every ordered pair will be compared.",
    )
    parser.add_argument(
        "-o", "--output",
        default=None,
        metavar="FILE",
        help=f"Output CSV path (default: {OUTPUT_CSV}).",
    )
    parser.add_argument(
        "--self",
        dest="include_self",
        action="store_true",
        help="Also compare each image against itself (sanity check). CLI mode only.",
    )
    return parser.parse_args()


def runs_from_cli(images: list[str], include_self: bool) -> list[dict]:
    """Build a run list from positional CLI arguments (all ordered pairs)."""
    pairs = list(itertools.permutations(images, 2))
    if include_self:
        pairs = [(img, img) for img in images] + pairs
    return [{"reference": ref, "candidate": cand} for ref, cand in pairs]


def run_comparison(reference: str, candidate: str) -> tuple[float, float, float]:
    """
    Call compute_mse.py for one pair and return
    (relMSE, relMSE*, relMSE**) as floats.
    """
    result = subprocess.run(
        [sys.executable, str(COMPUTE_MSE_SCRIPT), reference, candidate],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    if result.returncode != 0:
        raise RuntimeError(
            f"compute_mse.py failed for pair ({reference!r}, {candidate!r}):\n"
            f"{result.stderr.strip()}"
        )

    line = result.stdout.strip()
    parts = line.split("\t")
    if len(parts) != 3:
        raise ValueError(
            f"Unexpected output from compute_mse.py: {line!r}\n"
            f"Expected 3 tab-separated values."
        )

    return tuple(float(p) for p in parts)  # (relMSE, relMSE*, relMSE**)


def main() -> None:
    args = parse_args()
    output_csv = args.output or OUTPUT_CSV

    if args.images:
        if len(args.images) < 2:
            print("Error: at least two image paths are required.", file=sys.stderr)
            sys.exit(1)
        runs = runs_from_cli(args.images, args.include_self)
    else:
        if not RUNS:
            print(
                "Error: no images given and the RUNS list is empty.\n"
                "Either pass image paths on the command line or populate RUNS in the script.",
                file=sys.stderr,
            )
            sys.exit(1)
        runs = RUNS

    print(f"Pairs  : {len(runs)}")
    print(f"Output : {output_csv}")
    print()

    rows = []
    for i, params in enumerate(runs, start=1):
        ref   = params["reference"]
        cand  = params["candidate"]
        label = params.get("label") or f"{Path(ref).stem}_vs_{Path(cand).stem}"

        print(f"[{i}/{len(runs)}]  {label}")
        print(f"         {ref}  vs  {cand} … ", end="", flush=True)

        try:
            mse, mse_star, mse_dstar = run_comparison(ref, cand)
            print(f"relMSE={mse:.6g}  relMSE*={mse_star:.6g}  relMSE**={mse_dstar:.6g}")
            rows.append({
                "label":     label,
                "reference": ref,
                "candidate": cand,
                "relMSE":    mse,
                "relMSE*":   mse_star,
                "relMSE**":  mse_dstar,
            })
        except (RuntimeError, ValueError) as exc:
            print(f"ERROR — {exc}")
            # rows.append({
            #     "label":     label,
            #     "reference": ref,
            #     "candidate": cand,
            #     "relMSE":    "error",
            #     "relMSE*":   "error",
            #     "relMSE**":  "error",
            # })

    with open(output_csv, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDNAMES)
        writer.writeheader()
        writer.writerows(rows)

    print(f"\nDone. Results written to {output_csv}")


if __name__ == "__main__":
    main()