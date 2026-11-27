#!/usr/bin/env python3
"""Sort plugin lists by display-name (cross-platform companion of sort_plugin_lists.ps1).

    python3 sort_plugin_lists.py          # the Linux (Camelion++) lists
    python3 sort_plugin_lists.py --all    # every src/pl.*.json

Line endings of each file are preserved.
"""
import glob
import json
import sys

pattern = "src/pl.*.json" if "--all" in sys.argv else "src/pl.linux-*.json"
for path in sorted(glob.glob(pattern)):
    raw = open(path, "rb").read()
    eol = "\r\n" if b"\r\n" in raw else "\n"
    data = json.loads(raw.decode("utf-8"))
    data["npp-plugins"].sort(key=lambda p: p["display-name"].lower())
    text = json.dumps(data, indent="\t", ensure_ascii=False)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace("\n", eol) + eol)
    print(f"sorted {path} ({len(data['npp-plugins'])} plugins)")
