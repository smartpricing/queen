#!/usr/bin/env python3
"""
07_diff.py -- compare two correctness snapshots produced by 06_snapshot.sql.

A snapshot file is the raw stdout of `psql ... -f 06_snapshot.sql`. It contains:
  --- BEGIN snapshot label=<L> ---
  <CSV: stat_key,total_messages,pending_messages,newest_at>
  ...
  --- END snapshot ---
  --- BEGIN summary label=<L> ---
  <CSV one line: label,rows_n,sum_total,sum_pending,must_match_md5>
  --- END summary ---

Compares two such files and exits:
  0  if the must-match contract is satisfied
  1  if rows differ in must-match columns (with a few example diffs printed)
  2  if files cannot be parsed

Usage:
  ./07_diff.py BASELINE.txt CANDIDATE.txt
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Iterator


def parse(path: Path) -> tuple[dict[str, tuple[str, str, str]], dict[str, str]]:
    """Returns (rows_by_stat_key, summary_dict)."""
    text = path.read_text()

    # Pull the snapshot section
    snap_start = text.find("--- BEGIN snapshot")
    snap_end = text.find("--- END snapshot")
    if snap_start < 0 or snap_end < 0:
        raise SystemExit(f"{path}: missing snapshot section")
    snap_body = text[snap_start:snap_end].splitlines()[1:]  # drop header line

    rows: dict[str, tuple[str, str, str]] = {}
    for line in snap_body:
        line = line.rstrip("\n")
        if not line.strip():
            continue
        parts = line.split(",", 3)
        if len(parts) != 4:
            continue  # skip junk
        stat_key, total, pending, newest = parts
        rows[stat_key] = (total, pending, newest)

    # Summary section
    sum_start = text.find("--- BEGIN summary")
    sum_end = text.find("--- END summary")
    if sum_start < 0 or sum_end < 0:
        raise SystemExit(f"{path}: missing summary section")
    summary_body = text[sum_start:sum_end].splitlines()[1:]
    summary_line = ""
    for line in summary_body:
        line = line.strip()
        if line and not line.startswith("---"):
            summary_line = line
            break
    if not summary_line:
        raise SystemExit(f"{path}: empty summary")
    fields = summary_line.split(",")
    if len(fields) < 5:
        raise SystemExit(f"{path}: malformed summary line: {summary_line!r}")
    summary = {
        "label": fields[0],
        "rows_n": fields[1],
        "sum_total": fields[2],
        "sum_pending": fields[3],
        "must_match_md5": fields[4],
    }
    return rows, summary


def example_diffs(
    base: dict[str, tuple[str, str, str]],
    cand: dict[str, tuple[str, str, str]],
    limit: int = 10,
) -> Iterator[str]:
    only_in_base = sorted(set(base) - set(cand))
    only_in_cand = sorted(set(cand) - set(base))
    differs = sorted(k for k in base.keys() & cand.keys() if base[k] != cand[k])

    yield f"  - rows only in baseline: {len(only_in_base)}"
    yield f"  - rows only in candidate: {len(only_in_cand)}"
    yield f"  - rows differing on must-match cols: {len(differs)}"

    for k in (only_in_base + only_in_cand)[:limit]:
        yield f"    [missing] {k}"
    for k in differs[:limit]:
        yield (
            f"    [diff]    {k}\n"
            f"        baseline:  total={base[k][0]} pending={base[k][1]} newest={base[k][2]}\n"
            f"        candidate: total={cand[k][0]} pending={cand[k][1]} newest={cand[k][2]}"
        )


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    base_path = Path(sys.argv[1])
    cand_path = Path(sys.argv[2])

    base_rows, base_sum = parse(base_path)
    cand_rows, cand_sum = parse(cand_path)

    print(f"baseline:  {base_path}  rows={base_sum['rows_n']}  md5={base_sum['must_match_md5']}")
    print(f"candidate: {cand_path}  rows={cand_sum['rows_n']}  md5={cand_sum['must_match_md5']}")

    if base_sum["must_match_md5"] == cand_sum["must_match_md5"]:
        print("OK: must-match md5 hashes are identical -- correctness contract satisfied.")
        return 0

    print("FAIL: must-match md5 hashes differ. Investigating row-level differences:")
    for line in example_diffs(base_rows, cand_rows):
        print(line)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
