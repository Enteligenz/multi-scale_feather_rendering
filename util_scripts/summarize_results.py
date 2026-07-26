import re
import os
from collections import defaultdict

LOG_DIR = "."  # Directory where your .txt log files are

# Pattern: {spp}_{threshold_lod}_lod{switch}_{image_id_base}_{run_num}.txt
FILENAME_RE = re.compile(
    r'^(\d+_[\d.eE+-]+_lod\w+_.+)_(\d+)\.txt$'
)

def parse_render_time(filepath):
    with open(filepath, "r") as f:
        content = f.read()
    if content.strip() == "error":
        return None
    m = re.search(r'render:\s+([\d.]+) seconds', content)
    return m.group(1) if m else None

# Group files by their base name (everything before the final _N)
groups = defaultdict(dict)

for fname in sorted(os.listdir(LOG_DIR)):
    m = FILENAME_RE.match(fname)
    if m:
        base_name = m.group(1)
        run_num   = int(m.group(2))
        groups[base_name][run_num] = fname

# Write summary
output_lines = []

for base_name in sorted(groups.keys()):
    runs = groups[base_name]
    times = []
    for run_num in sorted(runs.keys()):
        fpath = os.path.join(LOG_DIR, runs[run_num])
        t = parse_render_time(fpath)
        times.append(t if t is not None else "error")
    line = f"{base_name}: {' + '.join(times)}"
    output_lines.append(line)

output_path = os.path.join(LOG_DIR, "SUMMARY.txt")
with open(output_path, "w") as f:
    f.write("\n".join(output_lines) + "\n")

print(f"Summary written to {output_path}")
print(f"Found {len(output_lines)} parameter groups.")