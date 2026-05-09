#ifndef ASCII_PRINTER_H
#define ASCII_PRINTER_H

#include "model/Season.h"
#include <string>

/**
 * Formats and prints standings in ASCII table format.
 */
class AsciiPrinter {
public:
    /**
     * Print a division's standings table to stdout.
     */
    static void printDivisionStandings(const Season& season,
                                       const std::string& division);

    /**
     * Print all divisions (organized by conference) to stdout.
     */
    static void printAllStandings(const Season& season);

private:
    /**
     * Print a horizontal separator line.
     */
    static void printSeparator(int width);

    /**
     * Format a single row (team with record).
     */
    static std::string formatTeamRow(const Team* team);
};

#endif // ASCII_PRINTER_H
