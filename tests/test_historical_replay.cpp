#include "catch.hpp"
#include "util/CsvParser.h"
#include "model/Season.h"
#include "model/Team.h"
#include "model/Game.h"
#include <map>
#include <string>
#include <stdexcept>
#include <vector>
#include <iostream>

namespace {

const std::string& requireField(const CsvParser::Row& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end()) {
        throw std::runtime_error("Missing CSV field: " + key);
    }
    return it->second;
}

Season loadSeasonFromCsv(const std::string& teamsPath, const std::string& gamesPath) {
    Season season;

    const auto teams = CsvParser::parse(teamsPath);
    for (const auto& row : teams) {
        season.addTeam(Team(
            requireField(row, "abbreviation"),
            requireField(row, "full_name"),
            requireField(row, "conference"),
            requireField(row, "division")
        ));
    }

    const auto games = CsvParser::parse(gamesPath);
    for (const auto& row : games) {
        season.addGame(Game(
            std::stoi(requireField(row, "week")),
            requireField(row, "date"),
            requireField(row, "home_team"),
            requireField(row, "away_team"),
            std::stoi(requireField(row, "home_score")),
            std::stoi(requireField(row, "away_score")),
            requireField(row, "status")
        ));
    }

    return season;
}

void requireDivisionOrder(const Season& season,
                          const std::string& division,
                          const std::vector<std::string>& expectedOrder) {
    const auto standings = season.teamsByDivision(division);
    REQUIRE(standings.size() == expectedOrder.size());

    for (size_t i = 0; i < expectedOrder.size(); ++i) {
        REQUIRE(standings[i]->abbreviation() == expectedOrder[i]);
    }
}

void requireConferenceOrder(const Season& season,
                             const std::string& conference,
                             const std::vector<std::string>& expectedOrder) {
    const auto standings = season.teamsByConference(conference);
    REQUIRE(standings.size() == expectedOrder.size());

    for (size_t i = 0; i < expectedOrder.size(); ++i) {
        REQUIRE(standings[i]->abbreviation() == expectedOrder[i]);
    }
}

void requireTeamPrecedes(const std::vector<Team*>& standings,
                         const std::string& earlier,
                         const std::string& later) {
    auto earlierIt = std::find_if(standings.begin(), standings.end(), [&](const Team* team) {
        return team->abbreviation() == earlier;
    });
    auto laterIt = std::find_if(standings.begin(), standings.end(), [&](const Team* team) {
        return team->abbreviation() == later;
    });

    REQUIRE(earlierIt != standings.end());
    REQUIRE(laterIt != standings.end());
    REQUIRE(std::distance(standings.begin(), earlierIt) < std::distance(standings.begin(), laterIt));
}

}  // namespace

TEST_CASE("Historical 2025 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2025.csv"
    );
    season.computeStandings();

    SECTION("Loads the full regular season") {
        REQUIRE(season.allTeams().size() == 32);
        REQUIRE(season.allGames().size() == 272);
    }

    SECTION("Pins the 2025 tie-break scenarios") {
        const Team* den = season.getTeam("DEN");
        const Team* ne = season.getTeam("NE");
        const Team* car = season.getTeam("CAR");
        const Team* tb = season.getTeam("TB");
        const Team* atl = season.getTeam("ATL");
        const Team* la = season.getTeam("LA");
        const Team* sf = season.getTeam("SF");

        REQUIRE(den != nullptr);
        REQUIRE(ne != nullptr);
        REQUIRE(car != nullptr);
        REQUIRE(tb != nullptr);
        REQUIRE(atl != nullptr);
        REQUIRE(la != nullptr);
        REQUIRE(sf != nullptr);

        REQUIRE(den->wins() == 14);
        REQUIRE(den->losses() == 3);
        REQUIRE(ne->wins() == 14);
        REQUIRE(ne->losses() == 3);
        REQUIRE(car->wins() == 8);
        REQUIRE(car->losses() == 9);
        REQUIRE(tb->wins() == 8);
        REQUIRE(tb->losses() == 9);
        REQUIRE(atl->wins() == 8);
        REQUIRE(atl->losses() == 9);
        REQUIRE(la->wins() == 12);
        REQUIRE(la->losses() == 5);
        REQUIRE(sf->wins() == 12);
        REQUIRE(sf->losses() == 5);

        const auto afcStandings = season.teamsByConference("AFC");
        const auto nfcStandings = season.teamsByConference("NFC");
        const auto nfcSouth = season.teamsByDivision("NFC South");
        const auto nfcWest = season.teamsByDivision("NFC West");

        requireTeamPrecedes(afcStandings, "DEN", "NE");
        requireTeamPrecedes(nfcStandings, "CAR", "TB");
        requireTeamPrecedes(nfcStandings, "CAR", "ATL");
        requireTeamPrecedes(nfcSouth, "CAR", "TB");
        requireTeamPrecedes(nfcSouth, "TB", "ATL");
        requireTeamPrecedes(nfcWest, "LA", "SF");
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"DEN", "NE", "JAX", "HOU", "BUF", "LAC", "PIT", "IND", "BAL", "MIA", "CIN", "KC", "CLE", "LV", "NYJ", "TEN"});
        requireConferenceOrder(season, "NFC", {"SEA", "LA", "SF", "CHI", "PHI", "GB", "MIN", "DET", "CAR", "TB", "ATL", "DAL", "NO", "WAS", "NYG", "ARI"});
    }
}

TEST_CASE("Historical 2024 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2024.csv"
    );
    season.computeStandings();

    SECTION("Loads the full regular season") {
        REQUIRE(season.allTeams().size() == 32);
        REQUIRE(season.allGames().size() == 272);
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"BUF", "MIA", "NYJ", "NE"});
        requireDivisionOrder(season, "AFC North", {"BAL", "PIT", "CIN", "CLE"});
        requireDivisionOrder(season, "AFC South", {"HOU", "IND", "JAX", "TEN"});
        requireDivisionOrder(season, "AFC West", {"KC", "LAC", "DEN", "LV"});
        requireDivisionOrder(season, "NFC East", {"PHI", "WAS", "DAL", "NYG"});
        requireDivisionOrder(season, "NFC North", {"DET", "MIN", "GB", "CHI"});
        requireDivisionOrder(season, "NFC South", {"TB", "ATL", "CAR", "NO"});
        requireDivisionOrder(season, "NFC West", {"LA", "SEA", "ARI", "SF"});
    }

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {8, 9}},
            {"ATL", {8, 9}},
            {"BAL", {12, 5}},
            {"BUF", {13, 4}},
            {"CAR", {5, 12}},
            {"CHI", {5, 12}},
            {"CIN", {9, 8}},
            {"CLE", {3, 14}},
            {"DAL", {7, 10}},
            {"DEN", {10, 7}},
            {"DET", {15, 2}},
            {"GB", {11, 6}},
            {"HOU", {10, 7}},
            {"IND", {8, 9}},
            {"JAX", {4, 13}},
            {"KC", {15, 2}},
            {"LA", {10, 7}},
            {"LAC", {11, 6}},
            {"LV", {4, 13}},
            {"MIA", {8, 9}},
            {"MIN", {14, 3}},
            {"NE", {4, 13}},
            {"NO", {5, 12}},
            {"NYG", {3, 14}},
            {"NYJ", {5, 12}},
            {"PHI", {14, 3}},
            {"PIT", {10, 7}},
            {"SEA", {10, 7}},
            {"SF", {6, 11}},
            {"TB", {10, 7}},
            {"TEN", {3, 14}},
            {"WAS", {12, 5}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
            REQUIRE(team->ties() == 0);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"KC", "BUF", "BAL", "LAC", "PIT", "DEN", "HOU", "CIN", "IND", "MIA", "NYJ", "LV", "JAX", "NE", "CLE", "TEN"});
        requireConferenceOrder(season, "NFC", {"DET", "PHI", "MIN", "WAS", "GB", "LA", "SEA", "TB", "ATL", "ARI", "DAL", "SF", "CHI", "CAR", "NO", "NYG"});
    }
}

TEST_CASE("Historical 2023 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2023.csv"
    );
    season.computeStandings();

    SECTION("Loads the full regular season") {
        REQUIRE(season.allTeams().size() == 32);
        REQUIRE(season.allGames().size() == 272);
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"BUF", "MIA", "NYJ", "NE"});
        requireDivisionOrder(season, "AFC North", {"BAL", "CLE", "PIT", "CIN"});
        requireDivisionOrder(season, "AFC South", {"HOU", "JAX", "IND", "TEN"});
        requireDivisionOrder(season, "AFC West", {"KC", "LV", "DEN", "LAC"});
        requireDivisionOrder(season, "NFC East", {"DAL", "PHI", "NYG", "WAS"});
        requireDivisionOrder(season, "NFC North", {"DET", "GB", "MIN", "CHI"});
        requireDivisionOrder(season, "NFC South", {"TB", "NO", "ATL", "CAR"});
        requireDivisionOrder(season, "NFC West", {"SF", "LA", "SEA", "ARI"});
    }

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {4, 13}},
            {"ATL", {7, 10}},
            {"BAL", {13, 4}},
            {"BUF", {11, 6}},
            {"CAR", {2, 15}},
            {"CHI", {7, 10}},
            {"CIN", {9, 8}},
            {"CLE", {11, 6}},
            {"DAL", {12, 5}},
            {"DEN", {8, 9}},
            {"DET", {12, 5}},
            {"GB", {9, 8}},
            {"HOU", {10, 7}},
            {"IND", {9, 8}},
            {"JAX", {9, 8}},
            {"KC", {11, 6}},
            {"LA", {10, 7}},
            {"LAC", {5, 12}},
            {"LV", {8, 9}},
            {"MIA", {11, 6}},
            {"MIN", {7, 10}},
            {"NE", {4, 13}},
            {"NO", {9, 8}},
            {"NYG", {6, 11}},
            {"NYJ", {7, 10}},
            {"PHI", {11, 6}},
            {"PIT", {10, 7}},
            {"SEA", {9, 8}},
            {"SF", {12, 5}},
            {"TB", {9, 8}},
            {"TEN", {6, 11}},
            {"WAS", {4, 13}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
            REQUIRE(team->ties() == 0);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"BAL", "BUF", "KC", "CLE", "MIA", "HOU", "PIT", "CIN", "JAX", "IND", "LV", "DEN", "NYJ", "TEN", "LAC", "NE"});
        requireConferenceOrder(season, "NFC", {"SF", "DAL", "DET", "PHI", "LA", "TB", "GB", "SEA", "NO", "MIN", "CHI", "ATL", "NYG", "WAS", "ARI", "CAR"});
    }
}


TEST_CASE("Historical 2022 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2022.csv"
    );
    season.computeStandings();

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {4, 13}}, {"ATL", {7, 10}}, {"BAL", {10, 7}}, {"BUF", {13, 3}},
            {"CAR", {7, 10}}, {"CHI", {3, 14}}, {"CIN", {12, 4}}, {"CLE", {7, 10}},
            {"DAL", {12, 5}}, {"DEN", {5, 12}}, {"DET", {9, 8}}, {"GB", {8, 9}},
            {"HOU", {3, 13}}, {"IND", {4, 12}}, {"JAX", {9, 8}}, {"KC", {14, 3}},
            {"LA", {5, 12}}, {"LAC", {10, 7}}, {"LV", {6, 11}}, {"MIA", {9, 8}},
            {"MIN", {13, 4}}, {"NE", {8, 9}}, {"NO", {7, 10}}, {"NYG", {9, 7}},
            {"NYJ", {7, 10}}, {"PHI", {14, 3}}, {"PIT", {9, 8}}, {"SEA", {9, 8}},
            {"SF", {13, 4}}, {"TB", {8, 9}}, {"TEN", {7, 10}}, {"WAS", {8, 8}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"BUF", "MIA", "NE", "NYJ"});
        requireDivisionOrder(season, "AFC North", {"CIN", "BAL", "PIT", "CLE"});
        requireDivisionOrder(season, "AFC South", {"JAX", "TEN", "IND", "HOU"});
        requireDivisionOrder(season, "AFC West", {"KC", "LAC", "LV", "DEN"});
        requireDivisionOrder(season, "NFC East", {"PHI", "DAL", "NYG", "WAS"});
        requireDivisionOrder(season, "NFC North", {"MIN", "DET", "GB", "CHI"});
        requireDivisionOrder(season, "NFC South", {"TB", "CAR", "NO", "ATL"});
        requireDivisionOrder(season, "NFC West", {"SF", "SEA", "LA", "ARI"});
    }

    SECTION("Matches final tie counts") {
        const std::map<std::string, int> expectedTies = {
            {"HOU", 1}, {"IND", 1}, {"NYG", 1}, {"WAS", 1},
        };
        for (const auto& [abbr, expectedTiesCount] : expectedTies) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->ties() == expectedTiesCount);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"KC", "BUF", "CIN", "LAC", "BAL", "MIA", "JAX", "PIT", "NE", "NYJ", "CLE", "TEN", "LV", "DEN", "IND", "HOU"});
        requireConferenceOrder(season, "NFC", {"PHI", "MIN", "SF", "DAL", "NYG", "SEA", "DET", "WAS", "GB", "TB", "CAR", "NO", "ATL", "LA", "ARI", "CHI"});
    }
}

TEST_CASE("Historical 2021 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2021.csv"
    );
    season.computeStandings();

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {11, 6}}, {"ATL", {7, 10}}, {"BAL", {8, 9}}, {"BUF", {11, 6}},
            {"CAR", {5, 12}}, {"CHI", {6, 11}}, {"CIN", {10, 7}}, {"CLE", {8, 9}},
            {"DAL", {12, 5}}, {"DEN", {7, 10}}, {"DET", {3, 13}}, {"GB", {13, 4}},
            {"HOU", {4, 13}}, {"IND", {9, 8}}, {"JAX", {3, 14}}, {"KC", {12, 5}},
            {"LA", {12, 5}}, {"LAC", {9, 8}}, {"LV", {10, 7}}, {"MIA", {9, 8}},
            {"MIN", {8, 9}}, {"NE", {10, 7}}, {"NO", {9, 8}}, {"NYG", {4, 13}},
            {"NYJ", {4, 13}}, {"PHI", {9, 8}}, {"PIT", {9, 7}}, {"SEA", {7, 10}},
            {"SF", {10, 7}}, {"TB", {13, 4}}, {"TEN", {12, 5}}, {"WAS", {7, 10}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"BUF", "NE", "MIA", "NYJ"});
        requireDivisionOrder(season, "AFC North", {"CIN", "PIT", "CLE", "BAL"});
        requireDivisionOrder(season, "AFC South", {"TEN", "IND", "HOU", "JAX"});
        requireDivisionOrder(season, "AFC West", {"KC", "LV", "LAC", "DEN"});
        requireDivisionOrder(season, "NFC East", {"DAL", "PHI", "WAS", "NYG"});
        requireDivisionOrder(season, "NFC North", {"GB", "MIN", "CHI", "DET"});
        requireDivisionOrder(season, "NFC South", {"TB", "NO", "ATL", "CAR"});
        requireDivisionOrder(season, "NFC West", {"LA", "ARI", "SF", "SEA"});
    }

    SECTION("Matches final tie counts") {
        const std::map<std::string, int> expectedTies = {
            {"DET", 1}, {"PIT", 1},
        };
        for (const auto& [abbr, expectedTiesCount] : expectedTies) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->ties() == expectedTiesCount);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"TEN", "KC", "BUF", "CIN", "LV", "NE", "PIT", "IND", "MIA", "LAC", "BAL", "CLE", "DEN", "NYJ", "HOU", "JAX"});
        requireConferenceOrder(season, "NFC", {"GB", "TB", "LA", "DAL", "ARI", "SF", "PHI", "NO", "MIN", "WAS", "SEA", "ATL", "CHI", "CAR", "NYG", "DET"});
    }
}

TEST_CASE("Historical 2020 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2020.csv"
    );
    season.computeStandings();

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {8, 8}}, {"ATL", {4, 12}}, {"BAL", {11, 5}}, {"BUF", {13, 3}},
            {"CAR", {5, 11}}, {"CHI", {8, 8}}, {"CIN", {4, 11}}, {"CLE", {11, 5}},
            {"DAL", {6, 10}}, {"DEN", {5, 11}}, {"DET", {5, 11}}, {"GB", {13, 3}},
            {"HOU", {4, 12}}, {"IND", {11, 5}}, {"JAX", {1, 15}}, {"KC", {14, 2}},
            {"LA", {10, 6}}, {"LAC", {7, 9}}, {"LV", {8, 8}}, {"MIA", {10, 6}},
            {"MIN", {7, 9}}, {"NE", {7, 9}}, {"NO", {12, 4}}, {"NYG", {6, 10}},
            {"NYJ", {2, 14}}, {"PHI", {4, 11}}, {"PIT", {12, 4}}, {"SEA", {12, 4}},
            {"SF", {6, 10}}, {"TB", {11, 5}}, {"TEN", {11, 5}}, {"WAS", {7, 9}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"BUF", "MIA", "NE", "NYJ"});
        requireDivisionOrder(season, "AFC North", {"PIT", "BAL", "CLE", "CIN"});
        requireDivisionOrder(season, "AFC South", {"TEN", "IND", "HOU", "JAX"});
        requireDivisionOrder(season, "AFC West", {"KC", "LV", "LAC", "DEN"});
        requireDivisionOrder(season, "NFC East", {"WAS", "NYG", "DAL", "PHI"});
        requireDivisionOrder(season, "NFC North", {"GB", "CHI", "MIN", "DET"});
        requireDivisionOrder(season, "NFC South", {"NO", "TB", "CAR", "ATL"});
        requireDivisionOrder(season, "NFC West", {"SEA", "LA", "ARI", "SF"});
    }

    SECTION("Matches final tie counts") {
        const std::map<std::string, int> expectedTies = {
            {"CIN", 1}, {"PHI", 1},
        };
        for (const auto& [abbr, expectedTiesCount] : expectedTies) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->ties() == expectedTiesCount);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"KC", "BUF", "PIT", "BAL", "CLE", "TEN", "IND", "MIA", "LV", "NE", "LAC", "DEN", "CIN", "HOU", "NYJ", "JAX"});
        requireConferenceOrder(season, "NFC", {"GB", "SEA", "NO", "TB", "LA", "CHI", "ARI", "MIN", "WAS", "DAL", "SF", "NYG", "CAR", "DET", "PHI", "ATL"});
    }
}

TEST_CASE("Historical 2019 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2019.csv"
    );
    season.computeStandings();

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {5, 10}}, {"ATL", {7, 9}}, {"BAL", {14, 2}}, {"BUF", {10, 6}},
            {"CAR", {5, 11}}, {"CHI", {8, 8}}, {"CIN", {2, 14}}, {"CLE", {6, 10}},
            {"DAL", {8, 8}}, {"DEN", {7, 9}}, {"DET", {3, 12}}, {"GB", {13, 3}},
            {"HOU", {10, 6}}, {"IND", {7, 9}}, {"JAX", {6, 10}}, {"KC", {12, 4}},
            {"LA", {9, 7}}, {"LAC", {5, 11}}, {"LV", {7, 9}}, {"MIA", {5, 11}},
            {"MIN", {10, 6}}, {"NE", {12, 4}}, {"NO", {13, 3}}, {"NYG", {4, 12}},
            {"NYJ", {7, 9}}, {"PHI", {9, 7}}, {"PIT", {8, 8}}, {"SEA", {11, 5}},
            {"SF", {13, 3}}, {"TB", {7, 9}}, {"TEN", {9, 7}}, {"WAS", {3, 13}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"NE", "BUF", "NYJ", "MIA"});
        requireDivisionOrder(season, "AFC North", {"BAL", "PIT", "CLE", "CIN"});
        requireDivisionOrder(season, "AFC South", {"HOU", "TEN", "IND", "JAX"});
        requireDivisionOrder(season, "AFC West", {"KC", "DEN", "LV", "LAC"});
        requireDivisionOrder(season, "NFC East", {"PHI", "DAL", "NYG", "WAS"});
        requireDivisionOrder(season, "NFC North", {"GB", "MIN", "CHI", "DET"});
        requireDivisionOrder(season, "NFC South", {"NO", "ATL", "TB", "CAR"});
        requireDivisionOrder(season, "NFC West", {"SF", "SEA", "LA", "ARI"});
    }

    SECTION("Matches final tie counts") {
        const std::map<std::string, int> expectedTies = {
            {"ARI", 1}, {"DET", 1},
        };
        for (const auto& [abbr, expectedTiesCount] : expectedTies) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->ties() == expectedTiesCount);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"BAL", "KC", "NE", "HOU", "BUF", "TEN", "PIT", "NYJ", "LV", "IND", "DEN", "JAX", "CLE", "LAC", "MIA", "CIN"});
        requireConferenceOrder(season, "NFC", {"SF", "GB", "NO", "SEA", "MIN", "LA", "PHI", "CHI", "DAL", "ATL", "TB", "ARI", "CAR", "NYG", "DET", "WAS"});
    }
}

TEST_CASE("Historical 2018 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2018.csv"
    );
    season.computeStandings();

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {3, 13}}, {"ATL", {7, 9}}, {"BAL", {10, 6}}, {"BUF", {6, 10}},
            {"CAR", {7, 9}}, {"CHI", {12, 4}}, {"CIN", {6, 10}}, {"CLE", {7, 8}},
            {"DAL", {10, 6}}, {"DEN", {6, 10}}, {"DET", {6, 10}}, {"GB", {6, 9}},
            {"HOU", {11, 5}}, {"IND", {10, 6}}, {"JAX", {5, 11}}, {"KC", {12, 4}},
            {"LA", {13, 3}}, {"LAC", {12, 4}}, {"LV", {4, 12}}, {"MIA", {7, 9}},
            {"MIN", {8, 7}}, {"NE", {11, 5}}, {"NO", {13, 3}}, {"NYG", {5, 11}},
            {"NYJ", {4, 12}}, {"PHI", {9, 7}}, {"PIT", {9, 6}}, {"SEA", {10, 6}},
            {"SF", {4, 12}}, {"TB", {5, 11}}, {"TEN", {9, 7}}, {"WAS", {7, 9}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"NE", "MIA", "BUF", "NYJ"});
        requireDivisionOrder(season, "AFC North", {"BAL", "PIT", "CLE", "CIN"});
        requireDivisionOrder(season, "AFC South", {"HOU", "IND", "TEN", "JAX"});
        requireDivisionOrder(season, "AFC West", {"KC", "LAC", "DEN", "LV"});
        requireDivisionOrder(season, "NFC East", {"DAL", "PHI", "WAS", "NYG"});
        requireDivisionOrder(season, "NFC North", {"CHI", "MIN", "GB", "DET"});
        requireDivisionOrder(season, "NFC South", {"NO", "ATL", "CAR", "TB"});
        requireDivisionOrder(season, "NFC West", {"LA", "SEA", "SF", "ARI"});
    }

    SECTION("Matches final tie counts") {
        const std::map<std::string, int> expectedTies = {
            {"CLE", 1}, {"GB", 1}, {"MIN", 1}, {"PIT", 1},
        };
        for (const auto& [abbr, expectedTiesCount] : expectedTies) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->ties() == expectedTiesCount);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"KC", "LAC", "NE", "HOU", "BAL", "IND", "PIT", "TEN", "CLE", "MIA", "DEN", "CIN", "BUF", "JAX", "NYJ", "LV"});
        requireConferenceOrder(season, "NFC", {"NO", "LA", "CHI", "SEA", "DAL", "PHI", "MIN", "ATL", "WAS", "CAR", "GB", "DET", "NYG", "TB", "SF", "ARI"});
    }
}

TEST_CASE("Historical 2017 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2017.csv"
    );
    season.computeStandings();

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {8, 8}}, {"ATL", {10, 6}}, {"BAL", {9, 7}}, {"BUF", {9, 7}},
            {"CAR", {11, 5}}, {"CHI", {5, 11}}, {"CIN", {7, 9}}, {"CLE", {0, 16}},
            {"DAL", {9, 7}}, {"DEN", {5, 11}}, {"DET", {9, 7}}, {"GB", {7, 9}},
            {"HOU", {4, 12}}, {"IND", {4, 12}}, {"JAX", {10, 6}}, {"KC", {10, 6}},
            {"LA", {11, 5}}, {"LAC", {9, 7}}, {"LV", {6, 10}}, {"MIA", {6, 10}},
            {"MIN", {13, 3}}, {"NE", {13, 3}}, {"NO", {11, 5}}, {"NYG", {3, 13}},
            {"NYJ", {5, 11}}, {"PHI", {13, 3}}, {"PIT", {13, 3}}, {"SEA", {9, 7}},
            {"SF", {6, 10}}, {"TB", {5, 11}}, {"TEN", {9, 7}}, {"WAS", {7, 9}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"NE", "BUF", "MIA", "NYJ"});
        requireDivisionOrder(season, "AFC North", {"PIT", "BAL", "CIN", "CLE"});
        requireDivisionOrder(season, "AFC South", {"JAX", "TEN", "IND", "HOU"});
        requireDivisionOrder(season, "AFC West", {"KC", "LAC", "LV", "DEN"});
        requireDivisionOrder(season, "NFC East", {"PHI", "DAL", "WAS", "NYG"});
        requireDivisionOrder(season, "NFC North", {"MIN", "DET", "GB", "CHI"});
        requireDivisionOrder(season, "NFC South", {"NO", "CAR", "ATL", "TB"});
        requireDivisionOrder(season, "NFC West", {"LA", "SEA", "ARI", "SF"});
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"NE", "PIT", "JAX", "KC", "TEN", "LAC", "BUF", "BAL", "CIN", "LV", "MIA", "DEN", "NYJ", "IND", "HOU", "CLE"});
        requireConferenceOrder(season, "NFC", {"PHI", "MIN", "LA", "NO", "CAR", "ATL", "SEA", "DET", "DAL", "ARI", "GB", "WAS", "SF", "TB", "CHI", "NYG"});
    }
}

TEST_CASE("Historical 2016 season replay", "[harness][historical]") {
    Season season = loadSeasonFromCsv(
        std::string(NFL3_SOURCE_DIR) + "/data/teams.csv",
        std::string(NFL3_SOURCE_DIR) + "/data/historical/2016.csv"
    );
    season.computeStandings();

    SECTION("Loads the full regular season") {
        REQUIRE(season.allTeams().size() == 32);
        REQUIRE(season.allGames().size() == 256);
    }

    SECTION("Matches final team records") {
        const std::map<std::string, std::pair<int, int>> expectedRecords = {
            {"ARI", {7, 8}}, {"ATL", {11, 5}}, {"BAL", {8, 8}}, {"BUF", {7, 9}},
            {"CAR", {6, 10}}, {"CHI", {3, 13}}, {"CIN", {6, 9}}, {"CLE", {1, 15}},
            {"DAL", {13, 3}}, {"DEN", {9, 7}}, {"DET", {9, 7}}, {"GB", {10, 6}},
            {"HOU", {9, 7}}, {"IND", {8, 8}}, {"JAX", {3, 13}}, {"KC", {12, 4}},
            {"LA", {4, 12}}, {"LAC", {5, 11}}, {"LV", {12, 4}}, {"MIA", {10, 6}},
            {"MIN", {8, 8}}, {"NE", {14, 2}}, {"NO", {7, 9}}, {"NYG", {11, 5}},
            {"NYJ", {5, 11}}, {"PHI", {7, 9}}, {"PIT", {11, 5}}, {"SEA", {10, 5}},
            {"SF", {2, 14}}, {"TB", {9, 7}}, {"TEN", {9, 7}}, {"WAS", {8, 7}},
        };

        for (const auto& [abbr, expected] : expectedRecords) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->wins() == expected.first);
            REQUIRE(team->losses() == expected.second);
        }
    }

    SECTION("Matches final division standings") {
        requireDivisionOrder(season, "AFC East", {"NE", "MIA", "BUF", "NYJ"});
        requireDivisionOrder(season, "AFC North", {"PIT", "BAL", "CIN", "CLE"});
        requireDivisionOrder(season, "AFC South", {"HOU", "TEN", "IND", "JAX"});
        requireDivisionOrder(season, "AFC West", {"KC", "LV", "DEN", "LAC"});
        requireDivisionOrder(season, "NFC East", {"DAL", "NYG", "WAS", "PHI"});
        requireDivisionOrder(season, "NFC North", {"GB", "DET", "MIN", "CHI"});
        requireDivisionOrder(season, "NFC South", {"ATL", "TB", "NO", "CAR"});
        requireDivisionOrder(season, "NFC West", {"SEA", "ARI", "LA", "SF"});
    }

    SECTION("Matches final tie counts") {
        const std::map<std::string, int> expectedTies = {
            {"ARI", 1}, {"CIN", 1}, {"SEA", 1}, {"WAS", 1},
        };
        for (const auto& [abbr, expectedTiesCount] : expectedTies) {
            const Team* team = season.getTeam(abbr);
            REQUIRE(team != nullptr);
            REQUIRE(team->ties() == expectedTiesCount);
        }
    }

    SECTION("Matches final conference standings") {
        requireConferenceOrder(season, "AFC", {"NE", "KC", "LV", "PIT", "MIA", "TEN", "DEN", "HOU", "BAL", "IND", "BUF", "CIN", "NYJ", "LAC", "JAX", "CLE"});
        requireConferenceOrder(season, "NFC", {"DAL", "ATL", "NYG", "SEA", "GB", "DET", "TB", "WAS", "MIN", "ARI", "PHI", "NO", "CAR", "LA", "CHI", "SF"});
    }
}
