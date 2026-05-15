#ifndef MONTECARLO_H
#define MONTECARLO_H

#include "Season.h"
#include <map>
#include <vector>
#include <string>
#include <random>

/**
 * Represents results from a single Monte Carlo simulation.
 */
struct PlayoffOutcome {
    std::map<std::string, int> playoffAppearances;  // team abbr -> count
    std::map<std::string, int> divisionWins;        // team abbr -> count
    std::map<std::string, int> wildcardWins;        // team abbr -> count
    std::map<std::string, int> simulatedWins;       // team abbr -> summed final wins
};

/**
 * Represents complete simulation results.
 */
struct SimulationResults {
    int totalIterations;
    
    // Playoff probabilities (0.0 - 1.0)
    std::map<std::string, double> playoffProbability;
    std::map<std::string, double> divisionWinProbability;
    std::map<std::string, double> wildcardProbability;
    
    // Win probabilities for unplayed games
    std::map<std::string, double> teamWinProbability;  // team abbr -> estimated win %
    
    PlayoffOutcome outcomes;
};

/**
 * Represents playoff probability impact for a single game.
 */
struct GameImpact {
    int week;
    std::string date;
    std::string homeTeam;
    std::string awayTeam;
    double homeDeltaPlayoffProb;
    double awayDeltaPlayoffProb;
    double homePlayoffProbIfHomeWins;
    double awayPlayoffProbIfAwayWins;
};

/**
 * Represents impact analysis for the next unplayed week.
 */
struct ImpactAnalysisResults {
    int week;
    std::vector<GameImpact> gameImpacts;
};

/**
 * MonteCarlo implements a probabilistic playoff simulator.
 * Simulates remaining games using win probability models based on team strength
 * and home field advantage.
 */
class MonteCarlo {
public:
    /**
     * Default constructor.
     */
    MonteCarlo() = default;

    /**
     * Run Monte Carlo simulation on a season.
     * Simulates all unplayed games and determines playoff outcomes.
     * 
     * @param season The current season state
     * @param iterations Number of simulation runs (default 100,000)
     * @param seed Random seed for reproducibility (optional)
     * @return SimulationResults with playoff probabilities and outcomes
     */
    SimulationResults simulate(const Season& season, 
                              int iterations = 100000,
                              unsigned int seed = 0);

    /**
     * Analyze how each game in the next unplayed week changes playoff odds.
     * For each game, runs two forced scenarios: home win and away win.
     *
     * @param season The current season state
     * @param iterations Number of simulation runs per scenario
     * @param seed Random seed for reproducibility
     * @return ImpactAnalysisResults for next unplayed week
     */
    ImpactAnalysisResults analyzeImpact(const Season& season,
                                        int iterations = 100000,
                                        unsigned int seed = 12345);

    /**
     * Get win probability for a specific game based on team strength.
     * Accounts for home field advantage and team records.
     * 
     * @param homeTeam The home team
     * @param awayTeam The away team
     * @return Probability home team wins (0.0 - 1.0)
     */
    double getWinProbability(const Team& homeTeam, const Team& awayTeam) const;

    /**
     * Calculate base win probability component from team strength.
     * Based on current win percentage with adjustment for games played.
     * 
     * @param team Team to evaluate
     * @return Strength factor (0.0 - 1.0)
     */
    double getTeamStrengthFactor(const Team& team) const;

    /**
     * Configure model coefficients used by getWinProbability.
     */
    void setModelParameters(double homeAdvantage,
                            double strengthWeight,
                            double pointDifferentialWeight = 0.0);

    /**
     * Load historical team win percentages from the prior season.
     * Automatically detects the current season year and loads prior year data.
     * 
     * @param season The current season (used to detect year)
     * @param historicalDataDir Path to historical data directory (e.g., "data/historical")
     * @return true if historical data was loaded successfully, false otherwise
     */
    bool loadHistoricalStrengths(const Season& season, 
                                 const std::string& historicalDataDir = "data/historical");

private:
    mutable std::mt19937 rng_;  // Random number generator
    double homeAdvantage_ = 0.57;
    double strengthWeight_ = 0.15;
    double pointDifferentialWeight_ = 0.0;
    std::map<std::string, double> priorYearStrengths_;  // Historical team strength factors
    
    /**
     * Simulate a single iteration of the season.
     * Starting from current state, simulates all unplayed games
     * and computes playoff seeding.
     * 
     * @param season Base season state to simulate from
     * @return Outcome of this simulation run
     */
    PlayoffOutcome simulateIteration(const Season& season);
    
    /**
     * Simulate remaining games for a season copy.
     * Fills in scores for all unplayed games based on win probabilities.
     * 
     * @param season Season to modify (will add simulated game outcomes)
     */
    void simulateRemainingGames(Season& season);
    
    /**
     * Determine playoff teams for current season state.
     * Uses tiebreaker rules to determine division winners and wild cards.
     * 
     * @param season Completed season
     * @return PlayoffOutcome with seeding information
     */
    PlayoffOutcome determinePlayoffs(const Season& season);

    /**
     * Return next unplayed week number, or -1 if all games are final.
     */
    int nextUnplayedWeek(const Season& season) const;

    /**
     * Build a season copy with one scheduled game forced to a final outcome.
     */
    Season forceGameOutcome(const Season& season,
                            const Game& targetGame,
                            bool homeWins) const;
};

#endif // MONTECARLO_H
