"""
EXR Error Map Colorizer
=======================
Reads a linear EXR error map, sums the per-pixel error across the R, G, and B
channels, applies the Turbo colormap over a user-defined error range, and saves
a plot with the colored image and a labeled colorbar.

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

# Path to your input .exr error map
# EXR_FILE = "./errormaps/error_owlbear_cub_HD_vs_LD_cropped.exr"
EXR_FILE = "./errormaps/error_owlbear_cub_HD_vs_footprint_0.1_cropped.exr"

# Which channels to sum. Pixels outside this list are ignored (e.g. alpha).
CHANNELS = ("R", "G", "B")

# Error range used for the colormap. Pixels outside this range are clamped to
# the nearest endpoint color (they won't be clipped/masked, just saturated).
ERROR_MIN = 0.0
ERROR_MAX = 5 #13.78 LD  # 1.83 footprint

# Output image path. Supported formats: .png, .pdf, .svg, .jpg
OUTPUT_FILE = "./errormaps/error_colored.png"

# Resolution of the saved figure in dots per inch
DPI = 150

# Font size for the colorbar label and tick labels (points)
FONT_SIZE = 20

# Label shown next to the colorbar
COLORBAR_LABEL = "Summed Relative Absolute Error (R+G+B)"

# =============================================================================


def load_summed_channels(exr_file: str,
                         channels: tuple[str, ...]) -> tuple[np.ndarray, list[str]]:
    """Load and sum the requested channels from an EXR file."""
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


def colorize(exr_file: str, channels: tuple[str, ...],
             error_min: float, error_max: float,
             output_file: str, dpi: int, font_size: int,
             colorbar_label: str) -> None:

    error, used = load_summed_channels(exr_file, channels)
    height, width = error.shape

    norm = mcolors.Normalize(vmin=error_min, vmax=error_max, clip=True)
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

    ax_img = fig.add_axes([
        0.0,
        0.0,
        img_width / fig_width,
        1.0,
    ])

    ax_cbar = fig.add_axes([
        (img_width + pad) / fig_width,
        0.05,
        (cbar_width * 0.35) / fig_width,
        0.90,
    ])

    im = ax_img.imshow(error, cmap=cmap, norm=norm, origin="upper",
                       interpolation="nearest", aspect="equal")
    ax_img.axis("off")

    cb = fig.colorbar(im, cax=ax_cbar)
    cb.set_label(colorbar_label, labelpad=8, fontsize=font_size)
    cb.ax.tick_params(labelsize=font_size)

    tick_count = 6
    ticks = np.linspace(error_min, error_max, tick_count)
    cb.set_ticks(ticks)
    cb.set_ticklabels([f"{t:.4g}" for t in ticks])

    fig.savefig(output_file, dpi=dpi, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)

    print(f"\nSaved: {output_file}  ({width}x{height} px, "
          f"range {error_min}–{error_max}, channels {used})\n")


# =============================================================================
# Entry point
# =============================================================================

if __name__ == "__main__":
    colorize(
        exr_file=EXR_FILE,
        channels=CHANNELS,
        error_min=ERROR_MIN,
        error_max=ERROR_MAX,
        output_file=OUTPUT_FILE,
        dpi=DPI,
        font_size=FONT_SIZE,
        colorbar_label=COLORBAR_LABEL,
    )
