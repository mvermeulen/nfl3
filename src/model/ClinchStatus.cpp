#include "ClinchStatus.h"
#include <exception>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Win-percentage comparisons are built from halves, so a small tolerance keeps
// floating point noise from reading as a guaranteed gap.
constexpr double kEpsilon = 1e-9;

// Best and worst win percentage a team can still finish with, assuming it wins
// (respectively loses) every remaining game on its schedule.
struct RecordBounds {
    double floorPct = 0.0;
    double ceilingPct = 0.0;
    std::string division;
    std::string conference;
};

bool strictlyGreater(double a, double b) {
    return a > b + kEpsilon;
}

bool greaterOrEqual(double a, double b) {
    return a >= b - kEpsilon;
}

int seasonYearFromSchedule(const Season& season) {
    int seasonYear = -1;
    for (const auto& game : season.allGames()) {
        const std::string& date = game.date();
        if (date.size() < 4) {
            continue;
        }
        try {
            const int year = std::stoi(date.substr(0, 4));
            if (seasonYear < 0 || year < seasonYear) {
                seasonYear = year;
            }
        } catch (const std::exception&) {
            // Ignore malformed dates and keep scanning.
        }
    }
    return seasonYear;
}

int wildcardSpotsPerConference(const Season& season) {
    // NFL expanded from 2 to 3 wild cards per conference in 2020. An unknown
    // season year means there is no schedule to reason about, so assume modern.
    const int seasonYear = seasonYearFromSchedule(season);
    if (seasonYear < 0 || seasonYear >= 2020) {
        return 3;
    }
    return 2;
}

std::unordered_map<std::string, RecordBounds> computeBounds(const Season& season) {
    std::unordered_map<std::string, int> remaining;
    for (const auto& [abbr, team] : season.allTeams()) {
        remaining[abbr] = 0;
    }
    for (const auto& game : season.allGames()) {
        if (game.isPlayed()) {
            continue;
        }
        // Only count games against teams this season knows about, matching how
        // computeStandings() skips unknown abbreviations.
        if (remaining.count(game.homeTeam()) && remaining.count(game.awayTeam())) {
            remaining[game.homeTeam()]++;
            remaining[game.awayTeam()]++;
        }
    }

    std::unordered_map<std::string, RecordBounds> bounds;
    for (const auto& [abbr, team] : season.allTeams()) {
        const double winPoints = team.wins() + 0.5 * team.ties();
        const int left = remaining[abbr];
        const int total = team.gamesPlayed() + left;

        RecordBounds b;
        b.division = team.division();
        b.conference = team.conference();
        if (total > 0) {
            b.floorPct = winPoints / total;
            b.ceilingPct = (winPoints + left) / total;
        }
        bounds[abbr] = b;
    }
    return bounds;
}

bool allGamesPlayed(const Season& season) {
    for (const auto& game : season.allGames()) {
        if (!game.isPlayed()) {
            return false;
        }
    }
    return true;
}

/**
 * Exact status for a finished season. Win-percentage bounds can't separate
 * teams that end on identical records, but once every game is in the books the
 * standings order is itself final, so the seeding settles those ties outright.
 */
std::map<std::string, TeamClinchStatus> finalStandingsStatuses(const Season& season,
                                                               int wildcardSpots) {
    std::map<std::string, TeamClinchStatus> statuses;
    for (const auto& [abbr, team] : season.allTeams()) {
        statuses[abbr] = TeamClinchStatus{};
    }

    std::set<std::string> divisionWinners;
    for (const auto& division : season.getDivisions()) {
        const auto standings = season.teamsByDivision(division);
        for (size_t i = 0; i < standings.size(); ++i) {
            TeamClinchStatus& status = statuses[standings[i]->abbreviation()];
            if (i == 0) {
                status.divisionClinched = true;
                status.playoffClinched = true;
                divisionWinners.insert(standings[i]->abbreviation());
            } else {
                status.divisionEliminated = true;
            }
        }
    }

    for (const std::string& conference : {std::string("AFC"), std::string("NFC")}) {
        int wildcardsAwarded = 0;
        for (const auto* team : season.teamsByConference(conference)) {
            const std::string& abbr = team->abbreviation();
            if (divisionWinners.count(abbr)) {
                continue;
            }
            if (wildcardsAwarded < wildcardSpots) {
                statuses[abbr].playoffClinched = true;
                ++wildcardsAwarded;
            } else {
                statuses[abbr].wildcardEliminated = true;
            }
        }
    }

    return statuses;
}

}  // namespace

std::map<std::string, TeamClinchStatus> ClinchStatus::compute(const Season& season) {
    Season evaluated = season;
    evaluated.computeStandings();

    const int wildcardSpots = wildcardSpotsPerConference(evaluated);
    if (allGamesPlayed(evaluated)) {
        return finalStandingsStatuses(evaluated, wildcardSpots);
    }

    const auto bounds = computeBounds(evaluated);

    // Division membership, used to reason about how many of the teams ahead of
    // a given team can be soaking up a division title rather than a wild card.
    std::map<std::string, std::vector<std::string>> divisionMembers;
    for (const auto& [abbr, b] : bounds) {
        divisionMembers[b.division].push_back(abbr);
    }

    std::map<std::string, TeamClinchStatus> statuses;

    for (const auto& [abbr, self] : bounds) {
        TeamClinchStatus status;

        // --- Division title -------------------------------------------------
        // Eliminated when a rival's worst case still beats this team's best
        // case; clinched when the reverse holds against every rival.
        // A division with a single team can only come from a partial fixture,
        // so leave it unflagged rather than credit a walkover title.
        const auto& rivals = divisionMembers[self.division];
        bool clinchedDivision = rivals.size() > 1;
        for (const auto& rivalAbbr : rivals) {
            if (rivalAbbr == abbr) {
                continue;
            }
            const RecordBounds& rival = bounds.at(rivalAbbr);
            if (strictlyGreater(rival.floorPct, self.ceilingPct)) {
                status.divisionEliminated = true;
            }
            if (!strictlyGreater(self.floorPct, rival.ceilingPct)) {
                clinchedDivision = false;
            }
        }
        status.divisionClinched = clinchedDivision;

        // --- Wild card ------------------------------------------------------
        // Count conference teams guaranteed to finish ahead of this one. At
        // most one team per division can be soaking up that division's title,
        // so the rest are guaranteed to be ahead in the wild card order.
        std::set<std::string> guaranteedAheadDivisions;
        int guaranteedAhead = 0;

        // Conference teams that could still finish ahead of (or tied with)
        // this one. Every team that ends up ahead must come from this set.
        std::set<std::string> possiblyAhead;

        for (const auto& [otherAbbr, other] : bounds) {
            if (otherAbbr == abbr || other.conference != self.conference) {
                continue;
            }
            if (strictlyGreater(other.floorPct, self.ceilingPct)) {
                ++guaranteedAhead;
                guaranteedAheadDivisions.insert(other.division);
            }
            if (greaterOrEqual(other.ceilingPct, self.floorPct)) {
                possiblyAhead.insert(otherAbbr);
            }
        }

        const int minNonWinnersAhead =
            guaranteedAhead - static_cast<int>(guaranteedAheadDivisions.size());
        status.wildcardEliminated = minNonWinnersAhead >= wildcardSpots;

        // A division's champion outranks its own division mates, so if any team
        // from a division finishes ahead of this one then that division's
        // champion does too. Each division therefore spends one of its
        // finishers-ahead on a division title rather than on a wild card spot,
        // capping the wild card blockers it can contribute at one less than the
        // teams it has still in range. For this team's own division the
        // alternative is that it wins the division outright, which secures a
        // berth just the same.
        int maxNonWinnersAhead = 0;
        for (const auto& [division, members] : divisionMembers) {
            if (bounds.at(members.front()).conference != self.conference) {
                continue;
            }
            int inRange = 0;
            for (const auto& memberAbbr : members) {
                if (memberAbbr != abbr && possiblyAhead.count(memberAbbr) > 0) {
                    ++inRange;
                }
            }
            if (inRange > 1) {
                maxNonWinnersAhead += inRange - 1;
            }
        }

        status.playoffClinched = maxNonWinnersAhead < wildcardSpots;

        statuses[abbr] = status;
    }

    return statuses;
}
