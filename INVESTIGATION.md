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

Inputs come from the web. Candidate sources to investigate:

| Source | Notes |
|--------|-------|
| nfl.com | Official, may require scraping or unofficial API |
| ESPN API (unofficial) | `site.api.espn.com` endpoints, JSON, no auth required for schedule/scores |
| Pro Football Reference | Structured HTML tables, scrapable |
| MySportsFeeds / SportsData.io | Commercial APIs with free tiers |

**Decision needed:** Pick one primary source for schedule seeding and result updates. ESPN's unofficial API is a pragmatic starting point because it returns JSON and requires no API key.

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
| 4 | Simulation | Monte Carlo engine; probabilistic playoff output |
| 5 | Web app | Embedded HTTP server serving HTML standings/simulation results |
| 6 | Data ingestion | Automated fetch from web source to update `schedule.csv` |

---

## 7. Open Questions

- Which web source will be the primary data feed? (ESPN unofficial API recommended as first pass.)
- Should the web app be an embedded C++ HTTP server, or generate static HTML files?
- Win probability model for simulation: ~~pure 50/50~~ fit a basic probabilistic model to historical data (home field advantage, prior-season team strength); not full Elo.
- Should playoff simulation include postseason bracket simulation, or only regular-season outcome probabilities?
- License / open-source intent?
