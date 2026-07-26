"""
EXR Error Map Analyzer
======================
Analyzes a linear EXR error map by summing the per-pixel error across the
R, G, and B channels and reporting statistics or querying individual pixels.

Configuration
-------------
Edit the variables in the CONFIG section below, then run the script.
"""

import numpy as np
import OpenEXR

# =============================================================================
# CONFIG — edit these values
# =============================================================================

# Path to your .exr error map
# EXR_FILE = "./errormaps/error_owlbear_cub_HD_vs_LD.exr"
EXR_FILE = "./errormaps/error_owlbear_cub_HD_vs_footprint_0.1.exr"

# Which channels to sum. Pixels outside this list are ignored (e.g. alpha).
CHANNELS = ("R", "G", "B")

# --- Mode ---
# "stats"  → print summary statistics across all pixels
# "pixel"  → print the summed error at a specific (x, y) coordinate
MODE = "stats"

# Pixel coordinate to query when MODE = "pixel".
# (0, 0) is the top-left corner. X goes right, Y goes down.
QUERY_X = 0
QUERY_Y = 0

# Outlier trimming for "stats" mode (percentage, not fraction).
# The top and bottom OUTLIER_TRIM % of values are excluded before
# computing all statistics (mean, median, std, percentiles, min, max).
# Examples: 0.0001 trims the outermost 0.0001% on each side.
#           0.0 disables trimming entirely.
# Note: the pixel-coordinate query is never affected by this setting.
OUTLIER_TRIM = 0.1

# =============================================================================


def load_summed_channels(exr_file: str,
                         channels: tuple[str, ...]) -> tuple[np.ndarray, list[str]]:
    """Load the requested channels from an EXR file and return their per-pixel sum."""
    f = OpenEXR.File(exr_file, separate_channels=True)
    available = f.channels()
    avail_names = list(available.keys())

    missing = [c for c in channels if c not in avail_names]
    if missing:
        raise ValueError(
            f"Channel(s) {missing} not found in '{exr_file}'. "
            f"Available channels: {avail_names}"
        )

    used = list(channels)
    summed = None
    for name in used:
        data = available[name].pixels.astype(np.float64)
        summed = data if summed is None else summed + data

    print(f"[info] Summed channels {used}. Available: {avail_names}")
    return summed, used


def trim_outliers(flat: np.ndarray, trim_pct: float) -> np.ndarray:
    """
    Return a 1-D array with the bottom and top trim_pct% of values removed.
    trim_pct is a percentage (e.g. 0.0001 means 0.0001%).
    """
    if trim_pct <= 0.0:
        return flat
    lo = np.percentile(flat, trim_pct)
    hi = np.percentile(flat, 100.0 - trim_pct)
    return flat[(flat >= lo) & (flat <= hi)]


def run_stats(exr_file: str, channels: tuple[str, ...], outlier_trim: float) -> None:
    """Print summary statistics for the summed error across all pixels."""
    error, used = load_summed_channels(exr_file, channels)

    height, width = error.shape
    flat = error.ravel()
    trimmed = trim_outliers(flat, outlier_trim)

    dropped = len(flat) - len(trimmed)

    # True global extremes from the full image, unaffected by trimming
    global_mn     = flat.min()
    global_mx     = flat.max()
    global_mn_pos = np.unravel_index(error.argmin(), error.shape)
    global_mx_pos = np.unravel_index(error.argmax(), error.shape)

    total  = flat.sum()
    mn     = trimmed.min()
    mx     = trimmed.max()
    avg    = trimmed.mean()
    median = np.median(trimmed)
    std    = trimmed.std()
    p90    = np.percentile(trimmed, 90)
    p99    = np.percentile(trimmed, 99)

    # Find coordinates of trimmed min/max by masking the 2D array
    lo_val = np.percentile(flat, outlier_trim)        if outlier_trim > 0.0 else flat.min()
    hi_val = np.percentile(flat, 100.0 - outlier_trim) if outlier_trim > 0.0 else flat.max()
    mask = (error >= lo_val) & (error <= hi_val)
    masked = np.where(mask, error, np.nan)
    mn_pos = np.unravel_index(np.nanargmin(masked), error.shape)
    mx_pos = np.unravel_index(np.nanargmax(masked), error.shape)

    print(f"\nFile     : {exr_file}")
    print(f"Channels : {used} (summed)")
    print(f"Size     : {width} x {height} pixels")
    if outlier_trim > 0.0:
        print(f"Trim     : {outlier_trim}% each side  ({dropped} pixels excluded)")
    print()
    if outlier_trim > 0.0:
        print(f"  Minimum (trimmed set) : {mn:.6f}  at pixel ({mn_pos[1]}, {mn_pos[0]})")
    print(f"  Minimum (global)      : {global_mn:.6f}  at pixel ({global_mn_pos[1]}, {global_mn_pos[0]})")
    if outlier_trim > 0.0:
        print(f"  Maximum (trimmed set) : {mx:.6f}  at pixel ({mx_pos[1]}, {mx_pos[0]})")
    print(f"  Maximum (global)      : {global_mx:.6f}  at pixel ({global_mx_pos[1]}, {global_mx_pos[0]})")

    print()
    print(f"  Mean            : {avg:.6f}")
    print(f"  Median          : {median:.6f}")
    print(f"  Std deviation   : {std:.6f}")
    print(f"  90th percentile : {p90:.6f}")
    print(f"  99th percentile : {p99:.6f}")
    print()
    print(f"  Total (all pixels)  : {total:.6f}")


def run_pixel_query(exr_file: str, channels: tuple[str, ...],
                    x: int, y: int) -> None:
    """Print the per-channel and summed error at a single pixel (x, y)."""
    f = OpenEXR.File(exr_file, separate_channels=True)
    available = f.channels()
    avail_names = list(available.keys())

    missing = [c for c in channels if c not in avail_names]
    if missing:
        raise ValueError(
            f"Channel(s) {missing} not found in '{exr_file}'. "
            f"Available channels: {avail_names}"
        )

    first = available[channels[0]].pixels
    height, width = first.shape
    if not (0 <= x < width and 0 <= y < height):
        raise ValueError(
            f"Pixel ({x}, {y}) is outside the image bounds ({width} x {height})."
        )

    per_channel = {}
    total = 0.0
    for name in channels:
        v = float(available[name].pixels[y, x])
        per_channel[name] = v
        total += v

    print(f"\nFile     : {exr_file}")
    print(f"Channels : {list(channels)}")
    print(f"Pixel    : ({x}, {y})")
    print()
    for name, v in per_channel.items():
        print(f"  {name} : {v:.6f}")
    print(f"  Sum : {total:.6f}")
    print()


# =============================================================================
# Entry point
# =============================================================================

if __name__ == "__main__":
    if MODE == "stats":
        run_stats(EXR_FILE, CHANNELS, OUTLIER_TRIM)
    elif MODE == "pixel":
        run_pixel_query(EXR_FILE, CHANNELS, QUERY_X, QUERY_Y)
    else:
        raise ValueError(f"Unknown MODE '{MODE}'. Choose 'stats' or 'pixel'.")
