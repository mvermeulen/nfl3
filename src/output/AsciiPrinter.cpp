#include "AsciiPrinter.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

void AsciiPrinter::printDivisionStandings(const Season& season,
                                          const std::string& division) {
    auto teams = season.teamsByDivision(division);
    if (teams.empty()) {
        return;
    }

    // Print division header
    std::cout << "\n" << division << std::endl;
    printSeparator(65);

    // Print column headers
    std::cout << std::left << std::setw(10) << "Team"
              << std::right << std::setw(6) << "Wins"
              << std::setw(6) << "Loss"
              << std::setw(6) << "Ties"
              << std::setw(8) << "Win %"
              << std::endl;
    printSeparator(65);

    // Print each team
    for (const auto* team : teams) {
        std::cout << std::left << std::setw(10) << team->abbreviation();
        std::cout << std::right << std::setw(6) << team->wins();
        std::cout << std::setw(6) << team->losses();
        std::cout << std::setw(6) << team->ties();
        
        // Win percentage
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << team->winPercentage();
        std::cout << std::setw(8) << oss.str();
        std::cout << std::endl;
    }
}

void AsciiPrinter::printAllStandings(const Season& season) {
    std::cout << "\n==== NFL STANDINGS ====\n" << std::endl;

    // Get all divisions sorted by conference
    auto divisions = season.getDivisions();
    std::sort(divisions.begin(), divisions.end());

    // Print AFC divisions first, then NFC
    std::vector<std::string> afcDivisions, nflDivisions;
    for (const auto& div : divisions) {
        if (div.find("AFC") != std::string::npos) {
            afcDivisions.push_back(div);
        } else {
            nflDivisions.push_back(div);
        }
    }

    std::cout << "========== AFC ==========" << std::endl;
    for (const auto& div : afcDivisions) {
        printDivisionStandings(season, div);
    }

    std::cout << "\n========== NFC ==========" << std::endl;
    for (const auto& div : nflDivisions) {
        printDivisionStandings(season, div);
    }

    std::cout << std::endl;
}

void AsciiPrinter::printSeparator(int width) {
    for (int i = 0; i < width; ++i) {
        std::cout << "-";
    }
    std::cout << std::endl;
}

std::string AsciiPrinter::formatTeamRow(const Team* team) {
    std::ostringstream oss;
    oss << std::left << std::setw(10) << team->abbreviation()
        << std::right << std::setw(6) << team->wins()
        << std::setw(6) << team->losses()
        << std::setw(6) << team->ties();
    return oss.str();
}
