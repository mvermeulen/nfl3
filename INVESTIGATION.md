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

### 2.4 Impact Analysis

For each game scheduled in the coming week, compute the marginal impact of that single game on the playoff probabilities of the teams involved. This enables users to understand which games are "must-win" (high variance in outcome) vs. "decided" (low variance).

**Methodology:**
1. Run a baseline 100k-iteration simulation with the current schedule.
2. For each unplayed game in the next week:
   - Create two hypothetical scenarios: home team wins, away team wins.
   - Re-run 100k-iteration simulation for each scenario.
   - Record playoff probability delta for both teams.
3. Display results sorted by impact magnitude.

**Output:** Table showing each next-week game with:
- Home team | Away team | Impact on Home team's playoff prob (%) | Impact on Away team's playoff prob (%)
- Example: "KC vs. Buffalo: KC +15% / Buffalo -18%" indicates this game is crucial for both teams' playoff odds.

### 2.5 Output
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

### Historical Season Data

Historical seasons (1999–present) from nflverse are cached locally in CSV format under `data/historical/`:

| File | Schema | Purpose |
|------|--------|---------|
| `data/historical/1999.csv`, `data/historical/2000.csv`, ... | Same as `schedule.csv` (week, date, home_team, away_team, home_score, away_score, status) | Backfitting win probability model; validating computed playoff seedings |

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

The [ESPN unofficial JSON API](https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard) is suitable for **in-season live score updates** to automate `schedule.csv` result ingestion during the season.

- No API key required; returns JSON.
- Use [nlohmann/json](https://github.com/nlohmann/json) to parse in C++.

### 4.3 Historical Data Caching

Historical seasons are fetched once from nflverse and cached locally in `data/historical/` as CSV files matching the `schedule.csv` schema. This avoids repeated network calls and enables offline model fitting and validation. Population of this cache is a one-time bootstrap step (or periodic refresh task, e.g., annual).

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

## 6. User Interface Design

### 6.1 CLI Invocation Model

The program is a command-line tool with a simple invocation pattern:

```bash
./nfl3 [COMMAND] [OPTIONS]
```

**Commands:**
- `status` — Compute and display current standings + playoff scenarios (default if no command given).
- `simulate [N]` — Run N Monte Carlo simulations (default: 100,000) and display playoff probability distributions.
- `impact` — Analyze the impact of each game in the coming week on playoff probabilities for all teams involved.
- `load-schedule <PATH>` — Load schedule CSV from custom path (default: `data/schedule.csv`).
- `web [PORT]` — Start HTTP server on given port (default: 8080); opens local browser to standings dashboard.
- `backfit-model <YEAR>` — Load historical season from local cache (`data/historical/<YEAR>.csv`) and fit win probability model; output fitted coefficients.

**Examples:**
```bash
./nfl3 status                      # Show current standings
./nfl3 simulate 100000             # Run 100k simulations (default is 100k)
./nfl3 impact                      # Show impact of next week's games on playoff odds
./nfl3 web 9000                    # Start web server on :9000
./nfl3 backfit-model 2023          # Fit model on 2023 season from local cache
```

### 6.2 ASCII Output Format

**Standings table** (terminal-friendly):
```
┌─────────────────────────────────────────────────────────────┐
│ AFC EAST                                                    │
├──────────┬──────┬──────┬──────┬──────────┬──────────┐
│ Team     │ Wins │ Loss │ Ties │ Div Win% │ Playoffs │
├──────────┼──────┼──────┼──────┼──────────┼──────────┤
│ Miami    │   11 │    6 │    0 │   100%   │ #1 Seed  │
│ Buffalo  │    9 │    8 │    0 │    67%   │ #5 Seed  │
│ NYJ      │    5 │   12 │    0 │    33%   │   Out    │
│ NEP      │    4 │   13 │    0 │    0%    │   Out    │
└──────────┴──────┴──────┴──────┴──────────┴──────────┘
```

**Playoff probability table** (post-simulation):
```
┌────────────────────────────────────────────┐
│ Playoff Probability (100k simulations)     |
├──────────┬────────┬────────┬────────┐
│ Team     │ Make % │ Seed # │ Super % │
├──────────┼────────┼────────┼────────┤
│ Miami    │  98.5% │ #1-#5  │  34.2% │
│ Buffalo  │  62.3% │ #5-#6  │  8.7%  │
│ NYJ      │  18.1% │ WC     │  0.3%  │
│ NEP      │   0.8% │ WC     │  0.0%  │
└──────────┴────────┴────────┴────────┘
```

**Game impact table** (next week's games):
```
┌────────────────────────────────────────────────────────┐
│ Impact Analysis — Week 14 Games                        │
├─────────────────┬─────────────────┬──────────┬────────┤
│ Away Team       │ Home Team        │ Impact % │ Importance │
├─────────────────┼─────────────────┼──────────┼────────┤
│ KC (Δ +12%)     │ Miami (Δ -14%)   │  13%    │ Critical   │
│ Buffalo (Δ +8%) │ NYJ (Δ -9%)      │  8.5%   │ High       │
│ NE (Δ +0.5%)    │ Baltimore (no Δ) │  0.2%   │ Minimal    │
└─────────────────┴─────────────────┴──────────┴────────┘
```

### 6.3 Web App Architecture

**Endpoints:**
- `GET /` — Redirect to `/standings`
- `GET /standings` — HTML dashboard showing current standings, division ranks, tiebreaker state
- `GET /api/standings` — JSON response (teams, records, playoff seeds, tiebreaker details)
- `GET /api/simulation?iterations=100000` — JSON response (playoff probability data; default: 100,000)
- `GET /api/impact` — JSON response (impact of each next-week game on team playoff probabilities)
- `POST /api/update-result` — Accept manual game result entry (home_team, away_team, home_score, away_score); update internal state; recalculate standings
- `GET /impact` — HTML dashboard showing impact analysis for next week's games
- `GET /simulation` — HTML dashboard showing probability tables and charts (if simple charting lib used)

**Technologies:**
- Backend: cpp-httplib (single-threaded request dispatch)
- Frontend: Simple HTML/CSS + inline JavaScript (no build step)
- Data format: JSON for API responses; CSV for persistent storage

**Sample HTML page:**
```html
<!DOCTYPE html>
<html>
<head><title>NFL3 Standings</title></head>
<body>
  <h1>Current Standings</h1>
  <div id="standings"></div>
  <button onclick="runSimulation()">Simulate 100k Seasons</button>
  <div id="simulation"></div>
  <script>
    // Fetch standings from /api/standings; populate div
    // Fetch simulation results from /api/simulation; populate div
  </script>
</body>
</html>
```

### 6.4 User Workflows

**During off-season:**
1. Load previous season's historical data via `./nfl3 backfit-model 2024` (reads from `data/historical/2024.csv`).
2. Fit win probability model; save coefficients to config.
3. Load new season schedule from nflverse into `data/schedule.csv`.
4. Review standings (`./nfl3 status`).

**During regular season:**
1. Update `data/schedule.csv` manually (or via future automated ingestion) as games are played.
2. Run `./nfl3 status` to see current standings and tiebreakers.
3. Run `./nfl3 simulate` to run 100k simulations and estimate playoff odds for remaining weeks.
4. Run `./nfl3 impact` to understand which games next week have the most impact on playoff scenarios.
5. Use `./nfl3 web 8080` to launch interactive dashboard for continuous monitoring (includes impact analysis page).
6. Use `/api/update-result` endpoint (via web form) to log new game results real-time.

**What-if scenarios (future enhancement):**
- Edit `data/schedule.csv` hypothetically (change a team's remaining opponents or scores).
- Re-run simulation to see impact on playoff probability.

---

## 7. Milestones

| # | Milestone | Description |
|---|-----------|-------------|
| 1 | Data layer | `teams.csv` + `schedule.csv` populated; CSV parser implemented |
| 2 | Standings | Compute records; basic ASCII standings output |
| 3 | Tiebreakers | Full NFL tiebreaker rules; exact playoff seeding |
| 4 | Unit tests | Unit tests for CSV parser, standings computation, and tiebreaker rule chain |
| 5 | Simulation | Monte Carlo engine; probabilistic playoff output |
| 6 | Web app | Embedded HTTP server serving HTML standings/simulation results |
| 7 | Impact analysis | For each game in the coming week, measure how much that single game affects playoff probabilities for the teams involved; display in ASCII and web UI |
| 8 | Data ingestion | Automated fetch from web source to update `schedule.csv` |
| 9 | End-to-end tests | Replay past seasons from nflverse historical data and verify computed playoff seedings match actual seedings; validate simulation output distributions against known outcomes |
| 10 | Playoff simulation | Postseason bracket simulation and Super Bowl probability (future) |

---

## 8. Code Coverage Goals

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

### 8.1 Performance Measurements

While no explicit performance targets are set, instrumentation is needed to track and understand system behavior as it matures:

| Operation | Metric | Purpose |
|-----------|--------|---------|
| CSV parse (schedule.csv, teams.csv) | Time (ms) | Establish baseline for data ingestion |
| Standings computation (all 32 teams, full tiebreaker chain) | Time (ms) | Monitor tiebreaker algorithm efficiency |
| Monte Carlo simulation (N iterations, e.g. 100k) | Time (s); iterations/sec | Track simulation throughput |
| Web server startup | Time (ms) | Responsiveness of CLI → server launch |
| HTTP request handling (standings, simulation results) | Latency (ms); throughput (requests/sec) | User-facing responsiveness |

**Implementation:**
- Emit timing logs at key checkpoints (e.g., `LOG_INFO("Tiebreaker computation completed in 45 ms")`) during development.
- Build a simple benchmark harness to measure end-to-end latency under realistic workloads.
- Store baseline measurements in a log or configuration for comparison across versions.
- **No hard SLA targets needed initially**, but if simulations become the bottleneck, this data informs optimization priorities.

### 8.2 Concurrency Model

**Resolved: Start single-threaded.** Concurrency is a concern primarily for the Monte Carlo simulation. If 100k iterations take longer than acceptable (e.g., >2 minutes), introduce multi-threading at that point. The simulation engine's embarrassingly parallel nature (each iteration independent) makes it an ideal candidate for thread pooling.

---

## 9. Open Questions

- Which web source will be the primary data feed? **Resolved: nflverse for historical/schedule data; ESPN unofficial API for live in-season updates.**
- Should the web app be an embedded C++ HTTP server, or generate static HTML files? **Resolved: embedded C++ HTTP server (cpp-httplib).**
- Win probability model for simulation: ~~pure 50/50~~ fit a basic probabilistic model to historical data (home field advantage, prior-season team strength); not full Elo.
- Should playoff simulation include postseason bracket simulation, or only regular-season outcome probabilities? **Deferred: postseason bracket simulation is a future item; not in scope for initial design.**
- License / open-source intent? **Resolved: MIT License.**
