#include "catch.hpp"
#include "model/Team.h"

TEST_CASE("Team initialization", "[Team]") {
    SECTION("Constructor with all parameters") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        REQUIRE(team.getAbbreviation() == "KC");
        REQUIRE(team.fullName() == "Kansas City Chiefs");
        REQUIRE(team.getConference() == "AFC");
        REQUIRE(team.getDivision() == "AFC West");
    }

    SECTION("Default constructor") {
        Team team;
        REQUIRE(team.abbreviation() == "");
        REQUIRE(team.wins() == 0);
    }

    SECTION("Initial record is 0-0-0") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        REQUIRE(team.wins() == 0);
        REQUIRE(team.losses() == 0);
        REQUIRE(team.ties() == 0);
        REQUIRE(team.gamesPlayed() == 0);
    }
}

TEST_CASE("Team record tracking", "[Team]") {
    Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");

    SECTION("Add wins") {
        team.addWin();
        team.addWin();
        REQUIRE(team.wins() == 2);
        REQUIRE(team.gamesPlayed() == 2);
    }

    SECTION("Add losses") {
        team.addLoss();
        team.addLoss();
        team.addLoss();
        REQUIRE(team.losses() == 3);
        REQUIRE(team.gamesPlayed() == 3);
    }

    SECTION("Add ties") {
        team.addTie();
        REQUIRE(team.ties() == 1);
        REQUIRE(team.gamesPlayed() == 1);
    }

    SECTION("Mixed record") {
        team.addWin();
        team.addWin();
        team.addLoss();
        team.addTie();
        REQUIRE(team.wins() == 2);
        REQUIRE(team.losses() == 1);
        REQUIRE(team.ties() == 1);
        REQUIRE(team.gamesPlayed() == 4);
    }
}

TEST_CASE("Team win percentage calculation", "[Team]") {
    SECTION("0-0-0 is 0.0") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        REQUIRE(team.winPercentage() == 0.0);
    }

    SECTION("1-0-0 is 1.0") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        team.addWin();
        REQUIRE(team.winPercentage() == 1.0);
    }

    SECTION("0-1-0 is 0.0") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        team.addLoss();
        REQUIRE(team.winPercentage() == 0.0);
    }

    SECTION("1-1-0 is 0.5") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        team.addWin();
        team.addLoss();
        REQUIRE(team.winPercentage() == 0.5);
    }

    SECTION("Tie counts as 0.5 win") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        team.addTie();
        REQUIRE(team.winPercentage() == 0.5);
    }

    SECTION("2-1-1 is 0.6 (2.5 / 4)") {
        Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");
        team.addWin();
        team.addWin();
        team.addLoss();
        team.addTie();
        REQUIRE(team.winPercentage() == Approx(0.625)); // (2 + 0.5) / 4
    }
}

TEST_CASE("Team division and conference records", "[Team]") {
    Team team("KC", "Kansas City Chiefs", "AFC", "AFC West");

    SECTION("Initial division record is 0-0-0") {
        REQUIRE(team.divisionWins() == 0);
        REQUIRE(team.divisionLosses() == 0);
        REQUIRE(team.divisionTies() == 0);
    }

    SECTION("Initial conference record is 0-0-0") {
        REQUIRE(team.conferenceWins() == 0);
        REQUIRE(team.conferenceLosses() == 0);
        REQUIRE(team.conferenceTies() == 0);
    }

    SECTION("Add division wins") {
        team.addDivisionWin();
        team.addDivisionWin();
        REQUIRE(team.divisionWins() == 2);
    }

    SECTION("Add conference wins") {
        team.addConferenceWin();
        team.addConferenceWin();
        team.addConferenceWin();
        REQUIRE(team.conferenceWins() == 3);
    }

    SECTION("Reset all records") {
        team.addWin();
        team.addWin();
        team.addLoss();
        team.addDivisionWin();
        team.addConferenceWin();
        
        team.resetRecord();
        
        REQUIRE(team.wins() == 0);
        REQUIRE(team.losses() == 0);
        REQUIRE(team.ties() == 0);
        REQUIRE(team.divisionWins() == 0);
        REQUIRE(team.conferenceWins() == 0);
    }
}
