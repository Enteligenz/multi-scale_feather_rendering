"""
Bias vs. Efficiency Scatterplot
================================
Each scatter point represents a Bias/Efficiency tradeoff for a given candidate.

- X-axis : Efficiency  (relMSE value from rows whose label starts with "Efficiency")
- Y-axis : Bias        (relMSE value from rows whose label starts with "Bias")
- Color  : LOD Switch  (extracted from the label, discrete, auto-assigned)
- Label  : full label string of the Bias row (shown on hover + as text on point)

Pairing logic:
  Rows are split by the first word of "label" into Bias rows and Efficiency rows.
  A Bias row and an Efficiency row are paired when their "candidate" column matches.

Label format expected in the "label" column:
  [Bias|Efficiency] [prefix] <LOD Switch keyword> [LOD Switch Value]

  where LOD Switch is one of:
    Bounce, Distance, Only LD, Only HD, PDF,
    Combined, CombinedLessDist, CombinedNew, Footprint

  and LOD Switch Value is an optional trailing number
  (integer, float, or scientific notation, e.g. 1e-12).

Examples:
  "Bias Bounce 2"                    -> LOD Switch: Bounce,    Value: 2
  "Bias Only LD"                     -> LOD Switch: Only LD,   Value: (none)
  "Efficiency ODistance 10.0"        -> LOD Switch: Distance,  Value: 10.0
  "MSE for Eff. Footprint 1e-12"     -> LOD Switch: Footprint, Value: 1e-12

HOW TO USE
----------
1. Install dependencies if needed:
       pip install plotly pandas
2. Set DATA_CSV and TIME_CSV to your file paths.
3. Optionally change VALUE_COLUMN to "relMSE*" or "relMSE**".
4. Run:
       python create_scatterplot_bias_efficiency.py
   A browser tab opens with the interactive plot.
"""

import re

import pandas as pd
import plotly.express as px
import plotly.graph_objects as go

# -- CONFIGURATION -------------------------------------------------------------
DATA_CSV  = "mse_results_owlbear_cub.csv"   # main input CSV
TIME_CSV  = "times_owlbear_cub.csv"   # CSV with "file" and "time" columns

# Switch between "relMSE", "relMSE*", or "relMSE**" here:
VALUE_COLUMN = "relMSE*"

# Base font size (applies to axis labels, tick labels, legend, hover).
# The title is scaled 1.4x this value, and point labels 0.8x.
FONT_SIZE = 24

# If True, point labels show only the LOD Switch Value (e.g. "2", "1e-12").
# If False, the full label is shown with the leading "Bias " stripped (e.g. "Bounce 2").
SHORT_LABELS = True
NO_LABELS = False

# If True, use a colorblind-optimized palette instead of the default Plotly palette.
COLORBLIND_PALETTE = True

# Rename LOD Switch labels in the legend. Any keyword not listed here keeps its original name.
# Example: {"Bounce": "Path Depth", "Only LD": "LD Only"}
LOD_SWITCH_NAMES = {
    "Bounce":           "Depth",
    # "Distance":         "Distance",
    "Only LD":          "OnlyLD",
    "Only HD":          "OnlyHD",
    # "PDF":              "PDF",
    "Combined":         "distPDF",
    "CombinedLessDist": "distPDFWeighted",
    "CombinedNew":      "distPDFDepth",
    # "Footprint":        "Footprint",
}

# Closeup & Single
# Y_TICK_MULTIPLIERS = [1, 2, 5]
# X_TICK_MULTIPLIERS = [1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5, 6, 7, 8, 9]
# Owlbear Cub & Penguin
Y_TICK_MULTIPLIERS = [1, 2, 3, 4, 5, 6, 7, 8, 9]
X_TICK_MULTIPLIERS = [1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5, 6, 7, 8, 9]

# -- CONSTANTS -----------------------------------------------------------------

# Order matters: longer/more-specific alternatives first.
LOD_SWITCH_KEYWORDS = [
    "Only LD",
    "Only HD",
    "CombinedLessDist",
    "CombinedNew",
    "Combined",
    "Footprint",
    "Bounce",
    "Distance",
    "PDF",
]

_NUMBER_PAT = r"[+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?"
_KEYWORDS_ALT = "|".join(re.escape(k) for k in LOD_SWITCH_KEYWORDS)
_LABEL_RE = re.compile(
    rf"(?P<lod_switch>{_KEYWORDS_ALT})"
    rf"(?:\s+(?P<lod_value>{_NUMBER_PAT}))?$"
)


def parse_label(label: str) -> tuple[str, str]:
    """
    Return (lod_switch, lod_switch_value) extracted from a label string.
    lod_switch_value is an empty string when no trailing number is present.
    Raises ValueError when no known keyword is found.
    """
    m = _LABEL_RE.search(label)
    if not m:
        raise ValueError(
            f"Could not extract a LOD Switch keyword from label: {label!r}\n"
            f"Known keywords: {LOD_SWITCH_KEYWORDS}"
        )
    return m.group("lod_switch"), (m.group("lod_value") or "")


# -- DATA ----------------------------------------------------------------------

df      = pd.read_csv(DATA_CSV)
time_df = pd.read_csv(TIME_CSV)

# -- PARSE LABEL COLUMN --------------------------------------------------------

parsed = df["label"].map(parse_label)
df["LOD Switch"]       = parsed.map(lambda t: t[0])
df["LOD Switch Value"] = parsed.map(lambda t: t[1])

# Strip optional "_seed_<n>" suffix from candidate and reference so that
# rows differing only by seed are averaged together.
# Match "_seed_<n>" immediately before the file extension (or end of string).
_SEED_RE = re.compile(r"_seed_\d+(?=\.[^.]+$|$)")
df["candidate"] = df["candidate"].str.replace(_SEED_RE, "", regex=True)
df["reference"]  = df["reference"].str.replace(_SEED_RE, "", regex=True)

# Label prefix determines whether a row contributes to Bias or Efficiency.
bias_df = df[df["label"].str.startswith("Bias")].copy()
eff_df  = df[df["label"].str.startswith("MSE for Eff.")].copy()

# Average VALUE_COLUMN across seeds (same base candidate name + same label).
bias_df = (bias_df.groupby(["candidate", "label", "LOD Switch", "LOD Switch Value"],
                            as_index=False)[VALUE_COLUMN].mean())
eff_df  = (eff_df.groupby(["candidate", "label", "LOD Switch", "LOD Switch Value"],
                            as_index=False)[VALUE_COLUMN].mean())

# -- APPLY TIME-BASED EFFICIENCY METRIC ---------------------------------------
# Strip seed suffix from the time CSV filenames so they match the base candidate names.
time_df["file"] = time_df["file"].str.replace(_SEED_RE, "", regex=True)
# Average time across seeds for the same base filename.
time_df = time_df.groupby("file", as_index=False)["time"].mean()

# Merge time into eff_df on candidate == file.
eff_df = eff_df.merge(time_df, left_on="candidate", right_on="file", how="left")

missing_mask = eff_df["time"].isna()
if missing_mask.any():
    missing_files = eff_df.loc[missing_mask, "candidate"].tolist()
    print(f"Warning: {len(missing_files)} efficiency row(s) had no matching time entry and will be dropped:")
    for f in missing_files:
        print(f"  - {f}")
    eff_df = eff_df.dropna(subset=["time"])

# Compute f = 1 / (relMSE * time) and use it as the efficiency value.
eff_df[VALUE_COLUMN] = 1.0 / (eff_df[VALUE_COLUMN] * eff_df["time"])

# -- PAIR ROWS ON "candidate" --------------------------------------------------

paired = bias_df.merge(
    eff_df[["candidate", VALUE_COLUMN]],
    on="candidate",
    suffixes=("_bias", "_eff"),
    how="inner",
)

if paired.empty:
    print(
        "No paired rows found. Make sure every candidate has both a Bias and "
        "an Efficiency row, and that the label starts with 'Bias' or 'MSE for Eff.'."
    )
    sys.exit(1)

# -- COLOR MAP -----------------------------------------------------------------

# Apply any renames from LOD_SWITCH_NAMES; unmatched keys keep their original name.
paired["LOD Switch"] = paired["LOD Switch"].map(lambda x: LOD_SWITCH_NAMES.get(x, x))

lod_switches = paired["LOD Switch"].unique()

# _COLORBLIND_PALETTE = [ # Combined from Okabe and Ito + Paul Tol's Muted
#     "rgb(0,0,0)",     "rgb(51,117,56)",   "rgb(0,114,178)",
#     "rgb(86,180,233)","rgb(240,228,66)", "rgb(230,159,0)",
#     "rgb(213,94,0)","rgb(204,121,167)", "rgb(93,168,153)",
# ]
_COLORBLIND_PALETTE = [ # Only Okabe and Ito
    "rgb(0,0,0)",     "rgb(0,158,115)",   "rgb(0,114,178)",
    "rgb(86,180,233)","rgb(240,228,66)", "rgb(230,159,0)",
    "rgb(213,94,0)","rgb(204,121,167)",
]
palette = _COLORBLIND_PALETTE if COLORBLIND_PALETTE else px.colors.qualitative.Plotly
color_map = {lod: palette[i % len(palette)] for i, lod in enumerate(lod_switches)}

# -- PLOT ----------------------------------------------------------------------

fig = go.Figure()

for lod, group in paired.groupby("LOD Switch"):

    # Highest priority: no labels at all
    if NO_LABELS:
        text_values = None
        mode_value = "markers"

    # Second priority: short labels
    elif SHORT_LABELS:
        text_values = group["LOD Switch Value"]
        mode_value = "markers+text"

    # Default: full labels
    else:
        text_values = group["label"].str.replace(
            r"^Bias\s+", "", regex=True
        )
        mode_value = "markers+text"

    fig.add_trace(go.Scatter(
        x=group[f"{VALUE_COLUMN}_eff"],
        y=group[f"{VALUE_COLUMN}_bias"],
        mode=mode_value,
        name=lod,
        marker=dict(
            color=color_map[lod],
            size=10,
            line=dict(width=1, color="white"),
        ),
        text=text_values,
        textposition="top center",
        textfont=dict(size=FONT_SIZE * 0.8),
        hovertemplate=(
            "<b>%{text}</b><br>"
            f"Bias ({VALUE_COLUMN}): %{{y:.4f}}<br>"
            f"Efficiency (1 / ({VALUE_COLUMN} * time)): %{{x:.4f}}"
            "<extra></extra>"
        ),
    ))

fig.update_layout(
    font=dict(size=FONT_SIZE),
    title=dict(text=f"Efficiency vs Bias", font=dict(size=FONT_SIZE * 1.4)),
    yaxis=dict(title=dict(text=r"$\text{Bias}(\text{MSE}(I, H_{\text{Ref}}))$", font=dict(size=FONT_SIZE)),       type="log", zeroline=True, zerolinecolor="lightgrey"),
    xaxis=dict(title=dict(text=r"$\text{Efficiency}\left(1 / (t \cdot \text{MSE}(I, H_{\text{Ref}}))\right)$", font=dict(size=FONT_SIZE)), type="log", zeroline=True, zerolinecolor="lightgrey"),
    legend=dict(title="LOD Switch", borderwidth=1),
    plot_bgcolor="white",
    paper_bgcolor="white",
    hovermode="closest",
    width=1500,
    height=700,
    margin=dict(t=70, b=60, l=60, r=40),
)

def log_tickvals(values, multipliers):
    """Generate tickvals/ticktext for a log axis covering the range of `values`.
    Major ticks at powers of 10, minor ticks at 2x and 5x each decade.
    Every tick gets its plain decimal label.
    """
    import math
    lo = math.floor(math.log10(values.min()))
    hi = math.ceil(math.log10(values.max()))
    vals = []
    for exp in range(lo, hi + 1):
        for mult in multipliers:
            v = mult * 10 ** exp
            if values.min() * 0.5 <= v <= values.max() * 2:
                vals.append(v)
    def fmt(v):
        # Show as integer if whole number, else as minimal decimal
        return str(int(v)) if v == int(v) else f"{v:g}"
    return dict(tickvals=vals, ticktext=[fmt(v) for v in vals])

x_ticks = log_tickvals(paired[f"{VALUE_COLUMN}_eff"], X_TICK_MULTIPLIERS)
y_ticks = log_tickvals(paired[f"{VALUE_COLUMN}_bias"], Y_TICK_MULTIPLIERS)
fig.update_xaxes(showgrid=True, gridcolor="#eeeeee", **x_ticks)
fig.update_yaxes(showgrid=True, gridcolor="#eeeeee", **y_ticks)

# fig.show()
# fig.write_html("scatter_bias_efficiency.html")   # save to file instead

fig.write_html("scatter_bias_efficiency.html", include_mathjax="cdn")
import webbrowser, os
webbrowser.open(f"file://{os.path.abspath('scatter_bias_efficiency.html')}")