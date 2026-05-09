#include <iostream>
#include <algorithm>
#include <iomanip>
#include <string>
#include "util/CsvParser.h"
#include "model/Team.h"
#include "model/Game.h"
#include "model/Season.h"
#include "model/MonteCarlo.h"
#include "output/AsciiPrinter.h"

int main(int argc, char* argv[]) {
    try {
        std::string command = "status";
        if (argc > 1) {
            command = argv[1];
        }

        std::cout << "=== nfl3 NFL Game State Tracker ===" << std::endl << std::endl;

        // Load teams
        std::cout << "Loading teams from data/teams.csv..." << std::endl;
        auto teamsData = CsvParser::parse("data/teams.csv");
        
        Season season;
        for (const auto& row : teamsData) {
            std::string abbr = row.at("abbreviation");
            std::string fullName = row.at("full_name");
            std::string conference = row.at("conference");
            std::string division = row.at("division");
            
            Team team(abbr, fullName, conference, division);
            season.addTeam(team);
        }
        std::cout << "Loaded " << teamsData.size() << " teams." << std::endl << std::endl;

        // Load schedule
        std::cout << "Loading schedule from data/schedule.csv..." << std::endl;
        auto scheduleData = CsvParser::parse("data/schedule.csv");
        
        for (const auto& row : scheduleData) {
            int week = std::stoi(row.at("week"));
            std::string date = row.at("date");
            std::string homeTeam = row.at("home_team");
            std::string awayTeam = row.at("away_team");
            int homeScore = std::stoi(row.at("home_score"));
            int awayScore = std::stoi(row.at("away_score"));
            std::string status = row.at("status");
            
            Game game(week, date, homeTeam, awayTeam, homeScore, awayScore, status);
            season.addGame(game);
        }
        std::cout << "Loaded " << scheduleData.size() << " games." << std::endl << std::endl;

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
        } else {
            std::cerr << "Unknown command: " << command << "\n"
                      << "Usage: ./nfl3 [status|simulate|impact] [iterations]" << std::endl;
            return 1;
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
