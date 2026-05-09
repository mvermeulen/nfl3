#include "Tiebreaker.h"
#include <algorithm>
#include <map>

std::vector<Team*> Tiebreaker::breakTie(std::vector<Team*>& teams, 
                                         const std::vector<Game>& games,
                                         const std::map<std::string, Team>& allTeams,
                                         const std::string& divisionName) {
    if (teams.size() == 1) {
        return teams;
    }

    // Make a copy to work with
    std::vector<Team*> sorted = teams;

    // Tiebreaker 1: Head-to-head record (if exactly 2 teams in same division)
    if (sorted.size() == 2 && !divisionName.empty()) {
        int h2h = headToHeadRecord(*sorted[0], *sorted[1], games);
        if (h2h != 0) {
            if (h2h < 0) {
                std::swap(sorted[0], sorted[1]);
            }
            return sorted;
        }
    }

    // Tiebreaker 2: Division win-loss record (if in same division)
    if (!divisionName.empty()) {
        int div0 = sorted[0]->divisionWins();
        int div1 = sorted.size() > 1 ? sorted[1]->divisionWins() : -1;
        
        std::sort(sorted.begin(), sorted.end(), [](Team* a, Team* b) {
            return a->divisionWins() > b->divisionWins();
        });
        
        if (sorted.size() == 2 && sorted[0]->divisionWins() != div1) {
            return sorted;
        }
    }

    // Tiebreaker 3: Conference win-loss record
    std::sort(sorted.begin(), sorted.end(), [](Team* a, Team* b) {
        return a->conferenceWins() > b->conferenceWins();
    });

    if (sorted.size() == 2 && sorted[0]->conferenceWins() != sorted[1]->conferenceWins()) {
        return sorted;
    }

    // Tiebreaker 4: Strength of victory
    std::sort(sorted.begin(), sorted.end(), [&games, &allTeams](Team* a, Team* b) {
        return strengthOfVictory(*a, games, allTeams) > strengthOfVictory(*b, games, allTeams);
    });

    if (sorted.size() == 2 && 
        strengthOfVictory(*sorted[0], games, allTeams) != strengthOfVictory(*sorted[1], games, allTeams)) {
        return sorted;
    }

    // Tiebreaker 5: Strength of schedule
    std::sort(sorted.begin(), sorted.end(), [&games, &allTeams](Team* a, Team* b) {
        return strengthOfSchedule(*a, games, allTeams) > strengthOfSchedule(*b, games, allTeams);
    });

    // If we get here and still tied, the order is as good as we can compute
    return sorted;
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

int Tiebreaker::headToHeadWins(const Team& team, const std::vector<Team*>& opponents,
                               const std::vector<Game>& games) {
    int wins = 0;
    for (const auto& game : games) {
        if (!game.isPlayed()) continue;

        bool teamIsHome = (game.homeTeam() == team.getAbbreviation());
        bool teamIsAway = (game.awayTeam() == team.getAbbreviation());

        if (!teamIsHome && !teamIsAway) continue;

        // Check if opponent is in the list
        bool opponentFound = false;
        std::string opponent = teamIsHome ? game.awayTeam() : game.homeTeam();
        for (const auto& opp : opponents) {
            if (opp->getAbbreviation() == opponent) {
                opponentFound = true;
                break;
            }
        }

        if (!opponentFound) continue;

        // Count win if team won
        if ((teamIsHome && game.homeTeamWon()) || 
            (teamIsAway && game.awayTeamWon())) {
            wins++;
        }
    }
    return wins;
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

