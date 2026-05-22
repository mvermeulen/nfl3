#!/usr/bin/env python3
"""Fetch live NFL game scores from ESPN scoreboard API and update schedule.csv in-place.

Usage:
  python scripts/fetch_live_scores.py [--week N] [--all] [--schedule path/to/schedule.csv]
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import urllib.request
from pathlib import Path
from typing import Dict, List, Any

ESPN_SCOREBOARD_URL = "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard"

# Map ESPN's team abbreviations to our canonical project abbreviations
ESPN_TEAM_MAP = {
    "LAR": "LA",
    "WSH": "WAS",
}


def normalize_team(code: str) -> str:
    return ESPN_TEAM_MAP.get(code, code)


def fetch_espn_week(week: int | None = None) -> List[Dict[str, Any]]:
    """Fetch ESPN scoreboard events for a specific week or default week."""
    url = ESPN_SCOREBOARD_URL
    if week is not None:
        url += f"?week={week}"

    print(f"Fetching live scores from: {url}")
    try:
        req = urllib.request.Request(
            url, 
            headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
        )
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode("utf-8"))
            return data.get("events", [])
    except Exception as e:
        print(f"Error fetching from ESPN: {e}", file=sys.stderr)
        return []


def parse_espn_games(events: List[Dict[str, Any]], target_week: int | None = None) -> List[Dict[str, Any]]:
    """Parse ESPN events list into structured game dicts."""
    parsed_games = []
    for event in events:
        competitions = event.get("competitions", [])
        if not competitions:
            continue
        comp = competitions[0]

        # Extract week from event if not explicitly forced
        try:
            week = target_week if target_week is not None else int(event.get("week", {}).get("number", 1))
        except (ValueError, TypeError):
            week = 1

        raw_date = event.get("date", "")
        game_date = raw_date.split("T")[0] if raw_date else ""

        status_type = comp.get("status", {}).get("type", {})
        status_name = status_type.get("name", "")

        # Map status to ours
        if status_name == "STATUS_FINAL":
            status = "final"
        elif "IN_PROGRESS" in status_name or "HALFTIME" in status_name:
            status = "in_progress"
        else:
            status = "upcoming"

        # Determine home and away
        home_team = ""
        away_team = ""
        home_score = 0
        away_score = 0

        for competitor in comp.get("competitors", []):
            raw_abbr = competitor.get("team", {}).get("abbreviation", "")
            abbr = normalize_team(raw_abbr)
            score = 0
            try:
                score = int(competitor.get("score", 0))
            except (ValueError, TypeError):
                pass

            if competitor.get("homeAway") == "home":
                home_team = abbr
                home_score = score
            else:
                away_team = abbr
                away_score = score

        parsed_games.append({
            "week": week,
            "date": game_date,
            "home_team": home_team,
            "away_team": away_team,
            "home_score": home_score,
            "away_score": away_score,
            "status": status,
        })
    return parsed_games


def get_nfl_season(date_str: str) -> int | None:
    """Calculate the NFL season year for a given date YYYY-MM-DD.
    
    NFL seasons cross calendar years (e.g., Week 18 played in Jan 2027 belongs to the 2026 season).
    Months 1 (Jan), 2 (Feb), 3 (Mar) map to the previous year.
    """
    if not date_str:
        return None
    try:
        parts = date_str.split("-")
        year = int(parts[0])
        month = int(parts[1])
        if month in (1, 2, 3):
            return year - 1
        return year
    except (ValueError, IndexError):
        return None


def update_schedule_csv(schedule_path: Path, new_games: List[Dict[str, Any]]) -> int:
    """Merge new games into schedule.csv and write back in-place. Returns number of games updated."""
    if not schedule_path.exists():
        print(f"Error: schedule file not found at {schedule_path}", file=sys.stderr)
        return 0

    # Read existing schedule rows
    fieldnames = ["week", "date", "home_team", "away_team", "home_score", "away_score", "status"]
    rows = []
    with schedule_path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)

    # Create mapping of (week, home_team, away_team) -> row index for quick lookup
    lookup = {}
    for i, r in enumerate(rows):
        key = (int(r["week"]), r["home_team"], r["away_team"])
        lookup[key] = i

    updates_count = 0
    for ng in new_games:
        key = (int(ng["week"]), ng["home_team"], ng["away_team"])
        if key in lookup:
            idx = lookup[key]
            old = rows[idx]

            # Verify that the fetched game belongs to the same NFL season as the schedule game
            fetched_season = get_nfl_season(ng["date"])
            scheduled_season = get_nfl_season(old["date"])
            if fetched_season is not None and scheduled_season is not None and fetched_season != scheduled_season:
                continue

            # Detect differences to count updates
            changed = (
                old["status"] != ng["status"] or
                int(old["home_score"]) != ng["home_score"] or
                int(old["away_score"]) != ng["away_score"] or
                (ng["date"] and old["date"] != ng["date"])
            )

            if changed:
                rows[idx]["status"] = ng["status"]
                rows[idx]["home_score"] = str(ng["home_score"])
                rows[idx]["away_score"] = str(ng["away_score"])
                if ng["date"]:
                    rows[idx]["date"] = ng["date"]
                updates_count += 1

    # Write back to CSV
    with schedule_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    return updates_count


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch and sync live NFL scores from ESPN scoreboard.")
    parser.add_argument("--week", type=int, choices=range(1, 19), help="Fetch scores for a specific week (1-18)")
    parser.add_argument("--all", action="store_true", help="Fetch and sync all 18 weeks of the regular season")
    parser.add_argument(
        "--schedule", 
        default="data/schedule.csv", 
        type=Path, 
        help="Path to the schedule.csv file to update (default: data/schedule.csv)"
    )

    args = parser.parse_args()
    schedule_path: Path = args.schedule

    if args.all:
        print("Synchronizing ALL 18 weeks of the regular season...")
        total_updates = 0
        for w in range(1, 19):
            events = fetch_espn_week(w)
            parsed = parse_espn_games(events, target_week=w)
            updates = update_schedule_csv(schedule_path, parsed)
            total_updates += updates
            print(f"  Week {w}: processed {len(parsed)} games, updated {updates} games.")
        print(f"Synchronization complete! Total games updated: {total_updates}")
    else:
        # Fetch individual week (or default)
        events = fetch_espn_week(args.week)
        parsed = parse_espn_games(events, target_week=args.week)
        if not parsed:
            print("No games found on scoreboard.", file=sys.stderr)
            return 1

        actual_week = parsed[0]["week"]
        updates = update_schedule_csv(schedule_path, parsed)
        print(f"Processed Week {actual_week}: found {len(parsed)} games, updated {updates} in schedule.csv.")
        
        # Display short summary
        print("\nSummary of games processed:")
        for g in parsed:
            home = g["home_team"]
            away = g["away_team"]
            hs = g["home_score"]
            as_ = g["away_score"]
            status = g["status"]
            print(f"  {away} @ {home} -- {as_} - {hs} ({status})")

    return 0


if __name__ == "__main__":
    sys.exit(main())
