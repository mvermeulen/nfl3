#ifndef CSV_PARSER_H
#define CSV_PARSER_H

#include <string>
#include <vector>
#include <map>

class CsvParser {
public:
    using Row = std::map<std::string, std::string>;
    using Table = std::vector<Row>;

    /**
     * Parse a CSV file and return a vector of maps (header -> value).
     * Handles quoted fields and escaped quotes.
     * 
     * @param filename Path to the CSV file
     * @return Vector of maps, one per row, with column headers as keys
     * @throws std::runtime_error if file cannot be opened or parsing fails
     */
    static Table parse(const std::string& filename);

    /**
     * Write a table back to CSV file (round-trip support).
     * 
     * @param filename Path to write to
     * @param headers Column names in order
     * @param table Vector of rows to write
     * @throws std::runtime_error if file cannot be written
     */
    static void write(const std::string& filename,
                      const std::vector<std::string>& headers,
                      const Table& table);

private:
    /**
     * Parse a single CSV line, handling quoted fields and commas.
     * 
     * @param line Raw CSV line
     * @return Vector of field values
     */
    static std::vector<std::string> parseLine(const std::string& line);

    /**
     * Unquote and unescape a field value.
     * 
     * @param field Raw field (may be quoted)
     * @return Unquoted field
     */
    static std::string unquoteField(const std::string& field);

    /**
     * Quote and escape a field value for CSV output.
     * 
     * @param field Raw field value
     * @return Quoted field (if needed)
     */
    static std::string quoteField(const std::string& field);
};

#endif // CSV_PARSER_H
