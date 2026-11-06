"""Pulls the "Architect input" lines out of a creative log and writes one description file per prefab.

    python extract.py <xenbono_creative.log> <out folder>

Each file can be given to preview.exe (see preview.cpp), and then to render.py, to look at what the layout makes of that prefab. When a prefab appears
more than once in the log, the last time is kept.
"""
import os
import re
import sys

log, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)

prefabs = {}
current = None

for line in open(log, encoding="utf8", errors="replace"):
    m = re.search(r"Architect input: (prefab|type) (.*)$", line)
    if not m:
        continue

    kind, rest = m.group(1), m.group(2).strip()

    if kind == "prefab":
        current = rest.split()[0]
        prefabs[current] = [f"prefab {rest}"]
    elif current:
        prefabs[current].append(f"type {rest}")

for name, lines in prefabs.items():
    with open(os.path.join(out, name + ".txt"), "w", encoding="utf8") as f:
        f.write("\n".join(lines) + "\n")

print(f"{len(prefabs)} prefabs written to {out}")
