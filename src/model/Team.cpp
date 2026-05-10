#include "Team.h"

Team::Team(const std::string& abbreviation, const std::string& fullName,
           const std::string& conference, const std::string& division)
    : abbreviation_(abbreviation), fullName_(fullName),
      conference_(conference), division_(division),
      wins_(0), losses_(0), ties_(0),
      divisionWins_(0), divisionLosses_(0), divisionTies_(0),
      conferenceWins_(0), conferenceLosses_(0), conferenceTies_(0) {}

double Team::winPercentage() const {
    int total = gamesPlayed();
    if (total == 0) return 0.0;
    return (wins_ + 0.5 * ties_) / total;
}

double Team::pointDifferentialPerGame() const {
  const int total = gamesPlayed();
  if (total == 0) {
    return 0.0;
  }
  return static_cast<double>(pointsFor_ - pointsAgainst_) / static_cast<double>(total);
}
