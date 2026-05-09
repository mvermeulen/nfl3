#include "Season.h"
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

        if (game.isTie()) {
            homeTeam->addTie();
            awayTeam->addTie();
        } else if (game.homeTeamWon()) {
            homeTeam->addWin();
            awayTeam->addLoss();
        } else {
            awayTeam->addWin();
            homeTeam->addLoss();
        }
    }
}

std::vector<Team*> Season::teamsByDivision(const std::string& division) {
    std::vector<Team*> result;
    for (auto& [abbr, team] : teams_) {
        if (team.division() == division) {
            result.push_back(&team);
        }
    }
    // Sort by win percentage (descending), then by wins (descending)
    std::sort(result.begin(), result.end(),
              [](const Team* a, const Team* b) {
                  if (a->winPercentage() != b->winPercentage()) {
                      return a->winPercentage() > b->winPercentage();
                  }
                  return a->wins() > b->wins();
              });
    return result;
}

std::vector<const Team*> Season::teamsByDivision(const std::string& division) const {
    std::vector<const Team*> result;
    for (const auto& [abbr, team] : teams_) {
        if (team.division() == division) {
            result.push_back(&team);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const Team* a, const Team* b) {
                  if (a->winPercentage() != b->winPercentage()) {
                      return a->winPercentage() > b->winPercentage();
                  }
                  return a->wins() > b->wins();
              });
    return result;
}

std::vector<Team*> Season::teamsByConference(const std::string& conference) {
    std::vector<Team*> result;
    for (auto& [abbr, team] : teams_) {
        if (team.conference() == conference) {
            result.push_back(&team);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const Team* a, const Team* b) {
                  if (a->winPercentage() != b->winPercentage()) {
                      return a->winPercentage() > b->winPercentage();
                  }
                  return a->wins() > b->wins();
              });
    return result;
}

std::vector<const Team*> Season::teamsByConference(const std::string& conference) const {
    std::vector<const Team*> result;
    for (const auto& [abbr, team] : teams_) {
        if (team.conference() == conference) {
            result.push_back(&team);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const Team* a, const Team* b) {
                  if (a->winPercentage() != b->winPercentage()) {
                      return a->winPercentage() > b->winPercentage();
                  }
                  return a->wins() > b->wins();
              });
    return result;
}

std::vector<std::string> Season::getDivisions() const {
    std::set<std::string> divisionSet;
    for (const auto& [abbr, team] : teams_) {
        divisionSet.insert(team.division());
    }
    return std::vector<std::string>(divisionSet.begin(), divisionSet.end());
}
