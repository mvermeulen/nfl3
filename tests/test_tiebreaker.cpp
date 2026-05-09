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
    
    // Set up division records
    team1.addDivisionWin();
    team1.addDivisionWin();
    team2.addDivisionWin();
    
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
    
    // Set up conference records
    team1.addConferenceWin();
    team1.addConferenceWin();
    team1.addConferenceWin();
    team2.addConferenceWin();
    team2.addConferenceWin();
    
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
