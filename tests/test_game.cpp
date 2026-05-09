#include "catch.hpp"
#include "model/Game.h"

TEST_CASE("Game initialization", "[Game]") {
    SECTION("Create a scheduled game") {
        Game game(1, "2026-09-10", "KC", "BAL", -1, -1, "scheduled");
        REQUIRE(game.week() == 1);
        REQUIRE(game.date() == "2026-09-10");
        REQUIRE(game.homeTeam() == "KC");
        REQUIRE(game.awayTeam() == "BAL");
        REQUIRE(game.homeScore() == -1);
        REQUIRE(game.awayScore() == -1);
        REQUIRE(game.status() == "scheduled");
    }

    SECTION("Create a final game with scores") {
        Game game(1, "2026-09-10", "KC", "BAL", 24, 20, "final");
        REQUIRE(game.homeScore() == 24);
        REQUIRE(game.awayScore() == 20);
        REQUIRE(game.status() == "final");
    }
}

TEST_CASE("Game status queries", "[Game]") {
    SECTION("Scheduled game") {
        Game game(1, "2026-09-10", "KC", "BAL", -1, -1, "scheduled");
        REQUIRE(game.isScheduled() == true);
        REQUIRE(game.isInProgress() == false);
        REQUIRE(game.isFinal() == false);
        REQUIRE(game.isPlayed() == false);
    }

    SECTION("In-progress game") {
        Game game(1, "2026-09-10", "KC", "BAL", 14, 10, "in_progress");
        REQUIRE(game.isScheduled() == false);
        REQUIRE(game.isInProgress() == true);
        REQUIRE(game.isFinal() == false);
        REQUIRE(game.isPlayed() == true);
    }

    SECTION("Final game") {
        Game game(1, "2026-09-10", "KC", "BAL", 24, 20, "final");
        REQUIRE(game.isScheduled() == false);
        REQUIRE(game.isInProgress() == false);
        REQUIRE(game.isFinal() == true);
        REQUIRE(game.isPlayed() == true);
    }
}

TEST_CASE("Game winner determination", "[Game]") {
    SECTION("Home team wins") {
        Game game(1, "2026-09-10", "KC", "BAL", 24, 20, "final");
        REQUIRE(game.homeTeamWon() == true);
        REQUIRE(game.awayTeamWon() == false);
        REQUIRE(game.isTie() == false);
    }

    SECTION("Away team wins") {
        Game game(1, "2026-09-10", "KC", "BAL", 20, 24, "final");
        REQUIRE(game.homeTeamWon() == false);
        REQUIRE(game.awayTeamWon() == true);
        REQUIRE(game.isTie() == false);
    }

    SECTION("Tie game") {
        Game game(1, "2026-09-10", "KC", "BAL", 20, 20, "final");
        REQUIRE(game.homeTeamWon() == false);
        REQUIRE(game.awayTeamWon() == false);
        REQUIRE(game.isTie() == true);
    }
}

TEST_CASE("Game with large scores", "[Game]") {
    SECTION("High scoring game") {
        Game game(1, "2026-09-10", "KC", "BAL", 42, 38, "final");
        REQUIRE(game.homeScore() == 42);
        REQUIRE(game.awayScore() == 38);
        REQUIRE(game.homeTeamWon() == true);
    }

    SECTION("Low scoring game") {
        Game game(1, "2026-09-10", "KC", "BAL", 3, 0, "final");
        REQUIRE(game.homeScore() == 3);
        REQUIRE(game.awayScore() == 0);
        REQUIRE(game.homeTeamWon() == true);
    }
}
