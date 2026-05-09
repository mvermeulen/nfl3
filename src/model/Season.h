#ifndef SEASON_H
#define SEASON_H

#include "Team.h"
#include "Game.h"
#include "Tiebreaker.h"
#include <map>
#include <vector>
#include <string>

/**
 * Represents the full season state: all teams, games, and computed standings.
 */
class Season {
public:
    Season() = default;

    // Team management
    void addTeam(const Team& team);
    Team* getTeam(const std::string& abbreviation);
    const Team* getTeam(const std::string& abbreviation) const;
    const std::map<std::string, Team>& allTeams() const { return teams_; }

    // Game management
    void addGame(const Game& game);
    const std::vector<Game>& allGames() const { return games_; }
    void replaceGames(const std::vector<Game>& games) { games_ = games; }

    // Standings computation
    void computeStandings();

    // Standings queries
    std::vector<Team*> teamsByDivision(const std::string& division);
    std::vector<const Team*> teamsByDivision(const std::string& division) const;
    std::vector<Team*> teamsByConference(const std::string& conference);
    std::vector<const Team*> teamsByConference(const std::string& conference) const;

    // Division list
    std::vector<std::string> getDivisions() const;

private:
    std::map<std::string, Team> teams_;  // key: abbreviation
    std::vector<Game> games_;
};

#endif // SEASON_H
