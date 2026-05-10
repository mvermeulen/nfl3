# nfl3 — NFL Game State Tracker and Playoff Simulator

C++ application for tracking NFL game state, computing playoff tiebreaker seedings, and running Monte Carlo simulations of potential season outcomes.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/nfl3
```

See [INVESTIGATION.md](INVESTIGATION.md) for project goals and architecture.

## Historical Data Refresh

When a full season is complete (for example, 2026), regenerate historical CSV files with:

```bash
python scripts/generate_historical_data.py --start-year 2016 --end-year 2026
```

Notes:

- Source defaults to nflverse `games.csv` and includes regular-season games only.
- Team abbreviations are normalized for this project (`OAK->LV`, `LAR/STL->LA`, `SD/SDG->LAC`, `WSH->WAS`).
- 2022 intentionally has 271 games because `BUF-CIN` was canceled.

## Backtest Fixture Loader

Use `BacktestFixtureLoader` to load multiple prior seasons for weighting experiments:

- Load an explicit set of seasons with `loadSeasons(...)`.
- Load a rolling prior window with `loadPriorSeasons(..., targetSeason, lookbackYears)`.

Header: `src/util/BacktestFixtureLoader.h`

## Rule-Era Aware Simulation

`MonteCarlo` now adjusts assumptions by season era instead of relying on fixed modern defaults:

- Scheduled games per team are inferred from fixture data, with a year-based fallback (16 before 2021, 17 from 2021 onward).
- Wild-card spots are selected by era (2 before 2020, 3 from 2020 onward).

This keeps backtest weighting experiments consistent with historical rulesets.

## Tiebreak Procedure Fidelity

`Tiebreaker::breakTie(...)` follows iterative elimination semantics:

- Select one winner from the currently tied group.
- Remove that team.
- Restart the full tiebreak process for the remaining teams.

Implemented criteria include:

- Head-to-head (including multi-team head-to-head percentage).
- Division record for division ties.
- Common games, only when each tied team has at least 4 common games.
- Conference record.
- Strength of victory.
- Strength of schedule.

## Historical Replay And Regression Coverage

The test suite includes full-season historical replay assertions for 2016-2025:

- Expected team records and division standings.
- Full conference ordering assertions to catch tie-resolution drift.

It also includes focused procedural regressions for:

- 3-team and 4-team elimination/restart sequences.
- Conference-mode behavior differences from division-mode behavior.
- Common-games minimum applicability gates.
- Mirror cases that verify stable ordering under swapped tie shapes.

Run tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
