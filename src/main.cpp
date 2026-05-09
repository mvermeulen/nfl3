#include <iostream>
#include "util/CsvParser.h"

int main() {
    try {
        std::cout << "=== nfl3 NFL Game State Tracker ===" << std::endl << std::endl;

        // Load teams
        std::cout << "Loading teams from data/teams.csv..." << std::endl;
        auto teams = CsvParser::parse("data/teams.csv");
        std::cout << "Loaded " << teams.size() << " teams:" << std::endl;
        
        for (const auto& team : teams) {
            std::string abbr = team.at("abbreviation");
            std::string name = team.at("full_name");
            std::string conference = team.at("conference");
            std::string division = team.at("division");
            std::cout << "  " << abbr << ": " << name << " (" << division << ")" << std::endl;
        }

        std::cout << std::endl;

        // Load schedule
        std::cout << "Loading schedule from data/schedule.csv..." << std::endl;
        auto schedule = CsvParser::parse("data/schedule.csv");
        std::cout << "Loaded " << schedule.size() << " games:" << std::endl;

        for (const auto& game : schedule) {
            std::string week = game.at("week");
            std::string date = game.at("date");
            std::string homeTeam = game.at("home_team");
            std::string awayTeam = game.at("away_team");
            std::string homeScore = game.at("home_score");
            std::string awayScore = game.at("away_score");
            std::string status = game.at("status");
            
            std::cout << "  Week " << week << " (" << date << "): " 
                      << awayTeam << " @ " << homeTeam;
            
            if (status == "final") {
                std::cout << " [FINAL] " << homeScore << "-" << awayScore;
            } else if (status == "in_progress") {
                std::cout << " [IN PROGRESS] " << homeScore << "-" << awayScore;
            } else {
                std::cout << " [SCHEDULED]";
            }
            std::cout << std::endl;
        }

        std::cout << std::endl << "Data layer initialized successfully!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
