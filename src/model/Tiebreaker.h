#ifndef TIEBREAKER_H
#define TIEBREAKER_H

#include "Team.h"
#include "Game.h"
#include <vector>
#include <string>
#include <map>

/**
 * Tiebreaker implements NFL playoff tiebreaker rules to determine final standings.
 * Handles both division-based tiebreakers and wild-card tiebreakers.
 */
class Tiebreaker {
public:
    /**
     * Breaks tie between teams within the same division or playoff position.
     * Applies NFL tiebreaker rules in sequence until a clear ranking emerges.
     *
     * @param teams Vector of tied teams (may be > 2)
     * @param games Vector of all games played (for computing head-to-head records)
     * @param allTeams Map of all teams for looking up opponent records
     * @param divisionName Division name (for division-based tiebreakers)
     * @return Sorted vector of teams (first = tiebreaker winner)
     */
    static std::vector<Team*> breakTie(std::vector<Team*>& teams, 
                                       const std::vector<Game>& games,
                                       const std::map<std::string, Team>& allTeams,
                                       const std::string& divisionName = "");

private:
    /**
     * Helper: Compute head-to-head record between two teams.
     * @param team1 First team
     * @param team2 Second team
     * @param games All games
     * @return wins for team1 vs team2 (0 = team2 ahead, 1 = team1 ahead, -1 = tied)
     */
    static int headToHeadRecord(const Team& team1, const Team& team2, 
                               const std::vector<Game>& games);

    /**
     * Helper: Count head-to-head wins for a team against specific opponents.
     * @param team Team to check
     * @param opponents Teams to compute head-to-head record against
     * @param games All games
     * @return Wins by team against any opponent in the list
     */
    static int headToHeadWins(const Team& team, const std::vector<Team*>& opponents,
                             const std::vector<Game>& games);

    /**
     * Helper: Compute strength of victory (total wins of defeated opponents).
     * @param team Team to check
     * @param games All games
     * @param allTeams All teams for looking up opponent records
     * @return Cumulative wins of all teams this team defeated
     */
    static int strengthOfVictory(const Team& team, const std::vector<Game>& games,
                                 const std::map<std::string, Team>& allTeams);

    /**
     * Helper: Compute strength of schedule (total wins of all opponents).
     * @param team Team to check
     * @param games All games
     * @param allTeams All teams for looking up opponent records
     * @return Cumulative wins of all teams this team played
     */
    static int strengthOfSchedule(const Team& team, const std::vector<Game>& games,
                                  const std::map<std::string, Team>& allTeams);
};

#endif // TIEBREAKER_H
