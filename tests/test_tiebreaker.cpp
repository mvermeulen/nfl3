#include "catch.hpp"
#include "model/Tiebreaker.h"
#include "model/Team.h"
#include "model/Game.h"
#include <vector>
#include <map>

TEST_CASE("Tiebreaker head-to-head record", "[Tiebreaker]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;
    
    allTeams["KC"] = Team("KC", "Kansas City Chiefs", "AFC", "AFC West");
    allTeams["DEN"] = Team("DEN", "Denver Broncos", "AFC", "AFC West");
    
    SECTION("Team 1 wins head-to-head") {
        // KC home beats DEN away
        games.push_back(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        // KC away beats DEN home
        games.push_back(Game(2, "2026-09-17", "DEN", "KC", 20, 24, "final"));
        
        std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"]};
        auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");
        
        REQUIRE(result[0]->abbreviation() == "KC");
        REQUIRE(result[1]->abbreviation() == "DEN");
    }

    SECTION("Team 2 wins head-to-head") {
        // KC home loses to DEN away
        games.push_back(Game(1, "2026-09-10", "KC", "DEN", 20, 24, "final"));
        // KC away loses to DEN home
        games.push_back(Game(2, "2026-09-17", "DEN", "KC", 28, 20, "final"));
        
        std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"]};
        auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");
        
        REQUIRE(result[0]->abbreviation() == "DEN");
        REQUIRE(result[1]->abbreviation() == "KC");
    }

    SECTION("Head-to-head is tied") {
        // KC home beats DEN away
        games.push_back(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        // KC away loses to DEN home
        games.push_back(Game(2, "2026-09-17", "DEN", "KC", 24, 20, "final"));
        
        std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"]};
        auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");
        
        // Should move to next tiebreaker, order may vary
        REQUIRE(result.size() == 2);
    }
}

TEST_CASE("Tiebreaker with single team", "[Tiebreaker]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;
    
    Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
    allTeams["KC"] = team;
    
    std::vector<Team*> tied = {&team};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "");
    
    REQUIRE(result.size() == 1);
    REQUIRE(result[0]->abbreviation() == "KC");
}

TEST_CASE("Tiebreaker division record", "[Tiebreaker]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;
    
    Team team1("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team team2("DEN", "Denver Broncos", "AFC", "AFC West");
    
    // Set up division records: KC 2-1, DEN 1-1.
    team1.addDivisionWin();
    team1.addDivisionWin();
    team1.addDivisionLoss();
    team2.addDivisionWin();
    team2.addDivisionLoss();
    
    allTeams["KC"] = team1;
    allTeams["DEN"] = team2;
    
    SECTION("Team with better division record ranks higher") {
        std::vector<Team*> tied = {&team1, &team2};
        auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");
        
        REQUIRE(result[0]->abbreviation() == "KC");
        REQUIRE(result[1]->abbreviation() == "DEN");
    }
}

TEST_CASE("Tiebreaker conference record", "[Tiebreaker]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;
    
    Team team1("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team team2("DEN", "Denver Broncos", "AFC", "AFC West");
    
    // Set up conference records: KC 3-2, DEN 2-2.
    team1.addConferenceWin();
    team1.addConferenceWin();
    team1.addConferenceWin();
    team1.addConferenceLoss();
    team1.addConferenceLoss();
    team2.addConferenceWin();
    team2.addConferenceWin();
    team2.addConferenceLoss();
    team2.addConferenceLoss();
    
    allTeams["KC"] = team1;
    allTeams["DEN"] = team2;
    
    std::vector<Team*> tied = {&team1, &team2};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "");
    
    REQUIRE(result[0]->abbreviation() == "KC");
    REQUIRE(result[1]->abbreviation() == "DEN");
}

TEST_CASE("Tiebreaker strength of victory", "[Tiebreaker]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;
    
    Team team1("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team team2("DEN", "Denver Broncos", "AFC", "AFC West");
    Team opponentStrong("LAR", "LA Rams", "NFC", "NFC West");
    Team opponentWeak("LAC", "LA Chargers", "AFC", "AFC West");
    
    // Set up wins and opponent records
    team1.addWin();  // Beat strong opponent
    team2.addWin();  // Beat weak opponent
    opponentStrong.addWin();
    opponentStrong.addWin();
    opponentWeak.addWin();
    
    allTeams["KC"] = team1;
    allTeams["DEN"] = team2;
    allTeams["LAR"] = opponentStrong;
    allTeams["LAC"] = opponentWeak;
    
    // Add games (but strength of victory relies on opponent records which we set above)
    games.push_back(Game(1, "2026-09-10", "KC", "LAR", 24, 20, "final"));
    games.push_back(Game(1, "2026-09-10", "DEN", "LAC", 21, 20, "final"));
    
    std::vector<Team*> tied = {&team1, &team2};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "");
    
    REQUIRE(result[0]->abbreviation() == "KC");  // Beat stronger opponent
    REQUIRE(result[1]->abbreviation() == "DEN");
}

TEST_CASE("Tiebreaker 3+ team restart after first-place elimination", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team lac("LAC", "LA Chargers", "AFC", "AFC West");

    // Keep division record tied so the first-place decision reaches common games.
    for (int i = 0; i < 3; ++i) {
        kc.addDivisionWin();
        den.addDivisionWin();
        lac.addDivisionWin();
        kc.addDivisionLoss();
        den.addDivisionLoss();
        lac.addDivisionLoss();
    }

    allTeams["KC"] = kc;
    allTeams["DEN"] = den;
    allTeams["LAC"] = lac;
    allTeams["NYJ"] = Team("NYJ", "New York Jets", "AFC", "AFC East");
    allTeams["BUF"] = Team("BUF", "Buffalo Bills", "AFC", "AFC East");
    allTeams["MIA"] = Team("MIA", "Miami Dolphins", "AFC", "AFC East");
    allTeams["NE"] = Team("NE", "New England Patriots", "AFC", "AFC East");

    // Three-way head-to-head: each team is 2-2 (B>A, A>C, C>B cycles).
    games.emplace_back(1, "2026-09-10", "DEN", "KC", 27, 17, "final");
    games.emplace_back(2, "2026-09-17", "KC", "DEN", 14, 24, "final");
    games.emplace_back(3, "2026-09-24", "KC", "LAC", 31, 20, "final");
    games.emplace_back(4, "2026-10-01", "LAC", "KC", 13, 28, "final");
    games.emplace_back(5, "2026-10-08", "LAC", "DEN", 30, 16, "final");
    games.emplace_back(6, "2026-10-15", "DEN", "LAC", 10, 21, "final");

    // Common opponents for all three teams (minimum 4 games each):
    // KC = 4-0, DEN = 3-1, LAC = 2-2.
    games.emplace_back(7, "2026-10-22", "KC", "NYJ", 24, 10, "final");
    games.emplace_back(8, "2026-10-29", "KC", "BUF", 27, 20, "final");
    games.emplace_back(9, "2026-11-05", "KC", "MIA", 30, 14, "final");
    games.emplace_back(10, "2026-11-12", "KC", "NE", 17, 13, "final");

    games.emplace_back(11, "2026-11-19", "DEN", "NYJ", 21, 13, "final");
    games.emplace_back(12, "2026-11-26", "DEN", "BUF", 28, 24, "final");
    games.emplace_back(13, "2026-12-03", "DEN", "MIA", 20, 17, "final");
    games.emplace_back(14, "2026-12-10", "NE", "DEN", 24, 14, "final");

    games.emplace_back(15, "2026-12-17", "LAC", "NYJ", 26, 16, "final");
    games.emplace_back(16, "2026-12-24", "LAC", "BUF", 20, 13, "final");
    games.emplace_back(17, "2026-12-31", "MIA", "LAC", 27, 17, "final");
    games.emplace_back(18, "2027-01-07", "NE", "LAC", 23, 20, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"], &allTeams["LAC"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");

    // KC wins first on common-games percentage among all three.
    // Then the remaining two must restart at two-team head-to-head (LAC swept DEN).
    REQUIRE(result.size() == 3);
    REQUIRE(result[0]->abbreviation() == "KC");
    REQUIRE(result[1]->abbreviation() == "LAC");
    REQUIRE(result[2]->abbreviation() == "DEN");
}

TEST_CASE("Tiebreaker 3+ team skips common-games criterion when fewer than four games", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team lac("LAC", "LA Chargers", "AFC", "AFC West");

    // Keep division and conference records deterministic.
    for (int i = 0; i < 3; ++i) {
        kc.addDivisionWin();
        den.addDivisionWin();
        lac.addDivisionWin();
        kc.addDivisionLoss();
        den.addDivisionLoss();
        lac.addDivisionLoss();
    }

    // Conference: DEN best, then LAC, then KC.
    for (int i = 0; i < 8; ++i) den.addConferenceWin();
    for (int i = 0; i < 4; ++i) den.addConferenceLoss();
    for (int i = 0; i < 7; ++i) lac.addConferenceWin();
    for (int i = 0; i < 5; ++i) lac.addConferenceLoss();
    for (int i = 0; i < 6; ++i) kc.addConferenceWin();
    for (int i = 0; i < 6; ++i) kc.addConferenceLoss();

    allTeams["KC"] = kc;
    allTeams["DEN"] = den;
    allTeams["LAC"] = lac;
    allTeams["NYJ"] = Team("NYJ", "New York Jets", "AFC", "AFC East");
    allTeams["BUF"] = Team("BUF", "Buffalo Bills", "AFC", "AFC East");
    allTeams["MIA"] = Team("MIA", "Miami Dolphins", "AFC", "AFC East");

    // Three-way head-to-head tie (each 2-2).
    games.emplace_back(1, "2026-09-10", "DEN", "KC", 27, 17, "final");
    games.emplace_back(2, "2026-09-17", "KC", "DEN", 14, 24, "final");
    games.emplace_back(3, "2026-09-24", "KC", "LAC", 31, 20, "final");
    games.emplace_back(4, "2026-10-01", "LAC", "KC", 13, 28, "final");
    games.emplace_back(5, "2026-10-08", "LAC", "DEN", 30, 16, "final");
    games.emplace_back(6, "2026-10-15", "DEN", "LAC", 10, 21, "final");

    // Only three common opponents each: should NOT trigger the common-games criterion.
    // KC is best on these 3 games, but DEN should still rank first via conference record.
    games.emplace_back(7, "2026-10-22", "KC", "NYJ", 24, 10, "final");
    games.emplace_back(8, "2026-10-29", "KC", "BUF", 27, 20, "final");
    games.emplace_back(9, "2026-11-05", "KC", "MIA", 30, 14, "final");

    games.emplace_back(10, "2026-11-12", "DEN", "NYJ", 21, 13, "final");
    games.emplace_back(11, "2026-11-19", "DEN", "BUF", 28, 24, "final");
    games.emplace_back(12, "2026-11-26", "MIA", "DEN", 24, 14, "final");

    games.emplace_back(13, "2026-12-03", "LAC", "NYJ", 26, 16, "final");
    games.emplace_back(14, "2026-12-10", "LAC", "BUF", 20, 13, "final");
    games.emplace_back(15, "2026-12-17", "MIA", "LAC", 27, 17, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"], &allTeams["LAC"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");

    REQUIRE(result.size() == 3);
    REQUIRE(result[0]->abbreviation() == "DEN");
}

TEST_CASE("Tiebreaker conference 3+ tie ignores division-record criterion", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team bal("BAL", "Baltimore Ravens", "AFC", "AFC North");
    Team buf("BUF", "Buffalo Bills", "AFC", "AFC East");

    // Deliberately skew division records; these must be ignored for conference ties.
    for (int i = 0; i < 6; ++i) bal.addDivisionWin();
    for (int i = 0; i < 6; ++i) kc.addDivisionLoss();
    for (int i = 0; i < 3; ++i) buf.addDivisionWin();
    for (int i = 0; i < 3; ++i) buf.addDivisionLoss();

    allTeams["KC"] = kc;
    allTeams["BAL"] = bal;
    allTeams["BUF"] = buf;
    allTeams["NYJ"] = Team("NYJ", "New York Jets", "AFC", "AFC East");
    allTeams["MIA"] = Team("MIA", "Miami Dolphins", "AFC", "AFC East");
    allTeams["NE"] = Team("NE", "New England Patriots", "AFC", "AFC East");
    allTeams["TEN"] = Team("TEN", "Tennessee Titans", "AFC", "AFC South");

    // Three-way head-to-head tie (each 1-1).
    games.emplace_back(1, "2026-09-10", "KC", "BAL", 24, 17, "final");
    games.emplace_back(2, "2026-09-17", "BAL", "BUF", 30, 20, "final");
    games.emplace_back(3, "2026-09-24", "BUF", "KC", 27, 24, "final");

    // Four common opponents for all teams: KC = 4-0, BAL = 2-2, BUF = 1-3.
    games.emplace_back(4, "2026-10-01", "KC", "NYJ", 27, 10, "final");
    games.emplace_back(5, "2026-10-08", "KC", "MIA", 30, 13, "final");
    games.emplace_back(6, "2026-10-15", "KC", "NE", 21, 14, "final");
    games.emplace_back(7, "2026-10-22", "KC", "TEN", 24, 20, "final");

    games.emplace_back(8, "2026-10-29", "BAL", "NYJ", 20, 10, "final");
    games.emplace_back(9, "2026-11-05", "BAL", "MIA", 17, 24, "final");
    games.emplace_back(10, "2026-11-12", "BAL", "NE", 14, 21, "final");
    games.emplace_back(11, "2026-11-19", "BAL", "TEN", 23, 17, "final");

    games.emplace_back(12, "2026-11-26", "BUF", "NYJ", 17, 13, "final");
    games.emplace_back(13, "2026-12-03", "BUF", "MIA", 13, 27, "final");
    games.emplace_back(14, "2026-12-10", "BUF", "NE", 10, 20, "final");
    games.emplace_back(15, "2026-12-17", "BUF", "TEN", 14, 23, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["BAL"], &allTeams["BUF"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams);

    REQUIRE(result.size() == 3);
    REQUIRE(result[0]->abbreviation() == "KC");
}

TEST_CASE("Tiebreaker 4-team division elimination restarts criteria each round", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team lac("LAC", "LA Chargers", "AFC", "AFC West");
    Team lv("LV", "Las Vegas Raiders", "AFC", "AFC West");

    // Division records are intentionally set so that after KC is selected,
    // LAC wins the 3-team remainder, and DEN/LV remain tied for the 2-team restart.
    for (int i = 0; i < 5; ++i) {
        lac.addDivisionWin();
    }
    lac.addDivisionLoss();

    for (int i = 0; i < 4; ++i) {
        den.addDivisionWin();
        lv.addDivisionWin();
    }
    den.addDivisionLoss();
    den.addDivisionLoss();
    lv.addDivisionLoss();
    lv.addDivisionLoss();

    // KC division record should not matter for final 3-team and 2-team rounds.
    for (int i = 0; i < 3; ++i) {
        kc.addDivisionWin();
        kc.addDivisionLoss();
    }

    allTeams["KC"] = kc;
    allTeams["DEN"] = den;
    allTeams["LAC"] = lac;
    allTeams["LV"] = lv;

    // 4-team head-to-head: KC is unique best at 3-0.
    games.emplace_back(1, "2026-09-10", "KC", "DEN", 24, 10, "final");
    games.emplace_back(2, "2026-09-17", "KC", "LAC", 30, 17, "final");
    games.emplace_back(3, "2026-09-24", "KC", "LV", 27, 13, "final");

    // Remaining trio (DEN/LAC/LV) is a cycle (each 1-1 in that subgroup).
    games.emplace_back(4, "2026-10-01", "DEN", "LAC", 21, 14, "final");
    games.emplace_back(5, "2026-10-08", "LAC", "LV", 28, 17, "final");
    games.emplace_back(6, "2026-10-15", "LV", "DEN", 20, 13, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"], &allTeams["LAC"], &allTeams["LV"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");

    // Round 1 (4-team): KC first via 4-way H2H.
    // Round 2 (3-team restart): LAC first via division record tie-break among DEN/LAC/LV.
    // Round 3 (2-team restart): LV over DEN via direct head-to-head.
    REQUIRE(result.size() == 4);
    REQUIRE(result[0]->abbreviation() == "KC");
    REQUIRE(result[1]->abbreviation() == "LAC");
    REQUIRE(result[2]->abbreviation() == "LV");
    REQUIRE(result[3]->abbreviation() == "DEN");
}

TEST_CASE("Tiebreaker 4-team conference elimination restarts criteria each round", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team bal("BAL", "Baltimore Ravens", "AFC", "AFC North");
    Team buf("BUF", "Buffalo Bills", "AFC", "AFC East");
    Team hou("HOU", "Houston Texans", "AFC", "AFC South");

    // Deliberately skew division records; conference-mode tiebreaking should ignore them.
    for (int i = 0; i < 6; ++i) {
        bal.addDivisionWin();
    }
    for (int i = 0; i < 6; ++i) {
        kc.addDivisionLoss();
    }
    for (int i = 0; i < 4; ++i) {
        buf.addDivisionWin();
        hou.addDivisionWin();
    }
    for (int i = 0; i < 2; ++i) {
        buf.addDivisionLoss();
        hou.addDivisionLoss();
    }

    // Conference records for the 3-team remainder after KC wins the 4-team round.
    // BAL best; BUF and HOU tied so they must restart at two-team head-to-head.
    for (int i = 0; i < 9; ++i) bal.addConferenceWin();
    for (int i = 0; i < 3; ++i) bal.addConferenceLoss();
    for (int i = 0; i < 8; ++i) buf.addConferenceWin();
    for (int i = 0; i < 4; ++i) buf.addConferenceLoss();
    for (int i = 0; i < 8; ++i) hou.addConferenceWin();
    for (int i = 0; i < 4; ++i) hou.addConferenceLoss();

    allTeams["KC"] = kc;
    allTeams["BAL"] = bal;
    allTeams["BUF"] = buf;
    allTeams["HOU"] = hou;

    // 4-team head-to-head: KC is unique best at 3-0.
    games.emplace_back(1, "2026-09-10", "KC", "BAL", 27, 17, "final");
    games.emplace_back(2, "2026-09-17", "KC", "BUF", 24, 20, "final");
    games.emplace_back(3, "2026-09-24", "KC", "HOU", 30, 16, "final");

    // Remaining trio (BAL/BUF/HOU) is a cycle: each 1-1.
    games.emplace_back(4, "2026-10-01", "BAL", "BUF", 21, 14, "final");
    games.emplace_back(5, "2026-10-08", "BUF", "HOU", 28, 17, "final");
    games.emplace_back(6, "2026-10-15", "HOU", "BAL", 24, 20, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["BAL"], &allTeams["BUF"], &allTeams["HOU"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams);

    // Round 1 (4-team): KC first via 4-way H2H.
    // Round 2 (3-team restart): BAL first via conference record.
    // Round 3 (2-team restart): BUF over HOU via direct head-to-head.
    REQUIRE(result.size() == 4);
    REQUIRE(result[0]->abbreviation() == "KC");
    REQUIRE(result[1]->abbreviation() == "BAL");
    REQUIRE(result[2]->abbreviation() == "BUF");
    REQUIRE(result[3]->abbreviation() == "HOU");
}

TEST_CASE("Tiebreaker 4-team division mirror: 3-team remainder resolved by common games", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team lac("LAC", "LA Chargers", "AFC", "AFC West");
    Team lv("LV", "Las Vegas Raiders", "AFC", "AFC West");

    // Keep DEN/LAC/LV tied on division record so common-games breaks the 3-team remainder.
    for (int i = 0; i < 3; ++i) {
        den.addDivisionWin();
        lac.addDivisionWin();
        lv.addDivisionWin();
        den.addDivisionLoss();
        lac.addDivisionLoss();
        lv.addDivisionLoss();
    }

    allTeams["KC"] = kc;
    allTeams["DEN"] = den;
    allTeams["LAC"] = lac;
    allTeams["LV"] = lv;
    allTeams["NYJ"] = Team("NYJ", "New York Jets", "AFC", "AFC East");
    allTeams["BUF"] = Team("BUF", "Buffalo Bills", "AFC", "AFC East");
    allTeams["MIA"] = Team("MIA", "Miami Dolphins", "AFC", "AFC East");
    allTeams["NE"] = Team("NE", "New England Patriots", "AFC", "AFC East");

    // 4-team head-to-head: KC unique best at 3-0.
    games.emplace_back(1, "2026-09-10", "KC", "DEN", 27, 13, "final");
    games.emplace_back(2, "2026-09-17", "KC", "LAC", 30, 17, "final");
    games.emplace_back(3, "2026-09-24", "KC", "LV", 24, 14, "final");

    // Remaining trio (DEN/LAC/LV) forms a cycle (1-1 each).
    games.emplace_back(4, "2026-10-01", "DEN", "LAC", 23, 20, "final");
    games.emplace_back(5, "2026-10-08", "LAC", "LV", 28, 16, "final");
    games.emplace_back(6, "2026-10-15", "LV", "DEN", 21, 17, "final");

    // Common opponents for DEN/LAC/LV (plus KC, which all three also played):
    // DEN best, then LAC, then LV.
    games.emplace_back(7, "2026-10-22", "DEN", "NYJ", 24, 10, "final");
    games.emplace_back(8, "2026-10-29", "DEN", "BUF", 27, 21, "final");
    games.emplace_back(9, "2026-11-05", "DEN", "MIA", 20, 17, "final");
    games.emplace_back(10, "2026-11-12", "NE", "DEN", 24, 14, "final");

    games.emplace_back(11, "2026-11-19", "LAC", "NYJ", 26, 13, "final");
    games.emplace_back(12, "2026-11-26", "LAC", "BUF", 20, 17, "final");
    games.emplace_back(13, "2026-12-03", "MIA", "LAC", 24, 20, "final");
    games.emplace_back(14, "2026-12-10", "NE", "LAC", 23, 20, "final");

    games.emplace_back(15, "2026-12-17", "LV", "NYJ", 24, 20, "final");
    games.emplace_back(16, "2026-12-24", "LV", "BUF", 17, 14, "final");
    games.emplace_back(17, "2026-12-31", "MIA", "LV", 27, 17, "final");
    games.emplace_back(18, "2027-01-07", "NE", "LV", 30, 13, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["DEN"], &allTeams["LAC"], &allTeams["LV"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams, "AFC West");

    // 4-team round: KC first.
    // 3-team restart: DEN first via common games.
    // 2-team restart: LAC over LV via direct head-to-head.
    REQUIRE(result.size() == 4);
    REQUIRE(result[0]->abbreviation() == "KC");
    REQUIRE(result[1]->abbreviation() == "DEN");
    REQUIRE(result[2]->abbreviation() == "LAC");
    REQUIRE(result[3]->abbreviation() == "LV");
}

TEST_CASE("Tiebreaker 4-team conference mirror: 3-team remainder resolved by common games", "[Tiebreaker][regression]") {
    std::vector<Game> games;
    std::map<std::string, Team> allTeams;

    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team bal("BAL", "Baltimore Ravens", "AFC", "AFC North");
    Team buf("BUF", "Buffalo Bills", "AFC", "AFC East");
    Team hou("HOU", "Houston Texans", "AFC", "AFC South");

    // Keep conference records tied for BAL/BUF/HOU so common-games decides the 3-team remainder.
    for (int i = 0; i < 8; ++i) {
        bal.addConferenceWin();
        buf.addConferenceWin();
        hou.addConferenceWin();
    }
    for (int i = 0; i < 4; ++i) {
        bal.addConferenceLoss();
        buf.addConferenceLoss();
        hou.addConferenceLoss();
    }

    // Deliberately skew division records; conference-mode path must ignore them.
    for (int i = 0; i < 6; ++i) bal.addDivisionWin();
    for (int i = 0; i < 6; ++i) kc.addDivisionLoss();

    allTeams["KC"] = kc;
    allTeams["BAL"] = bal;
    allTeams["BUF"] = buf;
    allTeams["HOU"] = hou;
    allTeams["NYJ"] = Team("NYJ", "New York Jets", "AFC", "AFC East");
    allTeams["MIA"] = Team("MIA", "Miami Dolphins", "AFC", "AFC East");
    allTeams["NE"] = Team("NE", "New England Patriots", "AFC", "AFC East");
    allTeams["TEN"] = Team("TEN", "Tennessee Titans", "AFC", "AFC South");

    // 4-team head-to-head: KC unique best at 3-0.
    games.emplace_back(1, "2026-09-10", "KC", "BAL", 27, 13, "final");
    games.emplace_back(2, "2026-09-17", "KC", "BUF", 24, 17, "final");
    games.emplace_back(3, "2026-09-24", "KC", "HOU", 30, 20, "final");

    // Remaining trio (BAL/BUF/HOU) forms a cycle (1-1 each).
    games.emplace_back(4, "2026-10-01", "BAL", "BUF", 21, 17, "final");
    games.emplace_back(5, "2026-10-08", "BUF", "HOU", 28, 14, "final");
    games.emplace_back(6, "2026-10-15", "HOU", "BAL", 24, 20, "final");

    // Common opponents for BAL/BUF/HOU (plus KC, shared by all): BAL best, then BUF, then HOU.
    games.emplace_back(7, "2026-10-22", "BAL", "NYJ", 24, 10, "final");
    games.emplace_back(8, "2026-10-29", "BAL", "MIA", 27, 20, "final");
    games.emplace_back(9, "2026-11-05", "BAL", "NE", 20, 17, "final");
    games.emplace_back(10, "2026-11-12", "TEN", "BAL", 24, 14, "final");

    games.emplace_back(11, "2026-11-19", "BUF", "NYJ", 26, 13, "final");
    games.emplace_back(12, "2026-11-26", "BUF", "MIA", 20, 17, "final");
    games.emplace_back(13, "2026-12-03", "NE", "BUF", 24, 20, "final");
    games.emplace_back(14, "2026-12-10", "TEN", "BUF", 23, 20, "final");

    games.emplace_back(15, "2026-12-17", "HOU", "NYJ", 24, 20, "final");
    games.emplace_back(16, "2026-12-24", "HOU", "MIA", 17, 14, "final");
    games.emplace_back(17, "2026-12-31", "NE", "HOU", 27, 17, "final");
    games.emplace_back(18, "2027-01-07", "TEN", "HOU", 30, 13, "final");

    std::vector<Team*> tied = {&allTeams["KC"], &allTeams["BAL"], &allTeams["BUF"], &allTeams["HOU"]};
    auto result = Tiebreaker::breakTie(tied, games, allTeams);

    // 4-team round: KC first.
    // 3-team restart: BAL first via common games.
    // 2-team restart: BUF over HOU via direct head-to-head.
    REQUIRE(result.size() == 4);
    REQUIRE(result[0]->abbreviation() == "KC");
    REQUIRE(result[1]->abbreviation() == "BAL");
    REQUIRE(result[2]->abbreviation() == "BUF");
    REQUIRE(result[3]->abbreviation() == "HOU");
}
