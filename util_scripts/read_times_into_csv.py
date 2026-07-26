import os
import re
import csv

# ── Parameters (edit these as needed) ────────────────────────────────────────
INPUT_FOLDER = "C:/Uni/Msc Thesis/render_images/penguin/times"   # folder containing the .txt files
OUTPUT_CSV   = "times_penguin.csv"
KEYWORD      = "penguin"
PREFIX       = "C:/Uni/Msc Thesis/render_images/penguin/"
SUFFIX       = ".exr"
# ─────────────────────────────────────────────────────────────────────────────


def extract_file_value(filename: str) -> str | None:
    """
    Extract the substring starting at KEYWORD and ending after 'seed_<digits>',
    then wrap it with PREFIX and SUFFIX.
    """
    name = os.path.splitext(filename)[0]          # drop .txt
    # Match from KEYWORD to end-of-string, preferring to stop after seed_<number>
    pattern = rf"({re.escape(KEYWORD)}(?:.*?seed_\d+|.*))"
    match = re.search(pattern, name)
    if match:
        return PREFIX + match.group(1) + SUFFIX
    return None


def extract_render_time(filepath: str) -> float | None:
    """
    Return the render time (float) from a line like 'render:  161.92 seconds'.
    """
    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            if line.lower().startswith("render"):
                numbers = re.findall(r"[\d.]+", line)
                if numbers:
                    return float(numbers[0])
    return None


def main():
    txt_files = [f for f in os.listdir(INPUT_FOLDER) if f.endswith(".txt")]
    if not txt_files:
        print("No .txt files found in", INPUT_FOLDER)
        return

    rows = []
    for filename in sorted(txt_files):
        filepath = os.path.join(INPUT_FOLDER, filename)

        render_time = extract_render_time(filepath)
        file_value  = extract_file_value(filename)

        if render_time is None:
            print(f"  WARNING: no render time found in {filename}")
        if file_value is None:
            print(f"  WARNING: keyword '{KEYWORD}' / seed pattern not found in {filename}")

        rows.append({
            "file": file_value if file_value is not None else filename,
            "time": render_time if render_time is not None else "",
        })

    with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["file", "time"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"Done — {len(rows)} row(s) written to '{OUTPUT_CSV}'.")


if __name__ == "__main__":
    main()