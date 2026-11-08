#ifndef CLINCHSTATUS_H
#define CLINCHSTATUS_H

#include "Season.h"
#include <map>
#include <string>

/**
 * Mathematical clinch / elimination state for a single team.
 *
 * These flags are derived from the schedule alone (not from Monte Carlo
 * sampling), so they are stable regardless of iteration count. Every test is
 * a *sufficient* condition: a flag is only set when the outcome is guaranteed
 * by win-percentage bounds, never when it merely looks likely. Tiebreakers are
 * deliberately ignored, which keeps the flags sound (a team that can still
 * finish tied is treated as alive) at the cost of sometimes setting a flag a
 * little later than the NFL's official announcement.
 */
struct TeamClinchStatus {
    bool divisionClinched = false;     // Guaranteed to finish alone atop its division.
    bool divisionEliminated = false;   // Cannot finish atop its division.
    bool playoffClinched = false;      // Guaranteed a division title or wild card berth.
    bool wildcardEliminated = false;   // Cannot earn a wild card berth.
};

/**
 * Computes mathematical clinch / elimination status for every team in a season.
 */
class ClinchStatus {
public:
    /**
     * Compute status for all teams, keyed by team abbreviation.
     *
     * @param season Season whose schedule and records should be evaluated.
     *               Standings do not need to be computed first.
     */
    static std::map<std::string, TeamClinchStatus> compute(const Season& season);
};

#endif // CLINCHSTATUS_H
