#include "Season.h"
#include "Tiebreaker.h"
#include <algorithm>
#include <set>

void Season::addTeam(const Team& team) {
    teams_[team.abbreviation()] = team;
}

Team* Season::getTeam(const std::string& abbreviation) {
    auto it = teams_.find(abbreviation);
    if (it != teams_.end()) {
        return &it->second;
    }
    return nullptr;
}

const Team* Season::getTeam(const std::string& abbreviation) const {
    auto it = teams_.find(abbreviation);
    if (it != teams_.end()) {
        return &it->second;
    }
    return nullptr;
}

void Season::addGame(const Game& game) {
    games_.push_back(game);
}

void Season::computeStandings() {
    // Reset all team records
    for (auto& [abbr, team] : teams_) {
        team.resetRecord();
    }

    // Iterate through all games and update records
    for (const auto& game : games_) {
        if (!game.isPlayed()) continue;

        Team* homeTeam = getTeam(game.homeTeam());
        Team* awayTeam = getTeam(game.awayTeam());

        if (!homeTeam || !awayTeam) continue;  // Safeguard

        // Check if game is within division
        bool sameDivision = (homeTeam->division() == awayTeam->division());
        
        // Check if game is within conference
        bool sameConference = (homeTeam->conference() == awayTeam->conference());

        if (game.isTie()) {
            homeTeam->addTie();
            awayTeam->addTie();
            if (sameDivision) {
                homeTeam->addDivisionTie();
                awayTeam->addDivisionTie();
            }
            if (sameConference) {
                homeTeam->addConferenceTie();
                awayTeam->addConferenceTie();
            }
        } else if (game.homeTeamWon()) {
            homeTeam->addWin();
            awayTeam->addLoss();
            if (sameDivision) {
                homeTeam->addDivisionWin();
                awayTeam->addDivisionLoss();
            }
            if (sameConference) {
                homeTeam->addConferenceWin();
                awayTeam->addConferenceLoss();
            }
        } else {
            awayTeam->addWin();
            homeTeam->addLoss();
            if (sameDivision) {
                awayTeam->addDivisionWin();
                homeTeam->addDivisionLoss();
            }
            if (sameConference) {
                awayTeam->addConferenceWin();
                homeTeam->addConferenceLoss();
            }
        }

        homeTeam->addPointsFor(game.homeScore());
        homeTeam->addPointsAgainst(game.awayScore());
        awayTeam->addPointsFor(game.awayScore());
        awayTeam->addPointsAgainst(game.homeScore());
    }
}

std::vector<Team*> Season::teamsByDivision(const std::string& division) {
    std::vector<Team*> result;
    for (auto& [abbr, team] : teams_) {
        if (team.division() == division) {
            result.push_back(&team);
        }
    }
    
    // Group teams by win percentage
    std::map<double, std::vector<Team*>> winPctGroups;
    for (auto* team : result) {
        winPctGroups[team->winPercentage()].push_back(team);
    }
    
    // Sort by win percentage (descending), and apply tiebreaker within groups
    result.clear();
    for (auto it = winPctGroups.rbegin(); it != winPctGroups.rend(); ++it) {
        auto& group = it->second;
        if (group.size() == 1) {
            result.push_back(group[0]);
        } else {
            // Apply tiebreaker rules
            auto tiebreakerResult = Tiebreaker::breakTie(group, games_, teams_, division);
            for (auto* team : tiebreakerResult) {
                result.push_back(team);
            }
        }
    }
    return result;
}

std::vector<const Team*> Season::teamsByDivision(const std::string& division) const {
    // Use non-const version and cast result
    auto nonConstThis = const_cast<Season*>(this);
    auto result = nonConstThis->teamsByDivision(division);
    std::vector<const Team*> constResult;
    for (auto* team : result) {
        constResult.push_back(team);
    }
    return constResult;
}

std::vector<Team*> Season::teamsByConference(const std::string& conference) {
    std::vector<Team*> result;
    for (auto& [abbr, team] : teams_) {
        if (team.conference() == conference) {
            result.push_back(&team);
        }
    }
    
    // Group teams by win percentage
    std::map<double, std::vector<Team*>> winPctGroups;
    for (auto* team : result) {
        winPctGroups[team->winPercentage()].push_back(team);
    }
    
    // Sort by win percentage (descending), and apply tiebreaker within groups
    result.clear();
    for (auto it = winPctGroups.rbegin(); it != winPctGroups.rend(); ++it) {
        auto& group = it->second;
        if (group.size() == 1) {
            result.push_back(group[0]);
        } else {
            // Apply tiebreaker rules (no division context for conference tiebreaker)
            auto tiebreakerResult = Tiebreaker::breakTie(group, games_, teams_);
            for (auto* team : tiebreakerResult) {
                result.push_back(team);
            }
        }
    }
    return result;
}

std::vector<const Team*> Season::teamsByConference(const std::string& conference) const {
    // Use non-const version and cast result
    auto nonConstThis = const_cast<Season*>(this);
    auto result = nonConstThis->teamsByConference(conference);
    std::vector<const Team*> constResult;
    for (auto* team : result) {
        constResult.push_back(team);
    }
    return constResult;
}

std::vector<std::string> Season::getDivisions() const {
    std::set<std::string> divisionSet;
    for (const auto& [abbr, team] : teams_) {
        divisionSet.insert(team.division());
    }
    return std::vector<std::string>(divisionSet.begin(), divisionSet.end());
}
