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
    const std::string& getAbbreviation() const { return abbreviation_; }
    const std::string& fullName() const { return fullName_; }
    const std::string& conference() const { return conference_; }
    const std::string& getConference() const { return conference_; }
    const std::string& division() const { return division_; }
    const std::string& getDivision() const { return division_; }

    // Record accessors
    int wins() const { return wins_; }
    int losses() const { return losses_; }
    int ties() const { return ties_; }
    int gamesPlayed() const { return wins_ + losses_ + ties_; }
    int pointsFor() const { return pointsFor_; }
    int pointsAgainst() const { return pointsAgainst_; }
    int pointDifferential() const { return pointsFor_ - pointsAgainst_; }

    double pointDifferentialPerGame() const;

    /**
     * Win percentage (0.0 - 1.0).
     * Ties count as 0.5 wins each.
     */
    double winPercentage() const;

    // Record updates
    void addWin() { wins_++; }
    void addLoss() { losses_++; }
    void addTie() { ties_++; }
    void addPointsFor(int points) { pointsFor_ += points; }
    void addPointsAgainst(int points) { pointsAgainst_ += points; }
    
    // Division record (wins/losses/ties within division)
    int divisionWins() const { return divisionWins_; }
    int divisionLosses() const { return divisionLosses_; }
    int divisionTies() const { return divisionTies_; }
    void addDivisionWin() { divisionWins_++; }
    void addDivisionLoss() { divisionLosses_++; }
    void addDivisionTie() { divisionTies_++; }
    
    // Conference record (wins/losses/ties within conference)
    int conferenceWins() const { return conferenceWins_; }
    int conferenceLosses() const { return conferenceLosses_; }
    int conferenceTies() const { return conferenceTies_; }
    void addConferenceWin() { conferenceWins_++; }
    void addConferenceLoss() { conferenceLosses_++; }
    void addConferenceTie() { conferenceTies_++; }
    
    void resetRecord() {
        wins_ = 0;
        losses_ = 0;
        ties_ = 0;
        divisionWins_ = 0;
        divisionLosses_ = 0;
        divisionTies_ = 0;
        conferenceWins_ = 0;
        conferenceLosses_ = 0;
        conferenceTies_ = 0;
        pointsFor_ = 0;
        pointsAgainst_ = 0;
    }

private:
    std::string abbreviation_;
    std::string fullName_;
    std::string conference_;
    std::string division_;
    int wins_ = 0;
    int losses_ = 0;
    int ties_ = 0;
    int divisionWins_ = 0;
    int divisionLosses_ = 0;
    int divisionTies_ = 0;
    int conferenceWins_ = 0;
    int conferenceLosses_ = 0;
    int conferenceTies_ = 0;
    int pointsFor_ = 0;
    int pointsAgainst_ = 0;
};

#endif // TEAM_H
