#include "BacktestFixtureLoader.h"
#include "CsvParser.h"
#include "model/Game.h"
#include "model/Team.h"

#include <stdexcept>

namespace {

const std::string& requireField(const CsvParser::Row& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end()) {
        throw std::runtime_error("Missing CSV field: " + key);
    }
    return it->second;
}

std::string makeSeasonPath(const std::string& historicalDir, int seasonYear) {
    return historicalDir + "/" + std::to_string(seasonYear) + ".csv";
}

} // namespace

Season BacktestFixtureLoader::loadSeasonFromCsv(const std::string& teamsPath,
                                                const std::string& gamesPath) {
    Season season;

    const auto teams = CsvParser::parse(teamsPath);
    for (const auto& row : teams) {
        season.addTeam(Team(requireField(row, "abbreviation"),
                            requireField(row, "full_name"),
                            requireField(row, "conference"),
                            requireField(row, "division")));
    }

    const auto games = CsvParser::parse(gamesPath);
    for (const auto& row : games) {
        season.addGame(Game(std::stoi(requireField(row, "week")),
                            requireField(row, "date"),
                            requireField(row, "home_team"),
                            requireField(row, "away_team"),
                            std::stoi(requireField(row, "home_score")),
                            std::stoi(requireField(row, "away_score")),
                            requireField(row, "status")));
    }

    return season;
}

std::vector<BacktestSeasonFixture> BacktestFixtureLoader::loadSeasons(
    const std::string& teamsPath,
    const std::string& historicalDir,
    const std::vector<int>& seasonYears) {
    std::vector<BacktestSeasonFixture> fixtures;
    fixtures.reserve(seasonYears.size());

    for (int seasonYear : seasonYears) {
        fixtures.push_back({seasonYear,
                            loadSeasonFromCsv(teamsPath, makeSeasonPath(historicalDir, seasonYear))});
    }

    return fixtures;
}

std::vector<BacktestSeasonFixture> BacktestFixtureLoader::loadPriorSeasons(
    const std::string& teamsPath,
    const std::string& historicalDir,
    int targetSeason,
    int lookbackYears) {
    if (lookbackYears <= 0) {
        throw std::invalid_argument("lookbackYears must be positive");
    }

    std::vector<int> years;
    years.reserve(static_cast<size_t>(lookbackYears));
    for (int year = targetSeason - 1; year >= targetSeason - lookbackYears; --year) {
        years.push_back(year);
    }

    return loadSeasons(teamsPath, historicalDir, years);
}
