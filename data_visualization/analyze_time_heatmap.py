"""
EXR Render Time Analyzer
========================
Analyzes an EXR heatmap image where pixel values represent render time
that has been passed through C++'s log1p (i.e. ln(x+1)).

This script recovers the original render times via expm1 (i.e. e^x - 1)
and reports statistics or queries individual pixels.

Configuration
-------------
Edit the variables in the CONFIG section below, then run the script.
"""

import numpy as np
import OpenEXR

# =============================================================================
# CONFIG — edit these values
# =============================================================================

# Path to your .exr render-time heatmap
# EXR_FILE = "./timemaps/owlbear_cub_HD_for_timemap_timeMap.exr"
# EXR_FILE = "./timemaps/owlbear_cub_LD_for_timemap_timeMap.exr"
# EXR_FILE = "./timemaps/single_HD_for_timemap_timeMap.exr"
EXR_FILE = "./timemaps/single_LD_for_timemap_timeMap.exr"

# Which channel to read. Common names: "R", "G", "B", "Y", "A".
# Render time heatmaps are often stored in "R" or "Y" (luminance).
# Set to None to auto-detect the first available channel.
CHANNEL = None

# --- Mode ---
# "stats"  → print summary statistics across all pixels
# "pixel"  → print the render time at a specific (x, y) coordinate
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
OUTLIER_TRIM = 0.01

# =============================================================================


def load_channel(exr_file: str, channel: str | None) -> tuple[np.ndarray, str]:
    """
    Load a single channel from an EXR file as a 2-D float32 array.
    Returns (array, channel_name_used).
    """
    f = OpenEXR.File(exr_file, separate_channels=True)
    channels = f.channels()

    available = list(channels.keys())
    if not available:
        raise ValueError(f"No channels found in '{exr_file}'.")

    if channel is None:
        # Prefer "R" or "Y"; fall back to whatever is first
        for preferred in ("R", "Y", "G", "B"):
            if preferred in available:
                channel = preferred
                break
        else:
            channel = available[0]
        print(f"[info] Auto-selected channel '{channel}'. "
              f"Available: {available}")
    elif channel not in available:
        raise ValueError(
            f"Channel '{channel}' not found in '{exr_file}'. "
            f"Available channels: {available}"
        )

    data = channels[channel].pixels  # shape: (height, width), dtype float32
    return data, channel


def recover_time(log_values: np.ndarray) -> np.ndarray:
    """
    Reverse the log1p transform: original_time = expm1(log_value) = e^x - 1.
    Uses float64 for accuracy with large values.
    """
    return np.expm1(log_values.astype(np.float64))


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


def run_stats(exr_file: str, channel: str | None, outlier_trim: float) -> None:
    """Print summary statistics for render times across all pixels."""
    log_data, ch = load_channel(exr_file, channel)
    times = recover_time(log_data)

    height, width = times.shape
    flat = times.ravel()
    trimmed = trim_outliers(flat, outlier_trim)

    dropped = len(flat) - len(trimmed)

    # True global extremes from the full image, unaffected by trimming
    global_mn     = flat.min()  / 1_000_000
    global_mx     = flat.max()  / 1_000_000
    global_mn_pos = np.unravel_index(times.argmin(), times.shape)
    global_mx_pos = np.unravel_index(times.argmax(), times.shape)


    total  = flat.sum()                       / 1_000_000
    mn     = trimmed.min()                    / 1_000_000
    mx     = trimmed.max()                    / 1_000_000
    avg    = trimmed.mean()                   / 1_000_000
    median = np.median(trimmed)               / 1_000_000
    std    = trimmed.std()                    / 1_000_000
    p90    = np.percentile(trimmed, 90)       / 1_000_000
    p99    = np.percentile(trimmed, 99)       / 1_000_000

    # Find coordinates of trimmed min/max by masking the 2D array
    lo_val = np.percentile(flat, outlier_trim)        if outlier_trim > 0.0 else flat.min()
    hi_val = np.percentile(flat, 100.0 - outlier_trim) if outlier_trim > 0.0 else flat.max()
    mask = (times >= lo_val) & (times <= hi_val)
    masked = np.where(mask, times, np.nan)
    mn_pos = np.unravel_index(np.nanargmin(masked), times.shape)
    mx_pos = np.unravel_index(np.nanargmax(masked), times.shape)

    print(f"\nFile    : {exr_file}")
    print(f"Channel : {ch}")
    print(f"Size    : {width} x {height} pixels")
    # if outlier_trim > 0.0:
    #     print(f"  Minimum (trimmed set) : {mn:.6f} s")
    # print(f"  Minimum (global)      : {global_mn:.6f} s  at pixel ({global_mn_pos[1]}, {global_mn_pos[0]})")
    # if outlier_trim > 0.0:
    #     print(f"  Maximum (trimmed set) : {mx:.6f} s")
    # print(f"  Maximum (global)      : {global_mx:.6f} s  at pixel ({global_mx_pos[1]}, {global_mx_pos[0]})")
    if outlier_trim > 0.0:
        print(f"Trim    : {outlier_trim}% each side  ({dropped} pixels excluded)")
    print()
    if outlier_trim > 0.0:
        print(f"  Minimum (trimmed set) : {mn:.6f} s  at pixel ({mn_pos[1]}, {mn_pos[0]})")
    print(f"  Minimum (global)      : {global_mn:.6f} s  at pixel ({global_mn_pos[1]}, {global_mn_pos[0]})")
    if outlier_trim > 0.0:
        print(f"  Maximum (trimmed set) : {mx:.6f} s  at pixel ({mx_pos[1]}, {mx_pos[0]})")
    print(f"  Maximum (global)      : {global_mx:.6f} s  at pixel ({global_mx_pos[1]}, {global_mx_pos[0]})")


    print()
    print(f"  Mean            : {avg:.6f} s")
    print(f"  Median          : {median:.6f} s")
    print(f"  Std deviation   : {std:.6f} s")
    print(f"  90th percentile : {p90:.6f} s")
    print(f"  99th percentile : {p99:.6f} s")
    print()
    print(f"  Total (all pixels)  : {total:.6f} s")


def run_pixel_query(exr_file: str, channel: str | None,
                    x: int, y: int) -> None:
    """Print the render time at a single pixel (x, y)."""
    log_data, ch = load_channel(exr_file, channel)

    height, width = log_data.shape
    if not (0 <= x < width and 0 <= y < height):
        raise ValueError(
            f"Pixel ({x}, {y}) is outside the image bounds "
            f"({width} x {height})."
        )

    log_val  = float(log_data[y, x])   # row = y, col = x
    time_val = float(np.expm1(log_val)) / 1_000_000

    print(f"\nFile    : {exr_file}")
    print(f"Channel : {ch}")
    print(f"Pixel   : ({x}, {y})")
    print()
    print(f"  Stored log1p value : {log_val:.6f}")
    print(f"  Recovered time     : {time_val:.6f} s")
    print()


# =============================================================================
# Entry point
# =============================================================================

if __name__ == "__main__":
    if MODE == "stats":
        run_stats(EXR_FILE, CHANNEL, OUTLIER_TRIM)
    elif MODE == "pixel":
        run_pixel_query(EXR_FILE, CHANNEL, QUERY_X, QUERY_Y)
    else:
        raise ValueError(f"Unknown MODE '{MODE}'. Choose 'stats' or 'pixel'.")