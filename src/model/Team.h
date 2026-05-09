#ifndef TEAM_H
#define TEAM_H

#include <string>

/**
 * Represents an NFL team with its metadata and record.
 */
class Team {
public:
    // Default constructor
    Team() = default;

    Team(const std::string& abbreviation, const std::string& fullName,
         const std::string& conference, const std::string& division);

    // Accessors
    const std::string& abbreviation() const { return abbreviation_; }
    const std::string& fullName() const { return fullName_; }
    const std::string& conference() const { return conference_; }
    const std::string& division() const { return division_; }

    // Record accessors
    int wins() const { return wins_; }
    int losses() const { return losses_; }
    int ties() const { return ties_; }
    int gamesPlayed() const { return wins_ + losses_ + ties_; }

    /**
     * Win percentage (0.0 - 1.0).
     * Ties count as 0.5 wins each.
     */
    double winPercentage() const;

    // Record updates
    void addWin() { wins_++; }
    void addLoss() { losses_++; }
    void addTie() { ties_++; }
    void resetRecord() {
        wins_ = 0;
        losses_ = 0;
        ties_ = 0;
    }

private:
    std::string abbreviation_;
    std::string fullName_;
    std::string conference_;
    std::string division_;
    int wins_;
    int losses_;
    int ties_;
};

#endif // TEAM_H
