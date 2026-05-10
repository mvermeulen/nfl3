#!/usr/bin/env python3
"""Generate historical NFL regular-season CSV fixtures from nflverse data.

This script creates data/historical/<year>.csv files in the schema used by nfl3:
week,date,home_team,away_team,home_score,away_score,status

Example:
  python scripts/generate_historical_data.py --start-year 2016 --end-year 2025
"""

from __future__ import annotations

import argparse
import csv
import sys
import urllib.request
from collections import defaultdict
from pathlib import Path
from typing import Dict, List

DEFAULT_SOURCE_URL = "https://raw.githubusercontent.com/nflverse/nfldata/master/data/games.csv"

TEAM_MAP = {
    "LAR": "LA",
    "STL": "LA",
    "SD": "LAC",
    "SDG": "LAC",
    "OAK": "LV",
    "WSH": "WAS",
}


def normalize_team_code(code: str) -> str:
    return TEAM_MAP.get(code, code)


def read_games_source(source: str) -> List[Dict[str, str]]:
    if source.startswith("http://") or source.startswith("https://"):
        with urllib.request.urlopen(source) as response:
            lines = (line.decode("utf-8") for line in response)
            return list(csv.DictReader(lines))

    source_path = Path(source)
    if not source_path.exists():
        raise FileNotFoundError(f"Source file not found: {source}")

    with source_path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def generate_historical_csvs(
    rows: List[Dict[str, str]],
    output_dir: Path,
    start_year: int,
    end_year: int,
) -> Dict[int, int]:
    output_dir.mkdir(parents=True, exist_ok=True)

    by_year: Dict[int, List[Dict[str, str]]] = defaultdict(list)

    for row in rows:
        try:
            season = int(row["season"])
        except (KeyError, TypeError, ValueError):
            continue

        if season < start_year or season > end_year:
            continue

        if row.get("game_type") != "REG":
            continue

        home_score_raw = row.get("home_score", "")
        away_score_raw = row.get("away_score", "")
        if not home_score_raw or not away_score_raw:
            continue

        try:
            week = int(row["week"])
            home_score = int(float(home_score_raw))
            away_score = int(float(away_score_raw))
        except (TypeError, ValueError, KeyError):
            continue

        by_year[season].append(
            {
                "week": str(week),
                "date": row.get("gameday", ""),
                "home_team": normalize_team_code(row.get("home_team", "")),
                "away_team": normalize_team_code(row.get("away_team", "")),
                "home_score": str(home_score),
                "away_score": str(away_score),
                "status": "final",
                "_sort_game_id": row.get("game_id", ""),
            }
        )

    fieldnames = ["week", "date", "home_team", "away_team", "home_score", "away_score", "status"]
    counts: Dict[int, int] = {}

    for year in range(start_year, end_year + 1):
        season_rows = by_year.get(year, [])
        season_rows.sort(key=lambda r: (int(r["week"]), r["date"], r["_sort_game_id"]))

        out_path = output_dir / f"{year}.csv"
        with out_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for season_row in season_rows:
                writer.writerow({k: season_row[k] for k in fieldnames})

        counts[year] = len(season_rows)

    return counts


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate nfl3 historical season CSVs")
    parser.add_argument("--source", default=DEFAULT_SOURCE_URL, help="nflverse games.csv URL or local path")
    parser.add_argument("--start-year", type=int, required=True, help="first season year to generate")
    parser.add_argument("--end-year", type=int, required=True, help="last season year to generate")
    parser.add_argument(
        "--output-dir",
        default="data/historical",
        help="directory where <year>.csv files are written",
    )
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)

    if args.start_year > args.end_year:
        print("--start-year must be <= --end-year", file=sys.stderr)
        return 2

    rows = read_games_source(args.source)
    counts = generate_historical_csvs(
        rows=rows,
        output_dir=Path(args.output_dir),
        start_year=args.start_year,
        end_year=args.end_year,
    )

    print("Generated historical CSV files:")
    for year in range(args.start_year, args.end_year + 1):
        print(f"  {year}: {counts.get(year, 0)} games")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
