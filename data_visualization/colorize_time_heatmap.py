"""
EXR Render Time Colorizer
=========================
Reads a black-and-white EXR render-time heatmap (values stored as ln(t+1),
time in microseconds), applies the Turbo colormap over a user-defined time
range, and saves a plot with the colored image and a labeled colorbar.

Configuration
-------------
Edit the variables in the CONFIG section below, then run the script.
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import OpenEXR

# =============================================================================
# CONFIG — edit these values
# =============================================================================

# Path to your input .exr render-time heatmap
# EXR_FILE = "./timemaps/owlbear_cub_HD_for_timemap_timeMap.exr"
EXR_FILE = "./timemaps/owlbear_cub_LD_for_timemap_timeMap.exr"
# EXR_FILE = "./timemaps/single_HD_for_timemap_timeMap.exr"
# EXR_FILE = "./timemaps/single_LD_for_timemap_timeMap.exr"

# Which channel holds the data. Set to None to auto-detect.
CHANNEL = None

# Time range in seconds. Pixels outside this range are clamped to the nearest endpoint color (they won't be clipped/masked, just saturated).
# For Owlbear:
TIME_MIN_S = 0.002788 #0.002788 #002276
TIME_MAX_S = 0.820104 #1.820104 #044840
# For Single Feather:
# TIME_MIN_S = 0.001242 #001117 #001009
# TIME_MAX_S = 0.052019 #059873 #085765

# Output image path. Supported formats: .png, .pdf, .svg, .jpg
OUTPUT_FILE = "./timemaps/render_times_colored.png"

# Resolution of the saved figure in dots per inch
DPI = 150

# Font size for the colorbar label and tick labels (points)
FONT_SIZE = 24

# =============================================================================


def load_channel(exr_file: str, channel: str | None) -> tuple[np.ndarray, str]:
    """Load a single channel from an EXR file as a 2-D float32 array."""
    f = OpenEXR.File(exr_file, separate_channels=True)
    channels = f.channels()

    available = list(channels.keys())
    if not available:
        raise ValueError(f"No channels found in '{exr_file}'.")

    if channel is None:
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

    data = channels[channel].pixels  # (height, width), float32
    return data, channel


def recover_time_seconds(log_data: np.ndarray) -> np.ndarray:
    """Reverse log1p and convert from microseconds to seconds."""
    return np.expm1(log_data.astype(np.float64)) / 1_000_000


def colorize(exr_file: str, channel: str | None,
             time_min_s: float, time_max_s: float,
             output_file: str, dpi: int, font_size: int) -> None:

    log_data, ch = load_channel(exr_file, channel)
    times_s = recover_time_seconds(log_data)   # float64, seconds

    height, width = times_s.shape

    norm = mcolors.Normalize(vmin=time_min_s, vmax=time_max_s, clip=True)
    cmap = plt.colormaps["turbo"]

    # --- figure layout ---
    # Give the image its natural aspect ratio; colorbar gets a fixed width.
    img_aspect = width / height
    fig_height = 6.0                        # inches
    img_width  = fig_height * img_aspect
    cbar_width = 0.6                        # inches for colorbar + label
    pad        = 0.3                        # inches between image and cbar
    fig_width  = img_width + cbar_width + pad

    fig = plt.figure(figsize=(fig_width, fig_height))

    # Axes for the image (left portion)
    ax_img = fig.add_axes([
        0.0,                                # left
        0.0,                                # bottom
        img_width / fig_width,              # width fraction
        1.0,                                # height fraction
    ])

    # Axes for the colorbar (right portion)
    ax_cbar = fig.add_axes([
        (img_width + pad) / fig_width,
        0.05,
        (cbar_width * 0.35) / fig_width,
        0.90,
    ])

    im = ax_img.imshow(times_s, cmap=cmap, norm=norm, origin="upper",
                       interpolation="nearest", aspect="equal")
    ax_img.axis("off")

    cb = fig.colorbar(im, cax=ax_cbar)
    cb.set_label("Render time (s)", labelpad=8, fontsize=font_size)
    cb.ax.tick_params(labelsize=font_size)

    # Tick the colorbar at round values within the range
    tick_count = 6
    ticks = np.linspace(time_min_s, time_max_s, tick_count)
    cb.set_ticks(ticks)
    cb.set_ticklabels([f"{t:.4g}" for t in ticks])

    fig.savefig(output_file, dpi=dpi, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)

    print(f"\nSaved: {output_file}  ({width}x{height} px, "
          f"range {time_min_s}–{time_max_s} s, channel '{ch}')\n")


# =============================================================================
# Entry point
# =============================================================================

if __name__ == "__main__":
    colorize(
        exr_file=EXR_FILE,
        channel=CHANNEL,
        time_min_s=TIME_MIN_S,
        time_max_s=TIME_MAX_S,
        output_file=OUTPUT_FILE,
        dpi=DPI,
        font_size=FONT_SIZE,
    )