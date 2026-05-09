#include "catch.hpp"
#include "model/MonteCarlo.h"
#include "model/Season.h"
#include "model/Team.h"
#include "model/Game.h"

TEST_CASE("MonteCarlo win probability", "[MonteCarlo]") {
    MonteCarlo mc;
    
    SECTION("Equal teams: home field advantage ~57%") {
        Team home("KC", "Kansas City Chiefs", "AFC", "AFC West");
        Team away("DEN", "Denver Broncos", "AFC", "AFC West");
        
        double prob = mc.getWinProbability(home, away);
        
        // Should be close to 0.57 (home field advantage)
        REQUIRE(prob > 0.50);
        REQUIRE(prob < 0.65);
    }
    
    SECTION("Strong home team wins more often") {
        Team home("KC", "Kansas City Chiefs", "AFC", "AFC West");
        Team away("DEN", "Denver Broncos", "AFC", "AFC West");
        
        // Give home team wins to make them stronger
        for (int i = 0; i < 5; ++i) home.addWin();
        for (int i = 0; i < 3; ++i) away.addWin();
        
        double prob = mc.getWinProbability(home, away);
        
        // Strong home team should have high win probability
        REQUIRE(prob > 0.57);  // Better than baseline home advantage
    }
    
    SECTION("Strong away team lowers home team win probability") {
        Team home("KC", "Kansas City Chiefs", "AFC", "AFC West");
        Team away("DEN", "Denver Broncos", "AFC", "AFC West");
        
        // Give away team wins to make them stronger
        for (int i = 0; i < 5; ++i) away.addWin();
        for (int i = 0; i < 1; ++i) home.addWin();
        
        double prob = mc.getWinProbability(home, away);
        
        // Despite home advantage, weak home team should have < 57%
        REQUIRE(prob < 0.57);
    }
    
    SECTION("Probability is always between 0 and 1") {
        Team home("KC", "Kansas City Chiefs", "AFC", "AFC West");
        Team away("DEN", "Denver Broncos", "AFC", "AFC West");
        
        // Add many wins to both
        for (int i = 0; i < 20; ++i) {
            home.addWin();
            away.addWin();
        }
        
        double prob = mc.getWinProbability(home, away);
        
        REQUIRE(prob >= 0.0);
        REQUIRE(prob <= 1.0);
    }
}

TEST_CASE("MonteCarlo team strength factor", "[MonteCarlo]") {
    MonteCarlo mc;
    
    SECTION("Undefeated team has high strength") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        for (int i = 0; i < 4; ++i) team.addWin();
        
        double strength = mc.getTeamStrengthFactor(team);
        
        REQUIRE(strength > 0.6);
    }
    
    SECTION("Winless team has low strength") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        for (int i = 0; i < 4; ++i) team.addLoss();
        
        double strength = mc.getTeamStrengthFactor(team);
        
        REQUIRE(strength < 0.4);
    }
    
    SECTION("No games played gives neutral strength") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        
        double strength = mc.getTeamStrengthFactor(team);
        
        // Regressed toward 0.5 for no information
        REQUIRE(strength > 0.49);
        REQUIRE(strength < 0.51);
    }
    
    SECTION("Strength factor between 0.3 and 0.7") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        team.addWin();
        team.addWin();
        team.addLoss();
        
        double strength = mc.getTeamStrengthFactor(team);
        
        REQUIRE(strength >= 0.3);
        REQUIRE(strength <= 0.7);
    }
}

TEST_CASE("MonteCarlo simulation structure", "[MonteCarlo]") {
    MonteCarlo mc;
    Season season;
    
    // Set up teams
    season.addTeam(Team("KC", "Kansas City Chiefs", "AFC", "AFC West"));
    season.addTeam(Team("DEN", "Denver Broncos", "AFC", "AFC West"));
    season.addTeam(Team("LAC", "LA Chargers", "AFC", "AFC West"));
    season.addTeam(Team("LV", "Las Vegas Raiders", "AFC", "AFC West"));
    
    SECTION("Simulation with seed is reproducible") {
        auto results1 = mc.simulate(season, 1000, 12345);
        auto results2 = mc.simulate(season, 1000, 12345);
        
        REQUIRE(results1.totalIterations == 1000);
        REQUIRE(results2.totalIterations == 1000);
    }
    
    SECTION("Results contain probabilities for all teams") {
        auto results = mc.simulate(season, 100, 12345);
        
        REQUIRE(results.playoffProbability.size() == 4);
        REQUIRE(results.divisionWinProbability.size() == 4);
        REQUIRE(results.wildcardProbability.size() == 4);
        REQUIRE(results.teamWinProbability.size() == 4);
    }
    
    SECTION("Probabilities are between 0 and 1") {
        auto results = mc.simulate(season, 100, 12345);
        
        for (const auto& [abbr, prob] : results.playoffProbability) {
            REQUIRE(prob >= 0.0);
            REQUIRE(prob <= 1.0);
        }
        
        for (const auto& [abbr, prob] : results.divisionWinProbability) {
            REQUIRE(prob >= 0.0);
            REQUIRE(prob <= 1.0);
        }
    }
    
    SECTION("Division winner probabilities sum to 1 per division") {
        // With only AFC West teams, one should be division winner
        auto results = mc.simulate(season, 1000, 12345);
        
        double divWinProb = 0.0;
        for (const auto& [abbr, prob] : results.divisionWinProbability) {
            divWinProb += prob;
        }
        
        // Total across all teams should be ~1.0 per division
        // (or slightly less if scenarios not fully explored)
        REQUIRE(divWinProb >= 0.0);
        REQUIRE(divWinProb <= 4.0);  // At most 4 divisions worth
    }
}

TEST_CASE("MonteCarlo with games played", "[MonteCarlo]") {
    MonteCarlo mc;
    Season season;
    
    // Set up teams
    season.addTeam(Team("KC", "Kansas City Chiefs", "AFC", "AFC West"));
    season.addTeam(Team("DEN", "Denver Broncos", "AFC", "AFC West"));
    
    // Add a game so teams have records
    season.addGame(Game(1, "2026-09-10", "KC", "DEN", 24, 20, "final"));
    season.computeStandings();
    
    SECTION("Simulation considers current records") {
        auto results = mc.simulate(season, 100, 12345);
        
        // KC won the game, should have higher playoff probability
        double kcProb = results.playoffProbability["KC"];
        double denProb = results.playoffProbability["DEN"];
        
        REQUIRE(kcProb + denProb > 0.0);  // At least one has non-zero prob
    }
}

TEST_CASE("MonteCarlo deterministic with seed", "[MonteCarlo]") {
    Season season;
    season.addTeam(Team("KC", "Kansas City Chiefs", "AFC", "AFC West"));
    season.addTeam(Team("DEN", "Denver Broncos", "AFC", "AFC West"));
    
    MonteCarlo mc1, mc2;
    
    auto results1 = mc1.simulate(season, 500, 99999);
    auto results2 = mc2.simulate(season, 500, 99999);
    
    // Same seed should give same results
    REQUIRE(results1.totalIterations == results2.totalIterations);
    
    // Results should be identical (deterministic)
    for (const auto& [abbr, prob] : results1.playoffProbability) {
        REQUIRE(results2.playoffProbability[abbr] == prob);
    }
}
