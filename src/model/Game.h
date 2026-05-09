#ifndef GAME_H
#define GAME_H

#include <string>

/**
 * Represents a single NFL game (scheduled, in-progress, or final).
 */
class Game {
public:
    Game(int week, const std::string& date, const std::string& homeTeam,
         const std::string& awayTeam, int homeScore, int awayScore,
         const std::string& status);

    // Accessors
    int week() const { return week_; }
    const std::string& date() const { return date_; }
    const std::string& homeTeam() const { return homeTeam_; }
    const std::string& awayTeam() const { return awayTeam_; }
    int homeScore() const { return homeScore_; }
    int awayScore() const { return awayScore_; }
    const std::string& status() const { return status_; }

    // Game state queries
    bool isScheduled() const { return status_ == "scheduled"; }
    bool isInProgress() const { return status_ == "in_progress"; }
    bool isFinal() const { return status_ == "final"; }
    bool isPlayed() const { return isFinal() || isInProgress(); }

    /**
     * Returns true if home team won, false if away team won.
     * Only valid if isFinal() or isInProgress().
     * Returns true on tie.
     */
    bool homeTeamWon() const;

    /**
     * Returns true if away team won, false if home team won.
     * Only valid if isFinal() or isInProgress().
     * Returns false on tie.
     */
    bool awayTeamWon() const;

    /**
     * Returns true if game ended in a tie.
     */
    bool isTie() const;

private:
    int week_;
    std::string date_;
    std::string homeTeam_;
    std::string awayTeam_;
    int homeScore_;
    int awayScore_;
    std::string status_;  // "scheduled", "in_progress", "final"
};

#endif // GAME_H
