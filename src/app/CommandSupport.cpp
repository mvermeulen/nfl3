#include "app/CommandSupport.h"

#include "util/BacktestFixtureLoader.h"
#include "util/CsvParser.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nfl3 {

namespace {

double clampProbability(double p) {
    return std::max(1e-6, std::min(1.0 - 1e-6, p));
}

double sigmoid(double x) {
    if (x >= 0.0) {
        const double z = std::exp(-x);
        return 1.0 / (1.0 + z);
    }
    const double z = std::exp(x);
    return z / (1.0 + z);
}

double logit(double p) {
    const double c = clampProbability(p);
    return std::log(c / (1.0 - c));
}

std::string requireField(const CsvParser::Row& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end()) {
        throw std::runtime_error("Missing CSV field: " + key);
    }
    return it->second;
}

void loadTeams(Season& season, const std::string& teamsPath) {
    const auto teamsData = CsvParser::parse(teamsPath);
    for (const auto& row : teamsData) {
        Team team(requireField(row, "abbreviation"),
                  requireField(row, "full_name"),
                  requireField(row, "conference"),
                  requireField(row, "division"));
        season.addTeam(team);
    }
}

void loadSchedule(Season& season, const std::string& schedulePath) {
    const auto scheduleData = CsvParser::parse(schedulePath);
    for (const auto& row : scheduleData) {
        season.addGame(Game(
            std::stoi(requireField(row, "week")),
            requireField(row, "date"),
            requireField(row, "home_team"),
            requireField(row, "away_team"),
            std::stoi(requireField(row, "home_score")),
            std::stoi(requireField(row, "away_score")),
            requireField(row, "status")));
    }
}

} // namespace

Season loadSeasonFromCsvFiles(const std::string& teamsPath,
                              const std::string& schedulePath) {
    Season season;
    loadTeams(season, teamsPath);
    loadSchedule(season, schedulePath);
    return season;
}

void copyScheduleToPath(const std::string& sourcePath,
                        const std::string& destinationPath) {
    const auto source = CsvParser::parse(sourcePath);

    // Validate schema by reading required columns on every row.
    for (const auto& row : source) {
        static_cast<void>(requireField(row, "week"));
        static_cast<void>(requireField(row, "date"));
        static_cast<void>(requireField(row, "home_team"));
        static_cast<void>(requireField(row, "away_team"));
        static_cast<void>(requireField(row, "home_score"));
        static_cast<void>(requireField(row, "away_score"));
        static_cast<void>(requireField(row, "status"));
    }

    CsvParser::write(destinationPath,
                     {"week", "date", "home_team", "away_team", "home_score", "away_score", "status"},
                     source);
}

FittedModel fitModelFromHistoricalYear(int targetYear,
                                       const std::string& teamsPath,
                                       const std::string& historicalDir) {
    const int priorYear = targetYear - 1;
    auto fixtures = BacktestFixtureLoader::loadSeasons(
        teamsPath,
        historicalDir,
        {priorYear, targetYear});

    if (fixtures.size() != 2) {
        throw std::runtime_error("Unable to load prior/target historical fixtures");
    }

    Season prior = fixtures[0].season;
    Season target = fixtures[1].season;
    prior.computeStandings();

    std::vector<double> xs;
    std::vector<double> ys;

    for (const auto& game : target.allGames()) {
        if (!game.isPlayed()) {
            continue;
        }

        const Team* homePrior = prior.getTeam(game.homeTeam());
        const Team* awayPrior = prior.getTeam(game.awayTeam());
        if (!homePrior || !awayPrior) {
            continue;
        }

        const double x = homePrior->winPercentage() - awayPrior->winPercentage();
        double y = 0.5;
        if (game.homeTeamWon()) {
            y = 1.0;
        } else if (game.awayTeamWon()) {
            y = 0.0;
        }

        xs.push_back(x);
        ys.push_back(y);
    }

    if (xs.empty()) {
        throw std::runtime_error("No games available for fitting");
    }

    double sumY = 0.0;
    for (const double y : ys) {
        sumY += y;
    }

    // Fit logistic model p(home_win) = sigmoid(b0 + b1 * x).
    double b0 = logit(sumY / static_cast<double>(ys.size()));
    double b1 = 0.0;

    const double invN = 1.0 / static_cast<double>(xs.size());
    const double learningRate = 0.35;
    const double l2 = 1e-4;
    for (int iter = 0; iter < 2500; ++iter) {
        double grad0 = 0.0;
        double grad1 = 0.0;
        for (size_t i = 0; i < xs.size(); ++i) {
            const double p = sigmoid(b0 + b1 * xs[i]);
            const double error = p - ys[i];
            grad0 += error;
            grad1 += error * xs[i];
        }
        grad0 *= invN;
        grad1 = grad1 * invN + l2 * b1;

        b0 -= learningRate * grad0;
        b1 -= learningRate * grad1;
    }

    FittedModel model;
    model.sampleSize = static_cast<int>(xs.size());
    model.homeAdvantage = clampProbability(sigmoid(b0));
    model.slopePrevWinPct = b1;

    // Internally, MonteCarlo applies strengthWeight to team-strength diff where
    // strength diff is approximately 0.4 * previous-win-pct diff.
    model.strengthWeight = std::max(0.0, b1 / 0.4);

    double brier = 0.0;
    double logLoss = 0.0;
    for (size_t i = 0; i < xs.size(); ++i) {
        const double p = clampProbability(sigmoid(b0 + b1 * xs[i]));
        const double y = ys[i];
        const double diff = p - y;
        brier += diff * diff;
        logLoss += -(y * std::log(p) + (1.0 - y) * std::log(1.0 - p));
    }
    model.brierScore = brier * invN;
    model.logLoss = logLoss * invN;

    return model;
}

void persistFittedModel(const FittedModel& model,
                        int targetYear,
                        const std::string& outputPath) {
    CsvParser::Table rows;

    rows.push_back({{"coefficient", "target_year"}, {"value", std::to_string(targetYear)}});
    rows.push_back({{"coefficient", "sample_size"}, {"value", std::to_string(model.sampleSize)}});
    rows.push_back({{"coefficient", "home_advantage"}, {"value", std::to_string(model.homeAdvantage)}});
    rows.push_back({{"coefficient", "slope_prev_winpct"}, {"value", std::to_string(model.slopePrevWinPct)}});
    rows.push_back({{"coefficient", "strength_weight"}, {"value", std::to_string(model.strengthWeight)}});
    rows.push_back({{"coefficient", "brier_score"}, {"value", std::to_string(model.brierScore)}});
    rows.push_back({{"coefficient", "log_loss"}, {"value", std::to_string(model.logLoss)}});

    CsvParser::write(outputPath, {"coefficient", "value"}, rows);
}

bool loadFittedModel(const std::string& path, FittedModel& model) {
    std::ifstream in(path);
    if (!in.good()) {
        return false;
    }

    const auto rows = CsvParser::parse(path);
    for (const auto& row : rows) {
        const std::string key = requireField(row, "coefficient");
        const std::string value = requireField(row, "value");
        if (key == "home_advantage") {
            model.homeAdvantage = std::stod(value);
        } else if (key == "strength_weight") {
            model.strengthWeight = std::stod(value);
        } else if (key == "slope_prev_winpct") {
            model.slopePrevWinPct = std::stod(value);
        } else if (key == "brier_score") {
            model.brierScore = std::stod(value);
        } else if (key == "log_loss") {
            model.logLoss = std::stod(value);
        } else if (key == "sample_size") {
            model.sampleSize = std::stoi(value);
        }
    }

    return true;
}

void applyModelToMonteCarlo(MonteCarlo& mc, const FittedModel& model) {
    mc.setModelParameters(model.homeAdvantage, model.strengthWeight);
}

} // namespace nfl3
