#include "MonteCarlo.h"
#include <cmath>
#include <algorithm>
#include <exception>
#include <map>

namespace {

int detectSeasonYear(const Season& season) {
    int seasonYear = -1;
    for (const auto& game : season.allGames()) {
        const std::string& date = game.date();
        if (date.size() < 4) {
            continue;
        }
        try {
            const int year = std::stoi(date.substr(0, 4));
            if (seasonYear < 0 || year < seasonYear) {
                seasonYear = year;
            }
        } catch (const std::exception&) {
            // Ignore malformed dates and keep scanning.
        }
    }
    return seasonYear;
}

int fallbackGamesPerTeamFromYear(int seasonYear) {
    if (seasonYear >= 2021) {
        return 17;
    }
    return 16;
}

int inferScheduledGamesPerTeam(const Season& season, int fallback) {
    std::map<std::string, int> scheduleCounts;
    for (const auto& game : season.allGames()) {
        scheduleCounts[game.homeTeam()]++;
        scheduleCounts[game.awayTeam()]++;
    }

    int maxGames = 0;
    for (const auto& [abbr, count] : scheduleCounts) {
        if (count > maxGames) {
            maxGames = count;
        }
    }

    return maxGames > 0 ? maxGames : fallback;
}

int wildcardSpotsPerConference(int seasonYear) {
    // NFL expanded from 2 to 3 wild cards per conference in 2020.
    if (seasonYear >= 2020) {
        return 3;
    }
    return 2;
}

} // namespace

ImpactAnalysisResults MonteCarlo::analyzeImpact(const Season& season,
                                                int iterations,
                                                unsigned int seed) {
    ImpactAnalysisResults results;
    results.week = nextUnplayedWeek(season);
    if (results.week < 0) {
        return results;
    }

    const auto baseline = simulate(season, iterations, seed);

    for (const auto& game : season.allGames()) {
        if (game.week() != results.week || game.isPlayed()) {
            continue;
        }

        Season homeWinSeason = forceGameOutcome(season, game, true);
        Season awayWinSeason = forceGameOutcome(season, game, false);

        const auto homeWinResults = simulate(homeWinSeason, iterations, seed);
        const auto awayWinResults = simulate(awayWinSeason, iterations, seed);

        GameImpact impact{};
        impact.week = game.week();
        impact.date = game.date();
        impact.homeTeam = game.homeTeam();
        impact.awayTeam = game.awayTeam();
        impact.homePlayoffProbIfHomeWins = homeWinResults.playoffProbability.at(game.homeTeam());
        impact.awayPlayoffProbIfAwayWins = awayWinResults.playoffProbability.at(game.awayTeam());
        impact.homeDeltaPlayoffProb =
            impact.homePlayoffProbIfHomeWins - baseline.playoffProbability.at(game.homeTeam());
        impact.awayDeltaPlayoffProb =
            impact.awayPlayoffProbIfAwayWins - baseline.playoffProbability.at(game.awayTeam());

        results.gameImpacts.push_back(impact);
    }

    std::sort(results.gameImpacts.begin(), results.gameImpacts.end(),
              [](const GameImpact& a, const GameImpact& b) {
                  const double magA = std::abs(a.homeDeltaPlayoffProb) + std::abs(a.awayDeltaPlayoffProb);
                  const double magB = std::abs(b.homeDeltaPlayoffProb) + std::abs(b.awayDeltaPlayoffProb);
                  return magA > magB;
              });

    return results;
}

SimulationResults MonteCarlo::simulate(const Season& season, 
                                       int iterations,
                                       unsigned int seed) {
    // Initialize random number generator
    if (seed == 0) {
        rng_.seed(std::random_device{}());
    } else {
        rng_.seed(seed);
    }
    
    SimulationResults results;
    results.totalIterations = iterations;

    const int seasonYear = detectSeasonYear(season);
    const int totalGamesPerTeam = inferScheduledGamesPerTeam(
        season,
        fallbackGamesPerTeamFromYear(seasonYear)
    );
    
    // Initialize all probabilities to 0
    for (const auto& [abbr, team] : season.allTeams()) {
        results.playoffProbability[abbr] = 0.0;
        results.divisionWinProbability[abbr] = 0.0;
        results.wildcardProbability[abbr] = 0.0;
        results.teamWinProbability[abbr] = 0.0;
    }
    
    // Run simulations
    PlayoffOutcome aggregateOutcomes;
    for (int i = 0; i < iterations; ++i) {
        PlayoffOutcome outcome = simulateIteration(season);
        
        // Aggregate outcomes
        for (const auto& [abbr, count] : outcome.playoffAppearances) {
            aggregateOutcomes.playoffAppearances[abbr] += count;
        }
        for (const auto& [abbr, count] : outcome.divisionWins) {
            aggregateOutcomes.divisionWins[abbr] += count;
        }
        for (const auto& [abbr, count] : outcome.wildcardWins) {
            aggregateOutcomes.wildcardWins[abbr] += count;
        }
    }
    
    // Convert counts to probabilities
    for (const auto& [abbr, team] : season.allTeams()) {
        // Playoff probability
        if (aggregateOutcomes.playoffAppearances.count(abbr)) {
            results.playoffProbability[abbr] = 
                static_cast<double>(aggregateOutcomes.playoffAppearances[abbr]) / iterations;
        }
        
        // Division winner probability
        if (aggregateOutcomes.divisionWins.count(abbr)) {
            results.divisionWinProbability[abbr] = 
                static_cast<double>(aggregateOutcomes.divisionWins[abbr]) / iterations;
        }
        
        // Wild card probability
        if (aggregateOutcomes.wildcardWins.count(abbr)) {
            results.wildcardProbability[abbr] = 
                static_cast<double>(aggregateOutcomes.wildcardWins[abbr]) / iterations;
        }
        
        // Estimated end-of-season win percentage
        const int remainingGames = std::max(0, totalGamesPerTeam - team.gamesPlayed());
        const double estimatedWins = team.wins() + results.playoffProbability[abbr] * remainingGames;
        results.teamWinProbability[abbr] = estimatedWins / static_cast<double>(totalGamesPerTeam);
    }
    
    results.outcomes = aggregateOutcomes;
    return results;
}

double MonteCarlo::getWinProbability(const Team& homeTeam, const Team& awayTeam) const {
    // Get team strength factors
    double homeStrength = getTeamStrengthFactor(homeTeam);
    double awayStrength = getTeamStrengthFactor(awayTeam);
    
    // Normalize: if both are 0.5, home team should have 57% win rate
    // Weighted combination of strength and home advantage
    double homeWinProb = homeAdvantage_;
    
    // Adjust based on strength differential
    // If home team is stronger, increase probability; if weaker, decrease
    double strengthDiff = homeStrength - awayStrength;
    homeWinProb += strengthDiff * strengthWeight_;
    
    // Clamp to valid probability range
    homeWinProb = std::max(0.0, std::min(1.0, homeWinProb));
    
    return homeWinProb;
}

void MonteCarlo::setModelParameters(double homeAdvantage, double strengthWeight) {
    homeAdvantage_ = std::max(0.0, std::min(1.0, homeAdvantage));
    strengthWeight_ = std::max(0.0, strengthWeight);
}

PlayoffOutcome MonteCarlo::simulateIteration(const Season& season) {
    // Make a copy of the season to simulate on
    Season simSeason = season;
    
    // Simulate remaining games
    simulateRemainingGames(simSeason);
    
    // Compute standings with tiebreakers
    simSeason.computeStandings();
    
    // Determine playoff teams
    return determinePlayoffs(simSeason);
}

void MonteCarlo::simulateRemainingGames(Season& season) {
    std::uniform_real_distribution<> dist(0.0, 1.0);
    
    // Iterate through all current games and simulate unplayed ones
    const auto& games = season.allGames();
    std::vector<Game> updatedGames;
    
    for (const auto& game : games) {
        if (!game.isPlayed()) {
            // This game is unplayed - simulate its outcome
            const Team* homeTeam = season.getTeam(game.homeTeam());
            const Team* awayTeam = season.getTeam(game.awayTeam());
            
            if (homeTeam && awayTeam) {
                double homeWinProb = getWinProbability(*homeTeam, *awayTeam);
                double roll = dist(rng_);
                
                // Generate realistic NFL scores based on win probability
                // NFL average: ~23.4 points per team
                int homeScore, awayScore;
                if (roll < homeWinProb) {
                    // Home team wins - typically scores 7+ more points
                    homeScore = 21 + (rng_() % 23);  // 21-43 points (realistic range)
                    awayScore = 3 + (rng_() % 18);   // 3-20 points
                } else {
                    // Away team wins
                    homeScore = 3 + (rng_() % 18);   // 3-20 points
                    awayScore = 21 + (rng_() % 23);  // 21-43 points
                }
                
                // Create new game with simulated result
                Game simulatedGame(game.week(), game.date(),
                                 game.homeTeam(), game.awayTeam(),
                                 homeScore, awayScore, "final");
                updatedGames.push_back(simulatedGame);
            }
        } else {
            // Game already played - keep as-is
            updatedGames.push_back(game);
        }
    }
    
    // Replace season games with simulated results
    season.replaceGames(updatedGames);
}

PlayoffOutcome MonteCarlo::determinePlayoffs(const Season& season) {
    PlayoffOutcome outcome;
    const int seasonYear = detectSeasonYear(season);
    
    // Get all divisions
    auto divisions = season.getDivisions();
    
    // Get division winners (first place in each division)
    std::vector<std::string> playoffTeams;
    
    for (const auto& division : divisions) {
        auto divStandings = season.teamsByDivision(division);
        if (!divStandings.empty()) {
            auto* divWinner = divStandings[0];
            playoffTeams.push_back(divWinner->abbreviation());
            outcome.divisionWins[divWinner->abbreviation()]++;
        }
    }
    
    // Get conference wild cards
    // Count teams in each conference
    std::map<std::string, std::vector<const Team*>> conferenceTeams;
    for (const auto& [abbr, team] : season.allTeams()) {
        conferenceTeams[team.conference()].push_back(&team);
    }
    
    // For each conference, find top teams by record (excluding division winners)
    for (const auto& [conf, teams] : conferenceTeams) {
        // Get standings by conference
        auto confStandings = season.teamsByConference(conf);
        
        // Count wild cards (additional playoff spots beyond division winners)
        const int wildcardSpots = wildcardSpotsPerConference(seasonYear);
        int wildcardAdded = 0;
        
        for (const auto* team : confStandings) {
            // Check if already a division winner
            auto it = std::find(playoffTeams.begin(), playoffTeams.end(), 
                               team->abbreviation());
            
            if (it == playoffTeams.end() && wildcardAdded < wildcardSpots) {
                playoffTeams.push_back(team->abbreviation());
                outcome.wildcardWins[team->abbreviation()]++;
                wildcardAdded++;
            }
        }
    }
    
    // Record all playoff appearances
    for (const auto& abbr : playoffTeams) {
        outcome.playoffAppearances[abbr]++;
    }
    
    return outcome;
}

double MonteCarlo::getTeamStrengthFactor(const Team& team) const {
    // Simple strength factor based on win percentage
    // Starting from 0.5 (average), adjust based on record
    
    double winPct = team.winPercentage();
    
    // Map win percentage to strength factor
    // 0% record = 0.3 strength (below average)
    // 50% record = 0.5 strength (average)
    // 100% record = 0.7 strength (above average)
    double strengthFactor = 0.5 + (winPct - 0.5) * 0.4;
    
    // Discount early-season records (fewer games = less signal)
    int gamesPlayed = team.gamesPlayed();
    if (gamesPlayed < 4) {
        // Regress toward 0.5 (no information) for very few games
        double confidence = gamesPlayed / 4.0;
        strengthFactor = 0.5 + (strengthFactor - 0.5) * confidence;
    }
    
    return strengthFactor;
}

int MonteCarlo::nextUnplayedWeek(const Season& season) const {
    int nextWeek = -1;
    for (const auto& game : season.allGames()) {
        if (!game.isPlayed() && (nextWeek < 0 || game.week() < nextWeek)) {
            nextWeek = game.week();
        }
    }
    return nextWeek;
}

Season MonteCarlo::forceGameOutcome(const Season& season,
                                    const Game& targetGame,
                                    bool homeWins) const {
    Season forcedSeason = season;
    std::vector<Game> updatedGames;
    updatedGames.reserve(season.allGames().size());

    bool replaced = false;
    for (const auto& game : season.allGames()) {
        if (!replaced &&
            game.week() == targetGame.week() &&
            game.date() == targetGame.date() &&
            game.homeTeam() == targetGame.homeTeam() &&
            game.awayTeam() == targetGame.awayTeam() &&
            !game.isPlayed()) {
            const int homeScore = homeWins ? 24 : 17;
            const int awayScore = homeWins ? 17 : 24;
            updatedGames.emplace_back(game.week(), game.date(), game.homeTeam(), game.awayTeam(),
                                      homeScore, awayScore, "final");
            replaced = true;
        } else {
            updatedGames.push_back(game);
        }
    }

    forcedSeason.replaceGames(updatedGames);
    return forcedSeason;
}
