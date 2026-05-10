#include "Tiebreaker.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

std::set<std::string> opponentsPlayedByTeam(const Team& team,
                                            const std::vector<Game>& games) {
    std::set<std::string> opponents;
    for (const auto& game : games) {
        if (!game.isPlayed()) continue;

        if (game.homeTeam() == team.getAbbreviation()) {
            opponents.insert(game.awayTeam());
        } else if (game.awayTeam() == team.getAbbreviation()) {
            opponents.insert(game.homeTeam());
        }
    }

    return opponents;
}

double percentageFromRecord(int wins, int losses, int ties) {
    const int total = wins + losses + ties;
    if (total == 0) {
        return -1.0;
    }
    return (wins + (0.5 * ties)) / static_cast<double>(total);
}

bool sameMetric(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

std::set<std::string> tiedTeamAbbreviations(const std::vector<Team*>& teams) {
    std::set<std::string> abbrs;
    for (const auto* team : teams) {
        abbrs.insert(team->abbreviation());
    }
    return abbrs;
}

double headToHeadWinPercentage(const Team& team,
                               const std::vector<Team*>& opponents,
                               const std::vector<Game>& games,
                               int* gamesPlayedOut = nullptr) {
    std::set<std::string> opponentAbbrs;
    for (const auto* opponent : opponents) {
        if (opponent->abbreviation() != team.abbreviation()) {
            opponentAbbrs.insert(opponent->abbreviation());
        }
    }

    int wins = 0;
    int losses = 0;
    int ties = 0;

    for (const auto& game : games) {
        if (!game.isPlayed()) {
            continue;
        }

        const bool teamIsHome = (game.homeTeam() == team.abbreviation());
        const bool teamIsAway = (game.awayTeam() == team.abbreviation());
        if (!teamIsHome && !teamIsAway) {
            continue;
        }

        const std::string opponent = teamIsHome ? game.awayTeam() : game.homeTeam();
        if (opponentAbbrs.find(opponent) == opponentAbbrs.end()) {
            continue;
        }

        if (game.isTie()) {
            ties++;
        } else if ((teamIsHome && game.homeTeamWon()) || (teamIsAway && game.awayTeamWon())) {
            wins++;
        } else {
            losses++;
        }
    }

    if (gamesPlayedOut != nullptr) {
        *gamesPlayedOut = wins + losses + ties;
    }
    return percentageFromRecord(wins, losses, ties);
}

double commonGamesWinPercentage(const Team& team,
                                const std::set<std::string>& commonOpponents,
                                const std::vector<Game>& games,
                                int* gamesPlayedOut = nullptr) {
    int wins = 0;
    int losses = 0;
    int ties = 0;

    for (const auto& game : games) {
        if (!game.isPlayed()) continue;

        bool teamIsHome = (game.homeTeam() == team.getAbbreviation());
        bool teamIsAway = (game.awayTeam() == team.getAbbreviation());

        if (!teamIsHome && !teamIsAway) continue;

        std::string opponentAbbr = teamIsHome ? game.awayTeam() : game.homeTeam();
        if (commonOpponents.find(opponentAbbr) == commonOpponents.end()) continue;

        if (game.isTie()) {
            ties++;
        } else if ((teamIsHome && game.homeTeamWon()) ||
                   (teamIsAway && game.awayTeamWon())) {
            wins++;
        } else {
            losses++;
        }
    }

    const int totalGames = wins + losses + ties;
    if (gamesPlayedOut != nullptr) {
        *gamesPlayedOut = totalGames;
    }
    if (totalGames == 0) {
        return -1.0;
    }

    return (wins + (ties * 0.5)) / static_cast<double>(totalGames);
}

std::vector<Team*> topTeamsByMetric(const std::vector<Team*>& teams,
                                    const std::map<Team*, double>& metrics,
                                    bool* criterionApplied = nullptr) {
    if (teams.empty()) {
        return {};
    }

    double best = -2.0;
    bool hasMetric = false;
    for (auto* team : teams) {
        auto it = metrics.find(team);
        if (it == metrics.end()) {
            continue;
        }
        if (!hasMetric || it->second > best) {
            best = it->second;
            hasMetric = true;
        }
    }

    if (!hasMetric) {
        if (criterionApplied != nullptr) {
            *criterionApplied = false;
        }
        return teams;
    }

    std::vector<Team*> top;
    for (auto* team : teams) {
        const double value = metrics.at(team);
        if (sameMetric(value, best)) {
            top.push_back(team);
        }
    }

    if (criterionApplied != nullptr) {
        *criterionApplied = true;
    }
    return top;
}

Team* selectTopTeam(std::vector<Team*> tied,
                    const std::vector<Game>& games,
                    const std::map<std::string, Team>& allTeams,
                    const std::string& divisionName) {
    if (tied.size() == 1) {
        return tied.front();
    }

    // 1) Head-to-head among tied teams.
    if (tied.size() == 2) {
        const int h2h = Tiebreaker::headToHeadRecord(*tied[0], *tied[1], games);
        if (h2h > 0) {
            return tied[0];
        }
        if (h2h < 0) {
            return tied[1];
        }
    } else {
        std::map<Team*, double> h2h;
        for (auto* team : tied) {
            h2h[team] = headToHeadWinPercentage(*team, tied, games);
        }
        bool applied = false;
        auto narrowed = topTeamsByMetric(tied, h2h, &applied);
        if (applied && narrowed.size() == 1) {
            return narrowed.front();
        }
        if (applied && narrowed.size() < tied.size()) {
            tied = std::move(narrowed);
        }
    }

    // 2) Division record (division ties only).
    if (!divisionName.empty()) {
        std::map<Team*, double> divisionPct;
        for (auto* team : tied) {
            divisionPct[team] = percentageFromRecord(team->divisionWins(),
                                                     team->divisionLosses(),
                                                     team->divisionTies());
        }
        bool applied = false;
        auto narrowed = topTeamsByMetric(tied, divisionPct, &applied);
        if (applied && narrowed.size() == 1) {
            return narrowed.front();
        }
        if (applied && narrowed.size() < tied.size()) {
            tied = std::move(narrowed);
        }
    }

    // 3) Common games (minimum 4 games each) if applicable.
    {
        std::set<std::string> commonOpponents;
        bool initialized = false;
        const auto tiedAbbrs = tiedTeamAbbreviations(tied);

        for (auto* team : tied) {
            auto opponents = opponentsPlayedByTeam(*team, games);
            for (const auto& tiedAbbr : tiedAbbrs) {
                opponents.erase(tiedAbbr);
            }

            if (!initialized) {
                commonOpponents = std::move(opponents);
                initialized = true;
            } else {
                std::set<std::string> intersection;
                std::set_intersection(commonOpponents.begin(), commonOpponents.end(),
                                      opponents.begin(), opponents.end(),
                                      std::inserter(intersection, intersection.begin()));
                commonOpponents = std::move(intersection);
            }
        }

        if (!commonOpponents.empty()) {
            std::map<Team*, double> commonPct;
            bool minimumGamesSatisfied = true;
            for (auto* team : tied) {
                int commonGamesPlayed = 0;
                commonPct[team] = commonGamesWinPercentage(*team, commonOpponents, games,
                                                           &commonGamesPlayed);
                if (commonGamesPlayed < 4) {
                    minimumGamesSatisfied = false;
                }
            }

            if (minimumGamesSatisfied) {
                bool applied = false;
                auto narrowed = topTeamsByMetric(tied, commonPct, &applied);
                if (applied && narrowed.size() == 1) {
                    return narrowed.front();
                }
                if (applied && narrowed.size() < tied.size()) {
                    tied = std::move(narrowed);
                }
            }
        }
    }

    // 4) Conference record.
    {
        std::map<Team*, double> conferencePct;
        for (auto* team : tied) {
            conferencePct[team] = percentageFromRecord(team->conferenceWins(),
                                                       team->conferenceLosses(),
                                                       team->conferenceTies());
        }
        bool applied = false;
        auto narrowed = topTeamsByMetric(tied, conferencePct, &applied);
        if (applied && narrowed.size() == 1) {
            return narrowed.front();
        }
        if (applied && narrowed.size() < tied.size()) {
            tied = std::move(narrowed);
        }
    }

    // 5) Strength of victory.
    {
        std::map<Team*, double> sov;
        for (auto* team : tied) {
            sov[team] = static_cast<double>(Tiebreaker::strengthOfVictory(*team, games, allTeams));
        }
        bool applied = false;
        auto narrowed = topTeamsByMetric(tied, sov, &applied);
        if (applied && narrowed.size() == 1) {
            return narrowed.front();
        }
        if (applied && narrowed.size() < tied.size()) {
            tied = std::move(narrowed);
        }
    }

    // 6) Strength of schedule.
    {
        std::map<Team*, double> sos;
        for (auto* team : tied) {
            sos[team] = static_cast<double>(Tiebreaker::strengthOfSchedule(*team, games, allTeams));
        }
        bool applied = false;
        auto narrowed = topTeamsByMetric(tied, sos, &applied);
        if (applied && narrowed.size() == 1) {
            return narrowed.front();
        }
        if (applied && narrowed.size() < tied.size()) {
            tied = std::move(narrowed);
        }
    }

    // 7) Deterministic fallback.
    std::sort(tied.begin(), tied.end(), [](const Team* a, const Team* b) {
        return a->abbreviation() < b->abbreviation();
    });
    return tied.front();
}

}  // namespace

std::vector<Team*> Tiebreaker::breakTie(std::vector<Team*>& teams, 
                                         const std::vector<Game>& games,
                                         const std::map<std::string, Team>& allTeams,
                                         const std::string& divisionName) {
    if (teams.size() <= 1) {
        return teams;
    }

    std::vector<Team*> remaining = teams;
    std::vector<Team*> ordered;
    ordered.reserve(teams.size());

    while (!remaining.empty()) {
        Team* winner = selectTopTeam(remaining, games, allTeams, divisionName);
        ordered.push_back(winner);
        remaining.erase(std::remove(remaining.begin(), remaining.end(), winner), remaining.end());
    }

    return ordered;
}

int Tiebreaker::headToHeadRecord(const Team& team1, const Team& team2, 
                                 const std::vector<Game>& games) {
    int team1Wins = 0;
    int team2Wins = 0;

    for (const auto& game : games) {
        if (!game.isPlayed()) continue;

        // team1 home vs team2 away
        if (game.homeTeam() == team1.getAbbreviation() && 
            game.awayTeam() == team2.getAbbreviation()) {
            if (game.homeTeamWon()) team1Wins++;
            else if (game.awayTeamWon()) team2Wins++;
        }
        // team1 away vs team2 home
        else if (game.awayTeam() == team1.getAbbreviation() && 
                 game.homeTeam() == team2.getAbbreviation()) {
            if (game.awayTeamWon()) team1Wins++;
            else if (game.homeTeamWon()) team2Wins++;
        }
    }

    if (team1Wins > team2Wins) return 1;
    if (team2Wins > team1Wins) return -1;
    return 0; // Tied
}

int Tiebreaker::strengthOfVictory(const Team& team, const std::vector<Game>& games,
                                  const std::map<std::string, Team>& allTeams) {
    int totalWins = 0;

    for (const auto& game : games) {
        if (!game.isPlayed()) continue;

        bool teamIsHome = (game.homeTeam() == team.getAbbreviation());
        bool teamIsAway = (game.awayTeam() == team.getAbbreviation());

        if (!teamIsHome && !teamIsAway) continue;

        // Only count opponents team defeated
        bool teamWon = false;
        if (teamIsHome && game.homeTeamWon()) teamWon = true;
        if (teamIsAway && game.awayTeamWon()) teamWon = true;

        if (teamWon) {
            // Get opponent abbreviation
            std::string opponentAbbr = teamIsHome ? game.awayTeam() : game.homeTeam();
            
            // Look up opponent in allTeams map
            auto it = allTeams.find(opponentAbbr);
            if (it != allTeams.end()) {
                totalWins += it->second.wins();
            }
        }
    }
    return totalWins;
}

int Tiebreaker::strengthOfSchedule(const Team& team, const std::vector<Game>& games,
                                   const std::map<std::string, Team>& allTeams) {
    int totalWins = 0;

    for (const auto& game : games) {
        if (!game.isPlayed()) continue;

        bool teamIsHome = (game.homeTeam() == team.getAbbreviation());
        bool teamIsAway = (game.awayTeam() == team.getAbbreviation());

        if (!teamIsHome && !teamIsAway) continue;

        // Get opponent abbreviation
        std::string opponentAbbr = teamIsHome ? game.awayTeam() : game.homeTeam();
        
        // Look up opponent in allTeams map and add their wins
        auto it = allTeams.find(opponentAbbr);
        if (it != allTeams.end()) {
            totalWins += it->second.wins();
        }
    }
    return totalWins;
}

