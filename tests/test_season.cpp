#include "catch.hpp"
#include "model/Season.h"
#include "model/Team.h"
#include "model/Game.h"

TEST_CASE("Season team management", "[Season]") {
    Season season;
    Team team1("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team team2("DEN", "Denver Broncos", "AFC", "AFC West");
    
    SECTION("Add and retrieve teams") {
        season.addTeam(team1);
        season.addTeam(team2);
        
        Team* kc = season.getTeam("KC");
        REQUIRE(kc != nullptr);
        REQUIRE(kc->fullName() == "Kansas City Chiefs");
        
        Team* den = season.getTeam("DEN");
        REQUIRE(den != nullptr);
        REQUIRE(den->fullName() == "Denver Broncos");
    }
    
    SECTION("Get non-existent team") {
        season.addTeam(team1);
        Team* notThere = season.getTeam("LAR");
        REQUIRE(notThere == nullptr);
    }
}

TEST_CASE("Season game management", "[Season]") {
    Season season;
    Game game(1, "2026-09-10", "KC", "DEN", 24, 20, "final");
    
    SECTION("Add and retrieve games") {
        season.addGame(game);
        const auto& games = season.allGames();
        REQUIRE(games.size() == 1);
        REQUIRE(games[0].homeTeam() == "KC");
    }
}

TEST_CASE("Season standings computation", "[Season]") {
    Season season;
    
    // Add teams
    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team lv("LV", "Las Vegas Raiders", "AFC", "AFC West");
    Team lac("LAC", "LA Chargers", "AFC", "AFC West");
    
    season.addTeam(kc);
    season.addTeam(den);
    season.addTeam(lv);
    season.addTeam(lac);
    
    SECTION("Initial standings all 0-0") {
        season.computeStandings();
        auto standings = season.teamsByDivision("AFC West");
        
        for (const auto* team : standings) {
            REQUIRE(team->wins() == 0);
            REQUIRE(team->losses() == 0);
            REQUIRE(team->winPercentage() == 0.0);
        }
    }
    
    SECTION("Single game updates both teams") {
        season.addGame(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        season.computeStandings();
        
        Team* wcTeam = season.getTeam("KC");
        Team* losingTeam = season.getTeam("DEN");
        
        REQUIRE(wcTeam->wins() == 1);
        REQUIRE(wcTeam->losses() == 0);
        REQUIRE(losingTeam->wins() == 0);
        REQUIRE(losingTeam->losses() == 1);
    }
    
    SECTION("Multiple games update records correctly") {
        season.addGame(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        season.addGame(Game(1, "2026-09-10", "LAC", "LV", 17, 20, "final"));
        season.addGame(Game(2, "2026-09-17", "DEN", "LAC", 21, 17, "final"));
        
        season.computeStandings();
        
        Team* kc_final = season.getTeam("KC");
        Team* den_final = season.getTeam("DEN");
        Team* lv_final = season.getTeam("LV");
        Team* lac_final = season.getTeam("LAC");
        
        REQUIRE(kc_final->wins() == 1);
        REQUIRE(kc_final->losses() == 0);
        
        REQUIRE(den_final->wins() == 1);
        REQUIRE(den_final->losses() == 1);
        
        REQUIRE(lv_final->wins() == 1);
        REQUIRE(lv_final->losses() == 0);
        
        REQUIRE(lac_final->wins() == 0);
        REQUIRE(lac_final->losses() == 2);
    }
    
    SECTION("Tie game updates both teams with tie") {
        season.addGame(Game(1, "2026-09-10", "KC", "DEN", 20, 20, "final"));
        season.computeStandings();
        
        Team* kc_final = season.getTeam("KC");
        Team* den_final = season.getTeam("DEN");
        
        REQUIRE(kc_final->wins() == 0);
        REQUIRE(kc_final->losses() == 0);
        REQUIRE(kc_final->ties() == 1);
        
        REQUIRE(den_final->wins() == 0);
        REQUIRE(den_final->losses() == 0);
        REQUIRE(den_final->ties() == 1);
    }
    
    SECTION("Unplayed games are ignored") {
        season.addGame(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        season.addGame(Game(2, "2026-09-17", "LAC", "LV", -1, -1, "scheduled"));
        
        season.computeStandings();
        
        Team* lac = season.getTeam("LAC");
        Team* lv = season.getTeam("LV");
        
        REQUIRE(lac->wins() == 0);
        REQUIRE(lac->losses() == 0);
        REQUIRE(lv->wins() == 0);
        REQUIRE(lv->losses() == 0);
    }
}

TEST_CASE("Season division standings sorting", "[Season]") {
    Season season;
    
    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team lv("LV", "Las Vegas Raiders", "AFC", "AFC West");
    
    season.addTeam(kc);
    season.addTeam(den);
    season.addTeam(lv);
    
    SECTION("Sort by win percentage") {
        season.addGame(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        season.addGame(Game(1, "2026-09-10", "LV", "KC", 24, 20, "final"));
        
        season.computeStandings();
        auto standings = season.teamsByDivision("AFC West");
        
        REQUIRE(standings[0]->abbreviation() == "LV");  // 1-0, wins tiebreaker
        REQUIRE(standings[1]->abbreviation() == "KC");  // 1-1
        REQUIRE(standings[2]->abbreviation() == "DEN"); // 0-1
    }
}

TEST_CASE("Season division/conference record tracking", "[Season]") {
    Season season;
    
    Team kc("KC", "Kansas City Chiefs", "AFC", "AFC West");
    Team den("DEN", "Denver Broncos", "AFC", "AFC West");
    Team buf("BUF", "Buffalo Bills", "AFC", "AFC East");
    
    season.addTeam(kc);
    season.addTeam(den);
    season.addTeam(buf);
    
    SECTION("Division games tracked separately") {
        season.addGame(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
        season.addGame(Game(2, "2026-09-17", "KC", "BUF", 30, 27, "final"));
        
        season.computeStandings();
        
        Team* kc_final = season.getTeam("KC");
        REQUIRE(kc_final->wins() == 2);
        REQUIRE(kc_final->divisionWins() == 1);
        REQUIRE(kc_final->conferenceWins() == 2);
    }
    
    SECTION("Conference games tracked separately") {
        season.addGame(Game(1, "2026-09-10", "KC", "BUF", 30, 27, "final"));
        season.addGame(Game(2, "2026-09-17", "DEN", "KC", 20, 24, "final"));
        
        season.computeStandings();
        
        Team* kc_final = season.getTeam("KC");
        REQUIRE(kc_final->wins() == 2);
        REQUIRE(kc_final->divisionWins() == 1);  // One division game (vs DEN)
        REQUIRE(kc_final->conferenceWins() == 2); // Both games are conference games
    }
}

TEST_CASE("Season get divisions", "[Season]") {
    Season season;
    
    season.addTeam(Team("KC", "Kansas City Chiefs", "AFC", "AFC West"));
    season.addTeam(Team("DEN", "Denver Broncos", "AFC", "AFC West"));
    season.addTeam(Team("BUF", "Buffalo Bills", "AFC", "AFC East"));
    
    auto divisions = season.getDivisions();
    
    REQUIRE(divisions.size() == 2);
    REQUIRE(std::find(divisions.begin(), divisions.end(), "AFC West") != divisions.end());
    REQUIRE(std::find(divisions.begin(), divisions.end(), "AFC East") != divisions.end());
}
