#include "catch.hpp"
#include "util/BacktestFixtureLoader.h"

#include <stdexcept>
#include <string>

TEST_CASE("BacktestFixtureLoader loads explicit seasons", "[backtest][loader]") {
    const std::string teamsPath = std::string(NFL3_SOURCE_DIR) + "/data/teams.csv";
    const std::string historicalDir = std::string(NFL3_SOURCE_DIR) + "/data/historical";

    const auto fixtures = BacktestFixtureLoader::loadSeasons(teamsPath, historicalDir, {2025, 2024, 2022});

    REQUIRE(fixtures.size() == 3);
    REQUIRE(fixtures[0].seasonYear == 2025);
    REQUIRE(fixtures[1].seasonYear == 2024);
    REQUIRE(fixtures[2].seasonYear == 2022);

    REQUIRE(fixtures[0].season.allTeams().size() == 32);
    REQUIRE(fixtures[0].season.allGames().size() == 272);

    // 2022 had one canceled game and therefore has 271 regular-season games.
    REQUIRE(fixtures[2].season.allGames().size() == 271);
}

TEST_CASE("BacktestFixtureLoader loads prior seasons window", "[backtest][loader]") {
    const std::string teamsPath = std::string(NFL3_SOURCE_DIR) + "/data/teams.csv";
    const std::string historicalDir = std::string(NFL3_SOURCE_DIR) + "/data/historical";

    const auto fixtures = BacktestFixtureLoader::loadPriorSeasons(teamsPath, historicalDir, 2026, 4);

    REQUIRE(fixtures.size() == 4);
    REQUIRE(fixtures[0].seasonYear == 2025);
    REQUIRE(fixtures[1].seasonYear == 2024);
    REQUIRE(fixtures[2].seasonYear == 2023);
    REQUIRE(fixtures[3].seasonYear == 2022);
}

TEST_CASE("BacktestFixtureLoader validates lookback years", "[backtest][loader]") {
    const std::string teamsPath = std::string(NFL3_SOURCE_DIR) + "/data/teams.csv";
    const std::string historicalDir = std::string(NFL3_SOURCE_DIR) + "/data/historical";

    REQUIRE_THROWS_AS(
        BacktestFixtureLoader::loadPriorSeasons(teamsPath, historicalDir, 2026, 0),
        std::invalid_argument
    );
}
