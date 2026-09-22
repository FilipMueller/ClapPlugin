#!/usr/bin/env python3
"""Combine the per-run summary CSVs of a measurement campaign into one table.

Each run of the plugin writes a one-row ``*_summary.csv``. This script collects
them into a single CSV sorted by configuration, which is the table the
evaluation chapter is built from.

Usage:
    python aggregate_runs.py <directory> [-o results.csv]
    python aggregate_runs.py <directory> --check

``--check`` reports conditions that would undermine the measurements: Debug
builds, runs that never cleared warm-up, histogram range overflows, and
configurations measured fewer than three times.
"""

import argparse
import csv
import glob
import os
import sys
from collections import defaultdict

SORT_KEYS = ("sample_rate_hz", "max_block_size", "dsp_complexity", "run_marker")

# Columns most useful when scanning the table by eye.
PREVIEW = (
    "sample_rate_hz", "max_block_size", "dsp_complexity", "run_marker",
    "blocks_measured", "mean_ms", "p99_ms", "max_ms",
    "mean_load_percent", "max_load_percent", "overload_rate_percent",
)


def load(directory):
    rows = []
    pattern = os.path.join(directory, "**", "*_summary.csv")
    for path in sorted(glob.glob(pattern, recursive=True)):
        try:
            with open(path, newline="") as handle:
                for row in csv.DictReader(handle):
                    row["_source_file"] = os.path.basename(path)
                    rows.append(row)
        except OSError as error:
            print(f"warning: could not read {path}: {error}", file=sys.stderr)
    return rows


def sort_key(row):
    key = []
    for name in SORT_KEYS:
        try:
            key.append(float(row.get(name, 0) or 0))
        except ValueError:
            key.append(0.0)
    return tuple(key)


def check(rows):
    problems = []

    for row in rows:
        source = row["_source_file"]

        if row.get("build_type") != "Release":
            problems.append(
                f"{source}: built as {row.get('build_type')!r}; "
                "timings from an unoptimised build are not comparable"
            )

        if int(float(row.get("blocks_measured", 0) or 0)) == 0:
            problems.append(f"{source}: no blocks measured (warm-up never cleared)")

        overflow = int(float(row.get("histogram_overflow", 0) or 0))
        if overflow:
            problems.append(
                f"{source}: {overflow} blocks exceeded the histogram range; "
                "percentiles are truncated"
            )

    groups = defaultdict(list)
    for row in rows:
        groups[(row.get("sample_rate_hz"), row.get("max_block_size"),
                row.get("dsp_complexity"))].append(row)

    for (rate, block, dsp), group in sorted(groups.items()):
        if len(group) < 3:
            problems.append(
                f"sr={rate} buf={block} dsp={dsp}: only {len(group)} run(s); "
                "three or more are needed to judge repeatability"
            )

    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", help="directory holding the *_summary.csv files")
    parser.add_argument("-o", "--output", help="write the combined table here")
    parser.add_argument("--check", action="store_true",
                        help="report conditions that would undermine the measurements")
    args = parser.parse_args()

    rows = load(args.directory)
    if not rows:
        print(f"no *_summary.csv files found under {args.directory}", file=sys.stderr)
        return 1

    rows.sort(key=sort_key)
    print(f"{len(rows)} run(s) found\n")

    widths = {name: max(len(name), 12) for name in PREVIEW}
    print("  ".join(name.rjust(widths[name]) for name in PREVIEW))
    for row in rows:
        cells = []
        for name in PREVIEW:
            value = row.get(name, "")
            try:
                value = f"{float(value):.3f}"
            except (TypeError, ValueError):
                value = str(value)
            cells.append(value.rjust(widths[name]))
        print("  ".join(cells))

    if args.output:
        fieldnames = list(rows[0].keys())
        with open(args.output, "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nwritten to {args.output}")

    if args.check:
        problems = check(rows)
        print()
        if problems:
            print(f"{len(problems)} issue(s):")
            for problem in problems:
                print(f"  - {problem}")
            return 1
        print("no issues found")

    return 0


if __name__ == "__main__":
    sys.exit(main())
