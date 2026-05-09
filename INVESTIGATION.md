# nfl3 — Investigation Report

**Date:** May 9, 2026  
**Status:** Draft

---

## 1. Project Goal

Build a C++ application to track the state of NFL games throughout the season, evaluate playoff tiebreaker rules to determine exact standings, and run Monte Carlo simulations to estimate the probability of various end-of-season outcomes.

---

## 2. Core Features

### 2.1 Schedule & Results Tracking
- Maintain a CSV file representing the full NFL schedule (all 32 teams, 18-week regular season).
- Each row represents a game with fields such as: week, date, home team, away team, home score, away score, status (scheduled / final).
- The CSV is human-editable so results can be entered manually as games are played.
- Future: automate result ingestion from a web source (see §4).

### 2.2 Standings & Tiebreaker Evaluation
- Compute win/loss/tie records for all 32 teams, grouped by conference and division.
- Implement the full NFL playoff tiebreaker rules (division tiebreakers, conference tiebreakers) to produce exact playoff seedings and wild-card cutlines.
- Tiebreaker rules are a multi-step sequence (head-to-head, division record, common games, conference record, strength of victory, strength of schedule, etc.) and must be applied correctly.

### 2.3 Monte Carlo Simulation
- For games not yet played, simulate outcomes probabilistically.
- **Win probability model:** Not a simple 50/50 split, and not a full Elo system. Instead, fit a basic probabilistic model to historical data. Factors to consider:
  - **Home field advantage:** fit the historical home team win rate from past seasons (typically ~57%) as a baseline adjustment.
  - **Team strength from prior season:** use the previous year's win percentage (or point differential) as a proxy for team quality, decayed appropriately for roster changes.
  - The model should be parameterizable — weights are derived by fitting to past schedules/results, not hard-coded assumptions.
- The goal is a lightweight, explainable model that outperforms 50/50 without the complexity of full Elo maintenance.
- Run a large number of simulations (e.g., 10 000–100 000 iterations).
- For each simulation, record which teams make the playoffs and in which seed.
- Tally results across all simulations to produce probability estimates (e.g., "Team X has a 73% chance of making the playoffs as the #2 seed").

### 2.4 Output
- **ASCII output:** formatted standings table and playoff probability table printable to the terminal.
- **Web app:** a local web server (or generated static HTML) to display the same information in a browser.

---

## 3. Data Model

### CSV Schema (proposed: `data/schedule.csv`)

| Field       | Type   | Notes                                    |
|-------------|--------|------------------------------------------|
| week        | int    | 1–18 (regular season), 19–22 (playoffs)  |
| date        | string | YYYY-MM-DD                               |
| away_team   | string | 3-letter abbreviation (e.g., `KC`)       |
| home_team   | string | 3-letter abbreviation                    |
| away_score  | int    | -1 = not yet played                      |
| home_score  | int    | -1 = not yet played                      |
| status      | string | `scheduled` / `final` / `in_progress`   |

Teams will be stored with metadata in a separate `data/teams.csv` file (full name, abbreviation, conference, division). This file is static and maintained manually.

---

## 4. Data Sources

| Use case | Source |
|----------|--------|
| Historical data (1999–present) for backfitting win probability model | nflverse `games.csv` |
| Seeding current season schedule | nflverse `games.csv` |
| In-season live score updates | ESPN unofficial API |

### 4.1 Primary: nflverse

[nflverse](https://github.com/nflverse/nfldata) is the recommended primary source for historical and schedule data.

- **Free:** Data files hosted directly on GitHub Releases as downloadable CSV/Parquet — no API key, no account, no scraping.
- **Reliable:** Maintained by an active open-source analytics community; data is version-controlled.
- **Historical depth:** Game results back to **1999**, sufficient for backfitting the win probability model.

Key file: [`games.csv`](https://github.com/nflverse/nfldata/blob/master/data/games.csv) — one row per game with season, week, home/away team, final scores, location, and game type (regular season vs playoffs). Schema aligns closely with our `data/schedule.csv` design.

Direct download (no auth required):
```
https://github.com/nflverse/nfldata/raw/master/data/games.csv
```

### 4.2 Secondary: ESPN Unofficial API

The [ESPN unofficial JSON API](https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard) is suitable for **in-season live score updates** to automate `schedule.csv` result ingestion during the season. Not suitable for deep historical data.

- No API key required; returns JSON.
- Use [nlohmann/json](https://github.com/nlohmann/json) to parse in C++.

---

## 5. Architecture (proposed)

```
nfl3/
├── CMakeLists.txt
├── data/
│   ├── teams.csv          # static team metadata
│   └── schedule.csv       # schedule + results (editable)
├── src/
│   ├── main.cpp           # CLI entry point
│   ├── model/
│   │   ├── Team.h/.cpp    # team record, division, conference
│   │   ├── Game.h/.cpp    # game record (parsed from CSV)
│   │   └── Season.h/.cpp  # full season state
│   ├── standings/
│   │   ├── Standings.h/.cpp   # compute records from results
│   │   └── Tiebreaker.h/.cpp  # NFL tiebreaker rule chain
│   ├── sim/
│   │   └── MonteCarlo.h/.cpp  # simulation engine
│   ├── output/
│   │   ├── AsciiPrinter.h/.cpp
│   │   └── WebServer.h/.cpp   # simple embedded HTTP server
│   └── util/
│       └── CsvParser.h/.cpp
└── INVESTIGATION.md
```

Possible C++ libraries to consider:

| Library | Purpose |
|---------|---------|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | Single-header HTTP server for web app output |
| [nlohmann/json](https://github.com/nlohmann/json) | Parse JSON from ESPN API |
| Standard `<fstream>` | CSV reading/writing (simple enough to do without a library) |

---

## 6. Milestones

| # | Milestone | Description |
|---|-----------|-------------|
| 1 | Data layer | `teams.csv` + `schedule.csv` populated; CSV parser implemented |
| 2 | Standings | Compute records; basic ASCII standings output |
| 3 | Tiebreakers | Full NFL tiebreaker rules; exact playoff seeding |
| 4 | Unit tests | Unit tests for CSV parser, standings computation, and tiebreaker rule chain |
| 5 | Simulation | Monte Carlo engine; probabilistic playoff output |
| 6 | Web app | Embedded HTTP server serving HTML standings/simulation results |
| 7 | Data ingestion | Automated fetch from web source to update `schedule.csv` |
| 8 | End-to-end tests | Replay past seasons from nflverse historical data and verify computed playoff seedings match actual seedings; validate simulation output distributions against known outcomes |
| 9 | Playoff simulation | Postseason bracket simulation and Super Bowl probability (future) |

---

## 7. Code Coverage Goals

Code coverage targets for unit and end-to-end testing:

| Module | Target | Rationale |
|--------|--------|-----------|
| `model/` (Team, Game, Season) | 90%+ | Core data structures; high confidence required |
| `standings/` (Standings, Tiebreaker) | 95%+ | Playoff seeding correctness is critical; must match historical results |
| `sim/` (MonteCarlo) | 85%+ | Simulation logic; coverage of random cases and edge conditions |
| `util/` (CsvParser) | 80%+ | Utility parsing; lower risk but should handle malformed input |
| `output/` (AsciiPrinter, WebServer) | 70%+ | UI/output formatting; less critical to correctness |
| **Overall** | **80%+** | Establish a floor across the entire codebase |

**Testing strategy:**
- Use a C++ coverage tool (e.g., `gcov`, `llvm-cov`) to measure line and branch coverage.
- Unit tests drive coverage for individual modules.
- End-to-end tests provide coverage for integration paths (e.g., CSV → standings → tiebreaker logic).
- Focus first on critical path: standings computation and tiebreaker rule application.

---

## 8. Open Questions

- Which web source will be the primary data feed? **Resolved: nflverse for historical/schedule data; ESPN unofficial API for live in-season updates.**
- Should the web app be an embedded C++ HTTP server, or generate static HTML files? **Resolved: embedded C++ HTTP server (cpp-httplib).**
- Win probability model for simulation: ~~pure 50/50~~ fit a basic probabilistic model to historical data (home field advantage, prior-season team strength); not full Elo.
- Should playoff simulation include postseason bracket simulation, or only regular-season outcome probabilities? **Deferred: postseason bracket simulation is a future item; not in scope for initial design.**
- License / open-source intent? **Resolved: MIT License.**
