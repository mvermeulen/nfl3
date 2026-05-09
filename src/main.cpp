#include <iostream>
#include "util/CsvParser.h"
#include "model/Team.h"
#include "model/Game.h"
#include "model/Season.h"
#include "output/AsciiPrinter.h"

int main() {
    try {
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

        // Compute standings
        std::cout << "Computing standings..." << std::endl;
        season.computeStandings();

        // Display standings
        AsciiPrinter::printAllStandings(season);

        std::cout << "Standings computed successfully!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
