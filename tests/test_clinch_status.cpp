#include "catch.hpp"
#include "model/ClinchStatus.h"
#include "model/Game.h"
#include "model/Season.h"
#include "model/Team.h"

#include <string>
#include <vector>

namespace {

// Target record for one team in a synthetic conference.
struct TeamSpec {
    std::string abbr;
    std::string division;
    int wins = 0;
    int losses = 0;
    int ties = 0;
    int remaining = 0;
};

const std::vector<std::string>& afcDivisions() {
    static const std::vector<std::string> divisions = {
        "AFC East", "AFC North", "AFC South", "AFC West"
    };
    return divisions;
}

/**
 * Build a season holding a full 16-team AFC with the requested records, plus a
 * 16-team NFC of filler opponents. Every AFC result is manufactured against a
 * filler team, so AFC records are exactly as specified and independent of one
 * another; the NFC records that fall out are meaningless and never asserted on.
 */
Season buildAfcSeason(const std::vector<TeamSpec>& specs) {
    Season season;

    for (const auto& spec : specs) {
        season.addTeam(Team(spec.abbr, spec.abbr + " Team", "AFC", spec.division));
    }

    std::vector<std::string> fillers;
    for (int i = 1; i <= 16; ++i) {
        const std::string abbr = "F" + std::to_string(i);
        const std::string division = "NFC " + std::to_string((i - 1) / 4 + 1);
        fillers.push_back(abbr);
        season.addTeam(Team(abbr, abbr + " Filler", "NFC", division));
    }

    size_t fillerIndex = 0;
    int week = 1;
    auto addGame = [&](const std::string& afcTeam, int afcScore, int fillerScore,
                       const std::string& status) {
        const std::string& filler = fillers[fillerIndex++ % fillers.size()];
        season.addGame(Game(week, "2026-09-10", afcTeam, filler, afcScore, fillerScore, status));
        week = week % 18 + 1;
    };

    for (const auto& spec : specs) {
        for (int i = 0; i < spec.wins; ++i) {
            addGame(spec.abbr, 24, 10, "final");
        }
        for (int i = 0; i < spec.losses; ++i) {
            addGame(spec.abbr, 10, 24, "final");
        }
        for (int i = 0; i < spec.ties; ++i) {
            addGame(spec.abbr, 20, 20, "final");
        }
        for (int i = 0; i < spec.remaining; ++i) {
            addGame(spec.abbr, 0, 0, "scheduled");
        }
    }

    season.computeStandings();
    return season;
}

// Fill every AFC division with four identically-recorded teams, then let the
// caller override the interesting ones.
std::vector<TeamSpec> uniformAfc(int wins, int losses, int remaining) {
    std::vector<TeamSpec> specs;
    for (const auto& division : afcDivisions()) {
        const std::string prefix = division.substr(4, 1);  // E / N / S / W
        for (int i = 1; i <= 4; ++i) {
            specs.push_back(TeamSpec{prefix + std::to_string(i), division, wins, losses, 0, remaining});
        }
    }
    return specs;
}

void setSpec(std::vector<TeamSpec>& specs, const std::string& abbr,
             int wins, int losses, int remaining, int ties = 0) {
    for (auto& spec : specs) {
        if (spec.abbr == abbr) {
            spec.wins = wins;
            spec.losses = losses;
            spec.ties = ties;
            spec.remaining = remaining;
            return;
        }
    }
    FAIL("Unknown team spec: " + abbr);
}

}  // namespace

TEST_CASE("Nothing is clinched or eliminated before any games are played", "[clinch]") {
    const Season season = buildAfcSeason(uniformAfc(0, 0, 17));
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE(statuses.size() == 32);
    for (const auto& [abbr, status] : statuses) {
        REQUIRE_FALSE(status.divisionClinched);
        REQUIRE_FALSE(status.divisionEliminated);
        REQUIRE_FALSE(status.playoffClinched);
        REQUIRE_FALSE(status.wildcardEliminated);
    }
}

TEST_CASE("Division clinch and elimination follow win-percentage bounds", "[clinch]") {
    auto specs = uniformAfc(7, 7, 3);
    // E1's floor (14/17) clears every rival's ceiling (3/17).
    setSpec(specs, "E1", 14, 0, 3);
    setSpec(specs, "E2", 0, 14, 3);
    setSpec(specs, "E3", 0, 14, 3);
    setSpec(specs, "E4", 0, 14, 3);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE(statuses.at("E1").divisionClinched);
    REQUIRE_FALSE(statuses.at("E1").divisionEliminated);

    for (const std::string& abbr : {"E2", "E3", "E4"}) {
        REQUIRE(statuses.at(abbr).divisionEliminated);
        REQUIRE_FALSE(statuses.at(abbr).divisionClinched);
    }

    // The untouched divisions are still wide open.
    REQUIRE_FALSE(statuses.at("N1").divisionClinched);
    REQUIRE_FALSE(statuses.at("N1").divisionEliminated);
}

TEST_CASE("A rival that can still finish tied blocks a division clinch", "[clinch]") {
    auto specs = uniformAfc(7, 7, 3);
    // N1 floor = 10/17; N2 ceiling = 10/17. A tie is still reachable, which the
    // tiebreakers could resolve either way, so neither flag may fire.
    setSpec(specs, "N1", 10, 4, 3);
    setSpec(specs, "N2", 7, 7, 3);
    setSpec(specs, "N3", 0, 14, 3);
    setSpec(specs, "N4", 0, 14, 3);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE_FALSE(statuses.at("N1").divisionClinched);
    REQUIRE_FALSE(statuses.at("N2").divisionEliminated);

    // One fewer win for N2 drops its ceiling below N1's floor.
    setSpec(specs, "N2", 6, 8, 3);
    const Season tightened = buildAfcSeason(specs);
    const auto tightenedStatuses = ClinchStatus::compute(tightened);

    REQUIRE(tightenedStatuses.at("N1").divisionClinched);
    REQUIRE(tightenedStatuses.at("N2").divisionEliminated);
}

TEST_CASE("Ties count as half a win for clinch bounds", "[clinch]") {
    auto specs = uniformAfc(0, 14, 3);
    // S1 at 13-1-0 has a floor of 13/17, exactly S2's 10-4-0 ceiling, so S2
    // survives on the chance of a tie-broken finish.
    setSpec(specs, "S1", 13, 1, 3);
    setSpec(specs, "S2", 10, 4, 3);

    const Season wholeWins = buildAfcSeason(specs);
    const auto wholeWinStatuses = ClinchStatus::compute(wholeWins);
    REQUIRE_FALSE(wholeWinStatuses.at("S2").divisionEliminated);
    REQUIRE_FALSE(wholeWinStatuses.at("S1").divisionClinched);

    // Turning S1's loss into a tie adds half a win point, lifting its floor to
    // 13.5/17 and putting S2 out of reach.
    setSpec(specs, "S1", 13, 0, 3, 1);

    const Season withTie = buildAfcSeason(specs);
    const Team* s1 = withTie.getTeam("S1");
    REQUIRE(s1 != nullptr);
    REQUIRE(s1->ties() == 1);
    REQUIRE(s1->gamesPlayed() == 14);

    const auto tieStatuses = ClinchStatus::compute(withTie);
    REQUIRE(tieStatuses.at("S2").divisionEliminated);
    REQUIRE(tieStatuses.at("S1").divisionClinched);
}

TEST_CASE("Wild card elimination needs blockers spread across divisions", "[clinch]") {
    auto specs = uniformAfc(2, 12, 3);
    // Three guaranteed-ahead teams all from one division leave only two wild
    // card blockers, because one of them must take the division title.
    setSpec(specs, "E1", 13, 1, 3);
    setSpec(specs, "E2", 12, 2, 3);
    setSpec(specs, "E3", 11, 3, 3);
    setSpec(specs, "W4", 1, 13, 3);

    const Season oneDivision = buildAfcSeason(specs);
    const auto oneDivisionStatuses = ClinchStatus::compute(oneDivision);
    REQUIRE_FALSE(oneDivisionStatuses.at("W4").wildcardEliminated);

    // Adding two blockers from a second division pushes the guaranteed
    // non-division-winners ahead of W4 to three, which is every wild card spot.
    setSpec(specs, "N1", 13, 1, 3);
    setSpec(specs, "N2", 12, 2, 3);

    const Season twoDivisions = buildAfcSeason(specs);
    const auto twoDivisionStatuses = ClinchStatus::compute(twoDivisions);
    REQUIRE(twoDivisionStatuses.at("W4").wildcardEliminated);
    // Wild card and division elimination are independent: W4's own division is
    // weak enough that it can still win it outright.
    REQUIRE_FALSE(twoDivisionStatuses.at("W4").divisionEliminated);

    // The blockers themselves are obviously still alive.
    REQUIRE_FALSE(twoDivisionStatuses.at("E1").wildcardEliminated);
    REQUIRE_FALSE(twoDivisionStatuses.at("N2").wildcardEliminated);
}

TEST_CASE("Playoff berth clinches once nobody can catch up", "[clinch]") {
    auto specs = uniformAfc(5, 11, 1);
    setSpec(specs, "E1", 16, 0, 1);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE(statuses.at("E1").playoffClinched);
    REQUIRE(statuses.at("E1").divisionClinched);
    REQUIRE_FALSE(statuses.at("E2").playoffClinched);
}

TEST_CASE("A division title absorbs one of the teams that could finish ahead", "[clinch]") {
    auto specs = uniformAfc(5, 11, 1);
    // Only E1's three division rivals can still reach its finishing mark. One
    // of the four has to win the division, so at most two of them can sit ahead
    // of E1 in the wild card order and E1 is in either way.
    setSpec(specs, "E1", 15, 1, 1);
    setSpec(specs, "E2", 14, 2, 1);
    setSpec(specs, "E3", 14, 2, 1);
    setSpec(specs, "E4", 14, 2, 1);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE(statuses.at("E1").playoffClinched);
    // Still a genuine four-way race for the division itself.
    REQUIRE_FALSE(statuses.at("E1").divisionClinched);
    REQUIRE_FALSE(statuses.at("E2").divisionEliminated);
}

TEST_CASE("Rivals paired inside a division block only one wild card spot each", "[clinch]") {
    auto specs = uniformAfc(2, 12, 3);
    // W1's floor is 12/17. Five teams can still reach that mark, but they sit
    // two to a division (plus one division mate), so at most one per division
    // can end up ahead of W1 without a division title in hand: two blockers,
    // one short of the three wild card spots.
    setSpec(specs, "W1", 12, 2, 3);
    setSpec(specs, "W2", 10, 4, 3);  // Same division as W1.
    setSpec(specs, "E1", 11, 3, 3);
    setSpec(specs, "E2", 10, 4, 3);
    setSpec(specs, "S1", 10, 4, 3);
    setSpec(specs, "S2", 9, 5, 3);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE(statuses.at("W1").playoffClinched);
    // The division race itself is still live, so no division clinch yet.
    REQUIRE_FALSE(statuses.at("W1").divisionClinched);
    REQUIRE_FALSE(statuses.at("W2").divisionEliminated);

    // A third division with two teams still in range adds a third possible
    // blocker, and the berth is no longer guaranteed.
    setSpec(specs, "N1", 10, 4, 3);
    setSpec(specs, "N2", 9, 5, 3);
    const Season crowded = buildAfcSeason(specs);
    const auto crowdedStatuses = ClinchStatus::compute(crowded);

    REQUIRE_FALSE(crowdedStatuses.at("W1").playoffClinched);
}

TEST_CASE("A finished season reports the settled outcome", "[clinch]") {
    auto specs = uniformAfc(8, 9, 0);
    setSpec(specs, "W1", 12, 5, 0);
    setSpec(specs, "W2", 9, 8, 0);
    setSpec(specs, "W3", 5, 12, 0);
    setSpec(specs, "W4", 4, 13, 0);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    REQUIRE(statuses.at("W1").divisionClinched);
    REQUIRE(statuses.at("W1").playoffClinched);
    for (const std::string& abbr : {"W2", "W3", "W4"}) {
        REQUIRE(statuses.at(abbr).divisionEliminated);
        REQUIRE_FALSE(statuses.at(abbr).divisionClinched);
    }

    // Exactly one division winner and three wild cards survive per conference.
    int clinchedBerths = 0;
    int wildcardEliminated = 0;
    for (const auto& [abbr, status] : statuses) {
        const Team* team = season.getTeam(abbr);
        REQUIRE(team != nullptr);
        if (team->conference() != "AFC") {
            continue;
        }
        if (status.playoffClinched) {
            ++clinchedBerths;
        }
        if (status.wildcardEliminated) {
            ++wildcardEliminated;
        }
    }
    REQUIRE(clinchedBerths == 7);
    REQUIRE(wildcardEliminated == 9);  // 16 teams, less 4 winners and 3 wild cards.
}

TEST_CASE("A finished season resolves identical records through the standings", "[clinch]") {
    // Win-percentage bounds can never separate teams that end level, so these
    // flags have to come from the final standings order instead.
    auto specs = uniformAfc(2, 15, 0);
    setSpec(specs, "N1", 9, 8, 0);
    setSpec(specs, "N2", 9, 8, 0);
    setSpec(specs, "N3", 9, 8, 0);
    setSpec(specs, "N4", 9, 8, 0);

    const Season season = buildAfcSeason(specs);
    const auto statuses = ClinchStatus::compute(season);

    const auto standings = season.teamsByDivision("AFC North");
    REQUIRE(standings.size() == 4);

    REQUIRE(statuses.at(standings[0]->abbreviation()).divisionClinched);
    for (size_t i = 1; i < standings.size(); ++i) {
        REQUIRE(statuses.at(standings[i]->abbreviation()).divisionEliminated);
        REQUIRE_FALSE(statuses.at(standings[i]->abbreviation()).divisionClinched);
    }
}
