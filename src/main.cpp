#include <iostream>
#include <algorithm>
#include <iomanip>
#include <string>
#include <stdexcept>
#include "app/CommandSupport.h"
#include "model/Season.h"
#include "model/MonteCarlo.h"
#include "output/AsciiPrinter.h"
#include "output/WebServer.h"

namespace {

const std::string DEFAULT_TEAMS_PATH = "data/teams.csv";
const std::string DEFAULT_SCHEDULE_PATH = "data/schedule.csv";
const std::string DEFAULT_HISTORICAL_DIR = "data/historical";
const std::string DEFAULT_MODEL_COEFFS_PATH = "data/model_coefficients.csv";

void printUsage() {
    std::cerr << "Usage:\n"
              << "  ./nfl3 status\n"
              << "  ./nfl3 simulate [iterations]\n"
              << "  ./nfl3 impact [iterations]\n"
              << "  ./nfl3 load-schedule <path>\n"
              << "  ./nfl3 web [port]\n"
              << "  ./nfl3 backfit-model <year>\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        std::string command = "status";
        if (argc > 1) {
            command = argv[1];
        }

        std::cout << "=== nfl3 NFL Game State Tracker ===" << std::endl << std::endl;

        if (command == "load-schedule") {
            if (argc < 3) {
                throw std::runtime_error("load-schedule requires a CSV path");
            }
            nfl3::copyScheduleToPath(argv[2], DEFAULT_SCHEDULE_PATH);
            std::cout << "Loaded schedule from " << argv[2]
                      << " into " << DEFAULT_SCHEDULE_PATH << std::endl;
            return 0;
        }

        if (command == "backfit-model") {
            if (argc < 3) {
                throw std::runtime_error("backfit-model requires a target year");
            }

            const int year = std::stoi(argv[2]);
            const nfl3::FittedModel model = nfl3::fitModelFromHistoricalYear(
                year,
                DEFAULT_TEAMS_PATH,
                DEFAULT_HISTORICAL_DIR);

            nfl3::persistFittedModel(model, year, DEFAULT_MODEL_COEFFS_PATH);

            std::cout << "Fitted model using historical season " << year << "\n"
                      << "  Samples: " << model.sampleSize << "\n"
                      << "  Home advantage: " << std::fixed << std::setprecision(4)
                      << model.homeAdvantage << "\n"
                      << "  Slope (prev win pct diff): " << model.slopePrevWinPct << "\n"
                      << "  Strength weight (internal): " << model.strengthWeight << "\n"
                      << "Saved coefficients to " << DEFAULT_MODEL_COEFFS_PATH << std::endl;
            return 0;
        }

        // Load teams
        std::cout << "Loading teams from " << DEFAULT_TEAMS_PATH << "..." << std::endl;
        std::cout << "Loading schedule from " << DEFAULT_SCHEDULE_PATH << "..." << std::endl;
        Season season = nfl3::loadSeasonFromCsvFiles(DEFAULT_TEAMS_PATH, DEFAULT_SCHEDULE_PATH);
        std::cout << "Loaded " << season.allTeams().size() << " teams and "
                  << season.allGames().size() << " games." << std::endl << std::endl;

        nfl3::FittedModel model;
        if (nfl3::loadFittedModel(DEFAULT_MODEL_COEFFS_PATH, model)) {
            std::cout << "Using fitted model from " << DEFAULT_MODEL_COEFFS_PATH
                      << " (samples=" << model.sampleSize << ")" << std::endl;
        }

        if (command == "status") {
            std::cout << "Computing standings..." << std::endl;
            season.computeStandings();
            AsciiPrinter::printAllStandings(season);
            std::cout << "Standings computed successfully!" << std::endl;
        } else if (command == "simulate") {
            int iterations = 100000;
            if (argc > 2) {
                iterations = std::stoi(argv[2]);
            }

            std::cout << "Running Monte Carlo simulation (" << iterations
                      << " iterations)..." << std::endl;

            season.computeStandings();
            MonteCarlo mc;
            nfl3::applyModelToMonteCarlo(mc, model);
            auto results = mc.simulate(season, iterations, 12345);

            std::vector<std::pair<std::string, double>> ordered;
            for (const auto& [abbr, prob] : results.playoffProbability) {
                ordered.emplace_back(abbr, prob);
            }
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

            std::cout << "\nPlayoff probabilities:\n";
            std::cout << std::left << std::setw(8) << "Team"
                      << std::right << std::setw(14) << "Playoffs %"
                      << std::setw(14) << "Division %"
                      << std::setw(14) << "Wild Card %" << std::endl;
            std::cout << std::string(50, '-') << std::endl;
            for (const auto& [abbr, playoffProb] : ordered) {
                std::cout << std::left << std::setw(8) << abbr
                          << std::right << std::setw(13) << std::fixed << std::setprecision(1)
                          << playoffProb * 100.0
                          << std::setw(14) << results.divisionWinProbability[abbr] * 100.0
                          << std::setw(14) << results.wildcardProbability[abbr] * 100.0
                          << std::endl;
            }
        } else if (command == "impact") {
            int iterations = 100000;
            if (argc > 2) {
                iterations = std::stoi(argv[2]);
            }

            season.computeStandings();
            MonteCarlo mc;
            nfl3::applyModelToMonteCarlo(mc, model);
            auto impact = mc.analyzeImpact(season, iterations, 12345);

            if (impact.week < 0 || impact.gameImpacts.empty()) {
                std::cout << "No unplayed games found. Impact analysis not applicable." << std::endl;
                return 0;
            }

            std::cout << "\nImpact analysis for week " << impact.week
                      << " (" << iterations << " iterations per scenario):\n";
            std::cout << std::left << std::setw(14) << "Home"
                      << std::setw(14) << "Away"
                      << std::right << std::setw(14) << "Home Delta %"
                      << std::setw(14) << "Away Delta %" << std::endl;
            std::cout << std::string(56, '-') << std::endl;

            for (const auto& gameImpact : impact.gameImpacts) {
                std::cout << std::left << std::setw(14) << gameImpact.homeTeam
                          << std::setw(14) << gameImpact.awayTeam
                          << std::right << std::setw(13) << std::fixed << std::setprecision(1)
                          << gameImpact.homeDeltaPlayoffProb * 100.0
                          << std::setw(14) << gameImpact.awayDeltaPlayoffProb * 100.0
                          << std::endl;
            }
        } else if (command == "web") {
            int port = 8080;
            if (argc > 2) {
                port = std::stoi(argv[2]);
            }

            WebServer server(season,
                             DEFAULT_SCHEDULE_PATH,
                             model.homeAdvantage,
                             model.strengthWeight,
                             100000);
            server.run(port);
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            printUsage();
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
